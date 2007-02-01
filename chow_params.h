/*====================================================================
 * A place to list all of the paramaters used in the allocator
 *====================================================================
 ********************************************************************/

#ifndef __GUARD_CHOW_PARAMS_H
#define __GUARD_CHOW_PARAMS_H

extern Boolean      PARAM_MoveLoadsAndStores;
extern float        PARAM_MVCost;
extern float        PARAM_LDSave;
extern float        PARAM_STRSave;
extern float        PARAM_LoopDepthWeight;
extern unsigned int PARAM_BBMaxInsts;
extern unsigned int PARAM_NumReservedRegs;
extern unsigned int PARAM_NumMachineRegs;
extern Boolean      PARAM_EnableRegisterClasses;

#endif
