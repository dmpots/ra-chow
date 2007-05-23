/*====================================================================
 * rc.c (RegisterClass)
 *====================================================================
 * Holds functions and definitions used for implementing multiple
 * register classes in the chow register allocator
 ********************************************************************/

/*--------------------------INCLUDES---------------------------*/
#include <stdlib.h>
#include <vector>
#include <map>
#include <list>
#include <utility>

#include <SSA.h>
#include "rc.h"
#include "assign.h"
#include "mapping.h"
#include "params.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
/* local functions */

/* local variables */
unsigned int* mRc_CReg;
Boolean fEnableMultipleClasses = FALSE;
VectorSet* mRc_VsTmp; 
RegisterClass::ReservedRegsInfo* mRc_ReservedRegs;

/* constants */
//update this if adding more classes
const Unsigned_Int NUM_REG_CLASSES = 2; 
//the number of registers required by each type, for example a double
//requires two float registers.
int mDefType_RegWidth[] = {
  0, /* NO_DEFS */
  1, /* INT_DEF */ 
  1, /* FLOAT_DEF */
  2, /* DOUBLE_DEF */
  2, /* COMPLEX_DEF */ 
  2, /* DCOMPLEX_DEF */ 
  0  /* MULT_DEFS */
};

//space between sequential allocation of registers per class.
//each register class will be allocated registers starting from
//rc*REGCLASS_SPACE 
const Unsigned_Int REGCLASS_SPACE = 100;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/

namespace RegisterClass {
std::vector<RC> all_classes;

const ReservedRegsInfo& GetReservedRegInfo(RC rc)
{
  return mRc_ReservedRegs[rc];
}

/*
 *========================
 * RegisterClass::Init()
 *========================
 */
void Init(Arena arena, 
          Unsigned_Int cReg, 
          Boolean fEnableClasses,
          Unsigned_Int cReserved)
{
  unsigned int cRegisterClass = 1;
  fEnableMultipleClasses = fEnableClasses;
  if(fEnableMultipleClasses)
  {
    cRegisterClass = NUM_REG_CLASSES;
  }
  debug("using %d register classes", cRegisterClass);
  //populate the all_classes vector with the register classes used
  for(unsigned int i = 0; i < cRegisterClass; i++)
    all_classes.push_back(RC(i));


  //for now we have all register classes use the same number of
  //registers 
  mRc_CReg = (Unsigned_Int*)
    Arena_GetMemClear(arena, sizeof(Unsigned_Int)* cRegisterClass);
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    mRc_CReg[all_classes[i]] = cReg - cReserved;
  }

  //allocate an array of reserved register structs. these are used
  //during register assignment when we need registers for holding
  //spilled values
  mRc_ReservedRegs = (ReservedRegsInfo*)
    Arena_GetMemClear(arena, sizeof(ReservedRegsInfo) * cRegisterClass);
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    RC rc = all_classes[i];

    //allocate and initialize the array of registers for this class
    ReservedRegsInfo* reserved = &(mRc_ReservedRegs[rc]);
    reserved->regs = (Register*)
      Arena_GetMemClear(arena, sizeof(Register) * cReserved);
    for(LOOPVAR i = 0; i < cReserved; i++)
    {
      reserved->regs[i] = FirstRegister(rc)+i;
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
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    RC rc  = all_classes[i];
    mRc_VsTmp[rc] = VectorSet_Create(arena, mRc_CReg[rc]);
  }

  if(Params::Machine::double_takes_two_regs)
  {
    mDefType_RegWidth[DOUBLE_DEF] = 2;
  }
  else
  {
    mDefType_RegWidth[DOUBLE_DEF] = 1;
  }
}

/*
 *========================
 * RegisterClass::InitialRegisterClassForLRID
 *========================
 * An initial mapping of LRIDs to register classes. This funciton is
 * what determines which class a LRID will belong to. If you want to
 * change the number of register classes used then mess around with
 * this function.
 */
RC InitialRegisterClassForLRID(LRID lrid)
{
  //quick check to see if we are using multiple classes
  if(!fEnableMultipleClasses)
  {
    return INT;
  }

  //for now just switch on type
  RC rc;
  Def_Type def_type = Mapping::LiveRangeDefType(lrid);
  switch(def_type)
  {
    case INT_DEF: 
      rc = INT;
      break;
    case FLOAT_DEF:
    case DOUBLE_DEF:
      rc = FLOAT;
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
 * RegisterClass::NumMachineReg()
 *========================
 * Returns the number of machine registers available for a given
 * register class
 */
Unsigned_Int NumMachineReg(RC rc)
{
  return mRc_CReg[rc];
}

/*
 *====================================
 * RegisterClass::MachineRegForColor()
 *====================================
 * Returns the number of machine registers available for a given
 * register class
 */
Register MachineRegForColor(RC rc, Color c)
{
  //mRcColor_Reg[rc][c];
  //compute machine register as class base register number + color +
  //number of reserved registers for that class
  Register r = (FirstRegister(rc))
               + (c) 
               + (mRc_ReservedRegs[rc].cReserved);
  debug("MACHINE REG(rc,c):(%d,%d) = %d",rc,c,r);
  return  r;
}

/*
 *====================================
 * RegisterClass::ColorForMachineReg()
 *====================================
 * Returns the number of machine registers available for a given
 * register class
 */
Color ColorForMachineReg(RC rc, Register r)
{
  Color c = r - (FirstRegister(rc)) - (mRc_ReservedRegs[rc].cReserved);
  debug("Color (rc,r):(%d,%d) = %d",rc,r,c);
  return  c;
}

VectorSet TmpVectorSet(RC rc)
{
  return mRc_VsTmp[rc];
}

/*
 *========================
 * FirstRegister()
 *========================
 * Returns the first machine register used for a given register class.
 * The remaining registers should be sequential starting from this
 * base regiser.
 */
Unsigned_Int FirstRegister(RegisterClass::RC rc)
{
  return rc*REGCLASS_SPACE;
}

/*
 *========================
 * RegWidth()
 *========================
 * Returns the "width" in registers for the given type, where width is
 * the number of registers that type requires. For example, a type of
 * Double requires two registers from the floating point class.
 */
int RegWidth(Def_Type dt)
{
  return mDefType_RegWidth[dt];
}

}//end RegisterClass namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/
namespace {

}//end anonymous namespace
