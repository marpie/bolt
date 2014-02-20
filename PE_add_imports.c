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

/*
 * new imports table is created (newidata).
 * old imports table is still here: FirstThunk arrays will be used from there
 * fixups table is fixed if needed
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
#include "ostrings.h"
#include "x86.h"
#include "dmalloc.h"

#define DONT_RUN_TWICE "please don't run this utility twice!"
#define NEW_IMPORT_SECTION_NAME ".idata2"
#define NEW_RELOC_SECTION_NAME ".reloc2"
#define COPYRIGHT "\nthis import table was (re)generated by PE_add_import utility written by dennis@yurichev.com available at http://www.yurichev.com/PE_add_imports.html\n"

struct imports_table
{
	address a;
	char* dll;
	char* func_name;
#ifdef FAILED_STDCALL_SUPPORT
	unsigned stdcall_bytes; // 0 mean absent
#endif
	struct imports_table *next;
};

struct imports_table *table=NULL;
unsigned imports_table_t=0;

void load_imports_table(char *fname)
{
	FILE* hdl=fopen (fname, "rt");
	if (hdl==NULL)
		die ("can't open %s file\n", fname);
	
	
	unsigned line=1;
	for (;;line++)
	{
		char DLL_name[128];
		char sym_name[128];
		address fn_adr;

		if (fscanf (hdl, "0x" PRI_ADR_HEX " %128[^!]!%128[^\n]\n", &fn_adr, DLL_name, sym_name)==3)
		{
			struct imports_table* t;
			if (table==NULL)
				t=table=DCALLOC(struct imports_table, 1, "struct imports_table");
			else
			{
				for (t=table; t->next; t=t->next); // find last
				t->next=DCALLOC(struct imports_table, 1, "struct imports_table");
				t=t->next;
			};

			t->a=fn_adr;
			t->dll=DSTRDUP(DLL_name, "char*");
			t->func_name=DSTRDUP(sym_name, "char*");
#ifdef FAILED_STDCALL_SUPPORT
			char *tmp=strchr (t->func_name, '@');
			if (tmp)
			{
				if (sscanf(tmp+1, "%d", &t->stdcall_bytes)!=1)
					die ("unable to parse [%s]\n", tmp+1);
				//printf ("t->stdcall_bytes=%d\n", t->stdcall_bytes);
			};
#endif
			line++;
			imports_table_t++;
		}
		else
		{
			//printf ("can't parse line %d in %s\n", line, imports_table);
			break;
		};
	};
	if (table==NULL)
		die ("no lines processed from %s\n", fname);

	fclose(hdl);
};

void free_imports_table(struct imports_table *t)
{
	if (t==NULL)
		return;
	DFREE(t->dll);
	DFREE(t->func_name);
	free_imports_table(t->next);
	DFREE(t);
};

LOADED_IMAGE im;
DWORD *fixups, *new_fixups;
size_t fixups_t;
bool fixups_changed=false;
struct PE_get_imports_info *i_tbl;

void remove_fixups_in_region(address begin, address end)
{
	for (size_t i=0; i<fixups_t; i++)
		if (VAL_IN_BOUNDS_INCL (fixups[i], begin, end) ||
				VAL_IN_BOUNDS_INCL (fixups[i]+sizeof(address), begin, end))
		{
			//printf ("removing fixup %d (0x%x)\n", i, fixups[i]);
			fixups[i]=UINT32_MAX;
			fixups_changed=true;
		};
};

void add_JMP(struct imports_table *t)
{
	printf ("Adding JMP at 0x" PRI_ADR_HEX " (%s)\n", t->a, t->func_name);
#ifdef _WIN64
#define INS_LEN X64_JMP_REL_IMM32_LEN
#else
#define INS_LEN X86_JMP_or_CALL_ABS_IMM32_LEN
#endif
	address pnt_RVA=t->a - PE_get_original_base (&im);
	// there are shouldn't be any fixups on the newly inserted instruction!
	remove_fixups_in_region(pnt_RVA, pnt_RVA+INS_LEN);

	// x86: (cdecl) JMP: FF 25 xx xx xx xx (absolute address). FIXUP should be here.
	// x86: (stdcall) CALL: FF 15 xx xx xx xx (absolute address). FIXUP should be here.
	// x64: 48 FF 25 xx xx xx xx (relative to the next label)
	byte* pnt_VA=(byte*)ImageRvaToVa (im.FileHeader, im.MappedAddress, pnt_RVA, NULL);
#ifdef _WIN64
	memcpy (pnt_VA, X64_JMP_REL_IMM32, X64_JMP_REL_IMM32_LEN);
#else
#ifdef FAILED_STDCALL_SUPPORT
	if (t->stdcall_bytes)
		memcpy (pnt_VA, X86_CALL_ABS_IMM32, X86_JMP_or_CALL_ABS_IMM32_LEN);
	else
#endif // FAILED_STDCALL_SUPPORT
		memcpy (pnt_VA, X86_JMP_ABS_IMM32, X86_JMP_or_CALL_ABS_IMM32_LEN);
#endif // _WIN64
	// find FirstThunk place for this dll!func_name
	address thunk_place=PE_find_thunk_by_import (i_tbl, t->dll, t->func_name);
#ifdef _WIN64
	DWORD to_write=thunk_place - pnt_RVA - X64_JMP_REL_IMM32_LEN;
	*(DWORD*)(pnt_VA + X64_JMP_REL_IMM32_OFS_TO_IMM32)=to_write;
	//pnt_VA+=X64_JMP_REL_IMM32_LEN;
	//pnt_RVA+=X64_JMP_REL_IMM32_LEN;
#else
	DWORD to_write=thunk_place + PE_get_original_base (&im);
	*(DWORD*)(pnt_VA + X86_JMP_or_CALL_ABS_IMM32_OFS_TO_IMM32)=to_write;
	pnt_VA+=X86_JMP_or_CALL_ABS_IMM32_LEN;
	// FIXUP should be also applied.
	*new_fixups=pnt_RVA + X86_JMP_or_CALL_ABS_IMM32_OFS_TO_IMM32;
	pnt_RVA+=X86_JMP_or_CALL_ABS_IMM32_LEN;
	//printf ("new fixup=0x%x\n", *new_fixups);
	new_fixups++;
	fixups_changed=true;
#ifdef FAILED_STDCALL_SUPPORT
	if (t->stdcall_bytes)
	{
		// add SUB ESP, x
		memcpy (pnt_VA, X86_SUB_ESP_IMM8, X86_SUB_ESP_IMM8_LEN);
		oassert (t->stdcall_bytes<0x100);
		*(pnt_VA + X86_SUB_ESP_IMM8_OFS_TO_IMM8)=(byte)t->stdcall_bytes;
		remove_fixups_in_region(pnt_RVA, pnt_RVA+X86_SUB_ESP_IMM8_LEN);
		pnt_VA+=X86_SUB_ESP_IMM8_LEN;
		pnt_RVA+=X86_SUB_ESP_IMM8_LEN;

		// add RETN
		memcpy (pnt_VA, X86_RETN, X86_RETN_LEN);
		remove_fixups_in_region(pnt_RVA, pnt_RVA+X86_RETN_LEN);
	}
#endif // FAILED_STDCALL_SUPPORT
#endif // _WIN64
};

void add_JMPs()
{
	// FirstThunk is now present in the i_tbl for DLLs added
	for (struct imports_table *t=table; t; t=t->next)
		add_JMP(t);
};

byte *new_fixups_section=NULL;
size_t new_fixups_section_len=0;

void rebuild_fixups_section()
{
	printf ("Rebuilding FIXUPs section...\n");
	//printf ("fixups_t=%d\n", fixups_t);
	// remove all -1 from array
	tetrabyte_array_remove_all_values((tetrabyte**)&fixups, UINT32_MAX, &fixups_t, true /*call_drealloc*/);
	//printf ("new fixups_t=%d\n", fixups_t);
	//for (unsigned i=0; i<fixups_t; i++)
	//	printf ("new fixup 0x%x\n", fixups[i]);

	qsort (fixups, fixups_t, sizeof(DWORD), &qsort_compare_tetrabytes);

	size_t fixup_section_size;
	byte* fixups_section=generate_fixups_section (fixups, fixups_t, &fixup_section_size);

	size_t reloc_directory_size;
	tetrabyte *reloc_directory=PE_get_reloc_directory (&im, PE_is_PE32(&im), &reloc_directory_size);

	// including case of absent fixups table
	if (fixup_section_size > reloc_directory_size)
	{
		if (reloc_directory_size==0)
			printf ("reloc section is absent.\n");
		else
			printf ("generated reloc section is bigger than what is in original file.\n");
		printf ("adding new reloc section.\n");
		new_fixups_section_len=align_to_boundary(fixup_section_size, 0x1000);
		new_fixups_section=DCALLOC (byte, new_fixups_section_len, "");
		memcpy (new_fixups_section, fixups_section, fixup_section_size);

		DWORD new_sect_RVA;
		size_t new_sect_size_of_raw_data=add_PE_section_at_end(&im, NEW_RELOC_SECTION_NAME, 
			new_fixups_section_len, 
			IMAGE_SCN_MEM_READ + IMAGE_SCN_CNT_INITIALIZED_DATA, &new_sect_RVA);
		set_data_directory_entry (&im, IMAGE_DIRECTORY_ENTRY_BASERELOC, 
				new_sect_RVA, new_fixups_section_len);
	}
	else
	{
		// clear fixup section first.
		bzero (reloc_directory, reloc_directory_size);
		// copy new fixup section
		memcpy (reloc_directory, fixups_section, fixup_section_size);
/*
 * FIXME: if to uncomment, the image will not load
		IMAGE_SECTION_HEADER *reloc_section=PE_find_reloc_section (&im);
		if (reloc_section==NULL)
			die ("Fatal error: can't find section devoted to FIXUPs\n");
		reloc_section->Misc.VirtualSize=fixup_section_size;
		//reloc_section.SizeOfRawData=;
*/
	};
	DFREE(fixups_section);
};

