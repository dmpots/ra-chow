#ifndef __GUARD_RC_H
#define __GUARD_RC_H

#include <Shared.h>
#include "types.h"

/* types */
//this struct holds information about the registers reserved for a
//given class. these reserved registers are used during register
//assignment to provide machine registers to any variable that was
//spilled
typedef struct reserved_regs_info
{
  Register* regs;         /* array of temporary registers */
  Unsigned_Int next_free; /* next free register from the pool */ 
  Unsigned_Int cReserved; /* count of number reserved */
} ReservedRegsInfo;

/* globals */
extern Unsigned_Int cRegisterClass;

/* exported functions */
RegisterClass RegisterClass_InitialRegisterClassForLRID(LRID lrid);
void RegisterClass_CreateLiveRangeTypeMap(Arena arena, 
                                          Unsigned_Int lr_count,
                                          LRID* mSSAName_LRID);
Unsigned_Int RegisterClass_NumMachineReg(RegisterClass);
Register RegisterClass_MachineRegForColor(RegisterClass, Color);
void InitRegisterClasses(Arena arena, 
                         Unsigned_Int cReg,
                         Boolean fEnableClasses,
                         Unsigned_Int cReserved);
VectorSet RegisterClass_TmpVectorSet(RegisterClass rc);


#endif
