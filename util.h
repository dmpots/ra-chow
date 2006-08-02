/*====================================================================
 * General utility functions
 *====================================================================
 * $Id: util.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/util.h $
 ********************************************************************/

#ifndef __GUARD_UTIL_H
#define __GUARD_UTIL_H

#define Block_FirstInst(block) ((block)->inst->next_inst)
#define Block_LastInst(block) ((block)->inst->prev_inst)
#define Block_FirstOpcode(block) ((block)->inst->next_inst->operations[0]->opcode)

#endif

