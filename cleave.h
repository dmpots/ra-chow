/*====================================================================
 * 
 *====================================================================
 * $Id: cleave.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/cleave.h $
 ********************************************************************/

#ifndef __GUARD_CLEAVE_H
#define __GUARD_CLEAVE_H
#include "debug.h"

struct block_tuple
{
  Block* top;
  Block* bottom;
};

typedef block_tuple BlockTuple;

void InitCleaver(Arena, Unsigned_Int cinLast);
void CleaveBlocks();
void CleaveBlocksWithPred(Inst* (*fnPred)(Block*));


#endif
