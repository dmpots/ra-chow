/*====================================================================
 * 
 *====================================================================
 * $Id: chow.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/chow.h $
 ********************************************************************/

#ifndef __GUARD_CHOW_H
#define __GUARD_CHOW_H

#include <Shared.h>
#include "types.h"

/*
 * allocation parameters 
 */
extern Unsigned_Int mRegisters;
extern float mMVCost;
extern float mLDSave;
extern float mSTRSave;



//usage statistics for variables
struct bb_stat
{
  Unsigned_Int defs;
  Unsigned_Int uses;
  Boolean start_with_def;
};

/* exported types */
typedef struct bb_stat BB_Stat;
typedef BB_Stat** BB_Stats;

//2-d array block x variable
extern BB_Stats bb_stats;
extern Unsigned_Int** register_map;


/* exported functions */
Register MachineRegForColor(Color c);
Inst* Insert_Store(LRID,Inst*,Register,Register,InstInsertLocation);
void Insert_Load(LRID, Inst*, Register, Register);
MemoryLocation ReserveStackSpace(Unsigned_Int size);

//globals
extern Variable GBL_fp_origname;

#endif

