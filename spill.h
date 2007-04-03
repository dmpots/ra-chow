/* module to handle details of spilling live ranges */

#ifndef __GUARD_SPILL_H
#define __GUARD_SPILL_H

#include <Shared.h>
#include "types.h"


/* forward def */
struct LiveRange;

namespace Spill {
/*types*/
struct Frame {
  Operation* op;
  MemoryLocation stack_pointer;
  Variable ssa_name;
  LRID lrid;
};

/*variables*/
extern Frame frame;
extern const Register REG_FP;

/*functions*/
MemoryLocation SpillLocation(const LiveRange* lr);
void Init(Arena);
Expr SpillTag(const LiveRange* lr);
void RewriteFrameOp();
Inst* InsertStore(LiveRange*,Inst*,Register,Register,InstInsertLocation);
void InsertLoad(LiveRange*, Inst*, Register, Register);

}

#endif
