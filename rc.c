/*====================================================================
 * rc.c (RegisterClass)
 * Holds functions and definitions used for implementing multiple
 * register classes in the chow register allocator
 *====================================================================
 * 
 ********************************************************************/

#include <stdlib.h>
#include <vector>
#include <map>
#include <list>
#include <utility>

#include <SSA.h>
#include "rc.h"
#include "assign.h"
#include "debug.h"
#include "mapping.h"

/* global variabls */
Unsigned_Int cRegisterClass = -1u;

/* local variables */
static Def_Type* mLrId_DefType;
static Unsigned_Int* mRc_CReg;
static Boolean fEnableMultipleClasses = FALSE;
static VectorSet* mRc_VsTmp; 
static ReservedRegsInfo* mRc_ReservedRegs;
static RegisterContents* mRc_RegisterContents = NULL;
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

/* local functions */
Unsigned_Int RegisterClass_FirstRegister(RegisterClass rc);


/*
 *========================
 * RegisterClass_Init()
 *========================
 */
void InitRegisterClasses(Arena arena, 
                         Unsigned_Int cReg, 
                         Boolean fEnableClasses,
                         Unsigned_Int cReserved)
{
  cRegisterClass = 1;
  fEnableMultipleClasses = fEnableClasses;
  if(fEnableMultipleClasses)
  {
    cRegisterClass = NUM_REG_CLASSES;
  }
  debug("using %d register classes", cRegisterClass);

  //for now we have all register classes use the same number of
  //registers 
  mRc_CReg = (Unsigned_Int*)
    Arena_GetMemClear(arena, sizeof(Unsigned_Int)* cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRc_CReg[rc] = cReg - cReserved;
  }

  //allocate an array of reserved register structs. these are used
  //during register assignment when we need registers for holding
  //spilled values
  mRc_ReservedRegs = (ReservedRegsInfo*)
    Arena_GetMemClear(arena, sizeof(ReservedRegsInfo) * cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    //allocate and initialize the array of registers for this class
    ReservedRegsInfo* reserved = &(mRc_ReservedRegs[rc]);
    reserved->regs = (Register*)
      Arena_GetMemClear(arena, sizeof(Register) * cReserved);
    for(LOOPVAR i = 0; i < cReserved; i++)
    {
      reserved->regs[i] = RegisterClass_FirstRegister(rc)+i;
    }

    //initialize remaing struct values
    reserved->cReserved = cReserved;
  }

  //reserve registers for addressability. at this time we only reserve
  //one register for addressing, which is the frame pointer. stores
  //and loads are generated using an immediate offset from the frame
  //pointer. we adjust the reserved registers for register class zero
  //(integers) to account for this. we save r0 for the frame pointer
  //so we bump the remaining regs by one. actually I think this is a
  //little silly and most machines would have a special register
  //already reserved for this purpose.
  ReservedRegsInfo* reservedIntRegs = &(mRc_ReservedRegs[0]);
  for(LOOPVAR i = 0; i < cReserved; i++)
  {
    reservedIntRegs->regs[i]++;
  }  
  reservedIntRegs->cReserved++;
  mRc_CReg[0]--;

  //allocate temporary vector sets sized to the number of available
  //registers for a given class. used in various live range
  //calculations
  mRc_VsTmp = (VectorSet*)
    Arena_GetMemClear(arena, sizeof(VectorSet) * cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRc_VsTmp[rc] = VectorSet_Create(arena, mRc_CReg[rc]);
  }

  //allocate register_contents structs per register class. these are
  //used during register assignment to find temporary registers when
  //we need to evict a register if we don't have enough reserved
  //registers to satisfy the needs of the instruction

  //RegisterContents** mRc_pRegisterContents = NULL;
  mRc_RegisterContents = (RegisterContents*)
    Arena_GetMemClear(arena, sizeof(RegisterContents) * cRegisterClass);
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRc_RegisterContents[rc].evicted = 
      new std::list< std::pair<LRID,Register> >;
    mRc_RegisterContents[rc].all= new std::vector<ReservedReg*>(); 
    mRc_RegisterContents[rc].regMap = new std::map<LRID,ReservedReg*>; 
    mRc_RegisterContents[rc].reserved= new std::vector<ReservedReg*>;

    //TODO: need to add the correct registers to the all and reserved list
    //first add the reserved registers
    //actual reserved must be calculated in this way since we reserve
    //an extra register for the frame pointer, but it is not available
    //to be used as a temporary register so we must remember this when
    //building the reserved regs list
    ReservedRegsInfo rri = mRc_ReservedRegs[rc];
    Unsigned_Int cActualReserved = 
      (rc == RegisterClass_INT ?  rri.cReserved-1 : rri.cReserved);
    for(LOOPVAR i = 0; i < cActualReserved; i++)
    {
      ReservedReg* rr = (ReservedReg*)
        Arena_GetMemClear(arena, sizeof(ReservedReg));
      
      rr->machineReg = rri.regs[i];
      rr->free = TRUE; 
      mRc_RegisterContents[rc].reserved->push_back(rr);
    }

    //now build the list of all remaining machine registers for this
    //register class.
    Register base = RegisterClass_FirstRegister(rc) + rri.cReserved;
    for(LOOPVAR i = 0; i < mRc_CReg[rc]; i++)
    {
      ReservedReg* rr = (ReservedReg*)
        Arena_GetMemClear(arena, sizeof(ReservedReg));
      
      rr->machineReg = base+i;
      rr->free = TRUE; 
      mRc_RegisterContents[rc].all->push_back(rr);
    }
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
void RegisterClass_CreateLiveRangeTypeMap(Arena arena, Unsigned_Int lr_count)
                             
{
  using Mapping::SSAName2LRID;
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
    debug("LRID: %3d ==> Type: %d", SSAName2LRID(i), def_type);
    mLrId_DefType[SSAName2LRID(i)] = def_type;
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

/*
 *========================
 * RegisterClass_FirstRegister()
 *========================
 * Returns the first machine register used for a given register class.
 * The remaining registers should be sequential starting from this
 * base regiser.
 */
Unsigned_Int RegisterClass_FirstRegister(RegisterClass rc)
{
  return rc*REGCLASS_SPACE;
}

Register RegisterClass_MachineRegForColor(RegisterClass rc, Color c)
{
  //mRcColor_Reg[rc][c];
  //compute machine register as class base register number + color +
  //number of reserved registers for that class
  Register r = (RegisterClass_FirstRegister(rc))
               + (c) 
               + (mRc_ReservedRegs[rc].cReserved);
  debug("MACHINE REG(rc,c):(%d,%d) = %d",rc,c,r);
  return  r;
}

VectorSet RegisterClass_TmpVectorSet(RegisterClass rc)
{
  return mRc_VsTmp[rc];
}


RegisterContents*
RegisterClass_GetRegisterContentsForLRID(LRID lrid)
{
  RegisterClass rc = RegisterClass_InitialRegisterClassForLRID(lrid);
  return &mRc_RegisterContents[rc];
}

RegisterContents*
RegisterClass_GetRegisterContentsForRegisterClass(RegisterClass rc)
{
  return &mRc_RegisterContents[rc];
}