int main(int argc, char* argv[])
{
	printf ("Simple tool for adding symbols to PE executable import table\n");
	printf ("<dennis@yurichev.com> (%s %s)\n", __DATE__, __TIME__);
	if(argc!=3)
	{
		printf ("usage: fname.exe imports_table");
		printf ("for example: winword.exe imports_table.txt\n");
		printf ("imports_table.txt is a text file consisting of address, DLL and symbol names:\n");
		printf ("0x00401234 mydll1.dll!MyFunction1\n");
		printf ("0x00405678 mydll2.dll!MyFunction2\n");
		printf ("etc\n");
		return 1;
	};

	char *fname=argv[1];
	load_imports_table (argv[2]);

	MapAndLoad_or_die (fname, NULL, &im, false, /* ReadOnly */ false);

	if (PE_find_section_by_name (&im, NEW_IMPORT_SECTION_NAME))
		die ("%s there are section named %s in %s\n",
				DONT_RUN_TWICE, NEW_IMPORT_SECTION_NAME, fname);

	i_tbl=PE_get_imports (&im);
	if (i_tbl==NULL)
		die ("%s: no imports\n", fname); // FIXME

	for (struct imports_table *t=table; t; t=t->next)
		add_DLL_and_symbol_to_imports (i_tbl, t->dll, t->func_name, 0 /* hint */);

	address next_available_RVA;
	// calculate RVA for new section
	calculate_next_available_RVA_and_phys_ofs(&im, &next_available_RVA, NULL);

	i_tbl->start_RVA=next_available_RVA; // of new section
	size_t approx_tbl_size=PE_approx_import_table_size (i_tbl);
	size_t bufsize=approx_tbl_size*2;
	byte* buf=DMALLOC(byte, bufsize, "byte*");
	bzero(buf, bufsize);
	size_t size_of_IMAGE_DIRECTORY_ENTRY_IMPORT;
	printf ("Generating new import table...\n");
	size_t new_tbl_size=PE_generate_import_table (i_tbl, buf, bufsize, 
			&size_of_IMAGE_DIRECTORY_ENTRY_IMPORT);
	// add "copyright"!
	strcpy ((char*)buf+new_tbl_size+1, COPYRIGHT);

	DWORD new_sect_RVA;
	size_t new_sect_size_of_raw_data=add_PE_section_at_end(&im, NEW_IMPORT_SECTION_NAME, 
			new_tbl_size+strlen(COPYRIGHT), 
			IMAGE_SCN_MEM_READ + IMAGE_SCN_MEM_WRITE + IMAGE_SCN_CNT_INITIALIZED_DATA, &new_sect_RVA);

	// fix directory entry for imports
	set_data_directory_entry(&im, IMAGE_DIRECTORY_ENTRY_IMPORT, new_sect_RVA, size_of_IMAGE_DIRECTORY_ENTRY_IMPORT);
	// read fixups
	fixups=make_array_of_fixups (&im, &fixups_t);
	//printf ("(as in file) fixups_t=%d\n", fixups_t);
	//for (unsigned i=0; i<fixups_t; i++)
	//	printf ("original fixup 0x%x\n", fixups[i]);
	
	// add place for new fixups
	fixups=DREALLOC(fixups, DWORD, fixups_t+imports_table_t, "fixups");
	new_fixups=fixups+fixups_t;
	fixups_t+=imports_table_t;

	// now add JMP-s
	add_JMPs();

	PE_get_imports_info_free(i_tbl);

	// rebuild fixups if needed
	// do not work out fixups if the executable not have it intentionally
	if (fixups_changed && 
			IS_SET(im.FileHeader->FileHeader.Characteristics, IMAGE_FILE_RELOCS_STRIPPED)==false)
		rebuild_fixups_section();
	
	DFREE(fixups);

	UnMapAndLoad_or_die (&im);

	// write import table at the end of file
	write_to_the_end_of_file_or_die (fname, buf, new_sect_size_of_raw_data);

	if (new_fixups_section)
	{
		write_to_the_end_of_file_or_die (fname, new_fixups_section, new_fixups_section_len);
		DFREE(new_fixups_section);
	};

	DFREE(buf);
	free_imports_table(table);

	PE_fix_checksum (fname);	

	dump_unfreed_blocks();
};
