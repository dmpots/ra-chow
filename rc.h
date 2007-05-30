#ifndef __GUARD_RC_H
#define __GUARD_RC_H

#include <Shared.h>
#include "types.h"

namespace RegisterClass {
  /* types */
  enum RC {INT, FLOAT};
  //this struct holds information about the registers reserved for a
  //given class. these reserved registers are used during register
  //assignment to provide machine registers to any variable that was
  //spilled
  struct ReservedRegsInfo
  {
    Register* regs;         /* array of temporary registers */
    Unsigned_Int cReserved; /* count of number reserved */
    /* count of number hidden at the front, for all but INT reg  class
     * this is the same as reserved, but ints need an extra hidden reg
     * for the frame pointer */
    Unsigned_Int cHidden;

  }; 

  /* variables */
  extern std::vector<RC> all_classes;

  /* functions */
  const ReservedRegsInfo& GetReservedRegInfo(RC rc);

  void Init(Arena arena, 
            Unsigned_Int cReg,
            Boolean fEnableClasses,
            Unsigned_Int* cReserved);
  void InitRegWidths();
  RC InitialRegisterClassForLRID(LRID lrid);
  unsigned int NumMachineReg(RC);
  Register MachineRegForColor(RC, Color);
  VectorSet TmpVectorSet(RC rc);
  Register FirstRegister(RegisterClass::RC);
  Color ColorForMachineReg(RC rc, Register r);
  int RegWidth(Def_Type dt);
}

#endif
