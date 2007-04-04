#ifndef __GUARD_ASSIGN_H
#define __GUARD_ASSIGN_H

#include <Shared.h>
#include <list>
#include <map>
#include <vector>

#include "types.h"
#include "debug.h"

namespace Assign {
  /* constants */
  extern const Register REG_UNALLOCATED;

  /* exported functions */
  void Init(Arena);
  Register GetMachineRegAssignment(Block* b, LRID lrid);
  void EnsureReg(Register* reg, 
                 LRID lrid, 
                 Block* blk,
                 Inst*  origInst, 
                 Inst** updatedInst, 
                 Operation* op,
                 RegPurpose purpose,
                 const RegisterList& instUses,
                 const RegisterList& instDefs);
  void ResetFreeTmpRegs(Inst* last_inst);
}
#endif
