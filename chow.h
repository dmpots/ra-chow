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

/* constants */
const Color NO_COLOR = -1u;

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
  Unsigned_Int cInsertedCopies;
  Unsigned_Int cThwartedCopies;
};

/* exported types */
typedef struct bb_stat BB_Stat;
typedef BB_Stat** BB_Stats;
typedef struct chow_stat Chow_Stats;

/* support multiple register classes */

//2-d array block x variable
extern BB_Stats bb_stats;
extern Unsigned_Int** mBlkIdSSAName_Color;
extern Chow_Stats chowstats;
extern Arena chow_arena;

/* exported functions */
Inst* Insert_Store(LRID,Inst*,Register,Register,InstInsertLocation);
void Insert_Load(LRID, Inst*, Register, Register);
MemoryLocation ReserveStackSpace(Unsigned_Int size);
LRID SSAName2LRID(Variable v);
Register GetMachineRegAssignment(Block*, LRID);
void CheckRegisterLimitFeasibility(void);
void LiveRange_BuildInitialSSA(void);
void RunChow();
void RenameRegisters();


//globals
extern Variable GBL_fp_origname;
const Register REG_UNALLOCATED = 666;
const Register REG_FP = 555;
const LRID     NO_LRID = (LRID)-1;//bigger than any lrid

inline int max(int a, int b) { return a > b ? a : b;}

#endif

