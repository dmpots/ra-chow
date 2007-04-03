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

//2-d array block x variable
extern Arena chow_arena;

/* exported functions */
Register GetMachineRegAssignment(Block*, LRID);
void CheckRegisterLimitFeasibility(void);
void LiveRange_BuildInitialSSA(void);
void RunChow();
void RenameRegisters();


//globals
const Register REG_UNALLOCATED = 666;

inline int max(int a, int b) { return a > b ? a : b;}

struct LiveRange;
namespace Chow {
  extern std::vector<LiveRange*> live_ranges;
}

#endif

