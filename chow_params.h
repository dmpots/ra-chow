/*====================================================================
 * A place to list all of the paramaters used in the allocator
 *====================================================================
 ********************************************************************/

#ifndef __GUARD_CHOW_PARAMS_H
#define __GUARD_CHOW_PARAMS_H

extern bool         PARAM_MoveLoadsAndStores;
extern bool         PARAM_EnhancedCodeMotion;
extern float        PARAM_MVCost;
extern float        PARAM_LDSave;
extern float        PARAM_STRSave;
extern float        PARAM_LoopDepthWeight;
extern unsigned int PARAM_BBMaxInsts;
extern unsigned int PARAM_NumReservedRegs;
extern unsigned int PARAM_NumMachineRegs;
extern bool         PARAM_EnableRegisterClasses;
extern bool         PARAM_ForceMinimumRegisterCount;

extern unsigned int DEBUG_DotDumpLR;

#endif
