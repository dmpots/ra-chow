/*====================================================================
 * rc.c (RegisterClass)
 * Holds functions and definitions used for implementing multiple
 * register classes in the chow register allocator
 *====================================================================
 * 
 ********************************************************************/

#include <stdlib.h>
#include <SSA.h>
#include "rc.h"
#include "debug.h"

/* global variabls */
Unsigned_Int cRegisterClass = -1u;

/* local variables */
static Def_Type* mLrId_DefType;
static Unsigned_Int* mRc_CReg;
static Boolean fEnableMultipleClasses = FALSE;
static VectorSet* mRc_VsTmp; 
//static Register**  mRcColor_Reg= NULL;

/* constants */
static const RegisterClass RegisterClass_UNIFIED = 0;
static const RegisterClass RegisterClass_INT = 0;
static const RegisterClass RegisterClass_FLOAT = 1;
static const RegisterClass RegisterClass_DOUBLE = 2;
//update this if adding more classes
static const Unsigned_Int NUM_REG_CLASSES = 2; 
//space between sequential allocation of registers per class.
//each register class will be allocated registers starting from
//rc*REGCLASS_SPACE 
static const Unsigned_Int REGCLASS_SPACE = 100;


/*
 *========================
 * RegisterClass_Init()
 *========================
 */
void InitRegisterClasses(Arena arena, Unsigned_Int cReg, Boolean fEnableClasses)
{
  cRegisterClass = 1;
  fEnableMultipleClasses = fEnableClasses;
  if(fEnableMultipleClasses)
  {
    cRegisterClass = NUM_REG_CLASSES;
  }

  //for now we have all register classes use the same number of
  //registers 
  mRc_CReg = (Unsigned_Int*)
    Arena_GetMemClear(arena, sizeof(Unsigned_Int)* cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRc_CReg[rc] = cReg;
  }

//allocate a map from colors to machine registers. we need such a
  //mapping because we use VectorSets to represent colors but don't
  //necessarily want color 0 to map to machine register 0
  /*
  mRcColor_Reg = (Unsigned_Int**)
     Arena_GetMemClear(arena, sizeof(Unsigned_Int) * cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    Unsigned_Int cRegs = mRc_CReg[rc];
    mRcColor_Reg[rc] = (Unsigned_Int*)
      Arena_GetMemClear(chow_arena, sizeof(Register) * cRegs);
    Register r = RegisterClass_MachineRegStart(rc);
    for(i = 0; i < cRegs; i++)
      mRcColor_Reg[rc][i] = r++;
  }
  */


  //allocate temporary vector sets sized to the number of available
  //registers for a given class. used in various live range
  //calculations
  mRc_VsTmp = (VectorSet*)
    Arena_GetMemClear(arena, sizeof(VectorSet) * cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRc_VsTmp[rc] = VectorSet_Create(arena, mRc_CReg[rc]);
  }
}

/*
 *========================
 * RegisterClass_InitialRegisterClassForLRID
 *========================
 * An initial mapping of LRIDs to register classes. This funciton is
 * what determines which class a LRID will belong to. If you want to
 * change the number of register classes used then mess around with
 * this function.
 */
RegisterClass RegisterClass_InitialRegisterClassForLRID(LRID lrid)
{
  //quick check to see if we are using multiple classes
  if(!fEnableMultipleClasses)
  {
    return RegisterClass_UNIFIED;
  }

  //for now just switch on type
  RegisterClass rc = -1u;
  Def_Type def_type = mLrId_DefType[lrid];
  switch(def_type)
  {
    case INT_DEF: 
      rc = RegisterClass_INT;
      break;
    case FLOAT_DEF:
    case DOUBLE_DEF:
      rc = RegisterClass_FLOAT;
      break;
    default:
      error("UNKNOWN INITIAL REGISTER CLASS (%d) FOR LRID: %d",
             def_type, lrid);
      abort();
  }

  return rc;
}

/*
 *========================
 * CreateLiveRangTypeMap
 *========================
 * Makes a mapping from live range id to def type
 */
void RegisterClass_CreateLiveRangeTypeMap(Arena arena, 
                                          Unsigned_Int lr_count,
                                          LRID* mSSAName_LRID)
{
  mLrId_DefType = (Def_Type*)
        Arena_GetMemClear(arena,sizeof(Def_Type) * lr_count);
  for(LOOPVAR i = 1; i < SSA_def_count; i++)
  {
    Def_Type def_type;
    Chain c = SSA_use_def_chains[i];
    if(c.is_phi_node)
    {
      def_type = c.op_pointer.phi_node->def_type;
    }
    else
    {
      def_type = Operation_Def_Type(c.op_pointer.operation, i);
    }
    debug("LRID: %3d ==> Type: %d", mSSAName_LRID[i], def_type);
    mLrId_DefType[mSSAName_LRID[i]] = def_type;
  }
}



/*
 *========================
 * RegisterClass_NumMachineReg()
 *========================
 * Returns the number of machine registers available for a given
 * register class
 */
Unsigned_Int RegisterClass_NumMachineReg(RegisterClass rc)
{
  return mRc_CReg[rc];
}


Register RegisterClass_MachineRegForColor(RegisterClass rc, Color c)
{
  //mRcColor_Reg[rc][c];
  Register r = (rc*REGCLASS_SPACE)+(c);
  debug("MACHINE REG(rc,c):(%d,%d) = %d",rc,c,r);
  return  r;
}

VectorSet RegisterClass_TmpVectorSet(RegisterClass rc)
{
  return mRc_VsTmp[rc];
}

