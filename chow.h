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
extern float wLoopDepth;



//usage statistics for variables
struct bb_stat
{
  Unsigned_Int defs;
  Unsigned_Int uses;
  Boolean start_with_def;
};

struct chow_stat
{
  Unsigned_Int clrInitial;
  Unsigned_Int clrFinal;
  Unsigned_Int clrColored;
  Unsigned_Int cSplits;
  Unsigned_Int cSpills;
  Unsigned_Int cChowStores;
  Unsigned_Int cChowLoads;
};

/* exported types */
typedef struct bb_stat BB_Stat;
typedef BB_Stat** BB_Stats;
typedef struct chow_stat Chow_Stats;

//2-d array block x variable
extern BB_Stats bb_stats;
extern Unsigned_Int** register_map;
extern Chow_Stats chowstats;


/* exported functions */
Register MachineRegForColor(Color c);
Inst* Insert_Store(LRID,Inst*,Register,Register,InstInsertLocation);
void Insert_Load(LRID, Inst*, Register, Register);
MemoryLocation ReserveStackSpace(Unsigned_Int size);

//globals
extern Variable GBL_fp_origname;

#endif

