/*
 *  _           _ _   
 * | |         | | |  
 * | |__   ___ | | |_ 
 * | '_ \ / _ \| | __|
 * | |_) | (_) | | |_ 
 * |_.__/ \___/|_|\__|
 *
 * Written by Dennis Yurichev <dennis(a)yurichev.com>, 2013
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial-NoDerivs 3.0 Unported License. 
 * To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-nd/3.0/.
 *
 */

#include <windows.h>
#include <stdbool.h>
#include "oassert.h"
#include <dbghelp.h>
#include "bolt_mingw_addons.h"
#include "memutils.h"
#include "PE.h"
#include "PE_imports.h"
#include "stuff.h"
#include "logging.h"
#include "dmalloc.h"
#include "bitfields.h"
#include "porg_utils.h"

#define NEW_IMPORT_SECTION_NAME "newidata"
#define DONT_RUN_TWICE "please don't run this utility twice!\n"
#define COPYRIGHT "\nthis import table was (re)generated by PE_add_import utility written by dennis@yurichev.com available at http://www.yurichev.com/PE_add_import.html\n"

void PE_util_add_symbol_to_imports (char *fname, char *dll_name, char *sym_name, WORD sym_ordinal,
		address pnt_VA_plus_original_base)
{
	LOADED_IMAGE im;

	MapAndLoad_or_die (fname, NULL, &im, false, /* ReadOnly */ false);

	if (PE_find_section_by_name (&im, NEW_IMPORT_SECTION_NAME))
		die ("there are section named %s in %s\n"
				DONT_RUN_TWICE, NEW_IMPORT_SECTION_NAME, fname);

	address pnt_RVA=pnt_VA_plus_original_base - PE_get_original_base (&im);
	address pnt_RVA_new_thunk=pnt_RVA+1;

	struct PE_get_imports_info* i_tbl=PE_get_imports (&im);
	if (i_tbl==NULL)
		die ("%s: no imports\n", fname);

	if (dll_present_in_imports (i_tbl, dll_name))
		die ("%s is already present in imports of %s\n"
				DONT_RUN_TWICE, dll_name, fname);

	add_DLL_and_symbol_to_imports (i_tbl, dll_name, sym_name, sym_ordinal, pnt_RVA_new_thunk);

	byte* pnt_VA=(byte*)ImageRvaToVa (im.FileHeader, im.MappedAddress, pnt_RVA, NULL);
	
	if (*pnt_VA==0xB8 && *(pnt_VA+5)==0xFF && *(pnt_VA+6)==0xE0)
		die ("MOV EAX / JMP EAX is already present at 0x" PRI_ADR_HEX "\n"
				DONT_RUN_TWICE, pnt_VA_plus_original_base);

	*pnt_VA=0xB8; // MOV EAX, dword32
	*(pnt_VA+5)=0xFF; // JMP EAX
	*(pnt_VA+6)=0xE0;
	
	// set code section writable
	IMAGE_SECTION_HEADER* sect_with_pnt=ImageRvaToSection (im.FileHeader, NULL, pnt_RVA);
	oassert(sect_with_pnt);
	SET_BIT (sect_with_pnt->Characteristics, IMAGE_SCN_MEM_WRITE);

	// calculate RVA for new section
	address next_available_RVA, next_available_phys_ofs;
	calculate_next_available_RVA_and_phys_ofs(&im, &next_available_RVA, &next_available_phys_ofs);

	i_tbl->start_RVA=next_available_RVA; // of new section
	byte buf[0x8000]; // FIXME! but I need tests...
	bzero(buf, sizeof(buf));
	size_t new_tbl_size=PE_generate_import_table (i_tbl, /* place_thunks */ false, buf, sizeof(buf));
	// add "copyright"!
	strcpy ((char*)buf+new_tbl_size+1, COPYRIGHT);

	size_t new_sect_size_of_raw_data=add_PE_section_at_end(&im, NEW_IMPORT_SECTION_NAME, new_tbl_size, IMAGE_SCN_MEM_READ + IMAGE_SCN_MEM_WRITE + IMAGE_SCN_CNT_INITIALIZED_DATA);

	// fix directory entry for imports
	set_data_directory_entry(&im, IMAGE_DIRECTORY_ENTRY_IMPORT, next_available_RVA, new_tbl_size);

	PE_get_imports_info_free(i_tbl);
	UnMapAndLoad_or_die (&im);

	// write import table at the end of file
	write_to_the_end_of_file_or_die (fname, buf, new_sect_size_of_raw_data);
	
	dump_unfreed_blocks();
};

int main(int argc, char* argv[])
{
	printf ("Simple tool for adding symbol to PE executable import table\n");
	printf ("<dennis@yurichev.com> (%s %s)\n", __DATE__, __TIME__);
	if(argc!=6)
	{
		printf ("usage: fname DLL_name sym_name sym_ordinal func_address\n");
		printf ("for example: winword.exe mydll.dll MyFunction 1 0x401122\n");
		printf ("mydll.dll!MyFunction will be added to import table of winword.exe\n");
		printf ("and, at address 0x401122 a JMP to mydll.dll!MyFunction will be written\n");
		return 1;
	};

	char *fname=argv[1];
	char *DLL_name=argv[2];
	char *sym_name=argv[3];
	int sym_ordinal=atoi(argv[4]);
	DWORD fn_adr=strtol(argv[5], NULL, 0);

	printf ("adding symbol %s!%s (ordinal %d) to %s at 0x%x\n", 
			DLL_name, sym_name, sym_ordinal, fname, fn_adr);
	PE_util_add_symbol_to_imports (fname, DLL_name, sym_name, sym_ordinal, fn_adr);
};
