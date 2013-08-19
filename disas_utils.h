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

#pragma once

#include <windows.h>

#include "value.h"
#include "CONTEXT_utils.h"
#include "memorycache.h"
#include "datatypes.h"
#include "x86_disas.h"

#ifdef  __cplusplus
extern "C" {
#endif

bool Da_op_get_value_of_op (Da_op *op, address * rt_adr, const CONTEXT * ctx, MemoryCache *mem, const char *fname, unsigned fileline, s_Value *result);
bool Da_op_set_value_of_op (Da_op* op, s_Value *val, CONTEXT * ctx, MemoryCache *mem);
address Da_op_calc_adr_of_op (Da_op* op, const CONTEXT * ctx, MemoryCache *mem);

#ifdef  __cplusplus
}
#endif

/* vim: set expandtab ts=4 sw=4 : */
