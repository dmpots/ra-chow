#ifndef __GUARD_RC_H
#define __GUARD_RC_H

#include <Shared.h>
#include "types.h"

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
                         Boolean fEnableClasses);
VectorSet RegisterClass_TmpVectorSet(RegisterClass rc);


#endif
