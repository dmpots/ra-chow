
#include <map>

#include "spill.h"
#include "mapping.h"
#include "live_range.h"

namespace {
  //local constants//
  const MemoryLocation MEM_UNASSIGNED = 0; 
  const MemoryLocation TAG_UNASSIGNED = 0; 

  //local variables//
  std::map<LRID, MemoryLocation> lr_mem_map;
  std::map<LRID, Expr> lr_tag_map;

  //local functions//
  Operation* get_frame_operation();
  MemoryLocation Frame_GetStackSize(Operation* frame_op);
  void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp);
  void Frame_SetRegFP(Operation* frame_op, Register reg);
  Variable Frame_GetRegFP(Operation* frame_op);
  MemoryLocation ReserveStackSpace(unsigned int size);
}

/*--------------------MODULE IMPLEMENTATION---------------------*/
namespace Spill {
/* module constants */
const Register REG_FP = 555;

/* module variables */
Frame frame;


/* module functions */
/*
 *========================
 * Init()
 *========================
 * Initialize module so that it can be used to generate spill code
 */
void Init()
{
  //get and keep a reference to the frame operation
  frame.op = get_frame_operation();
  frame.ssa_name = Frame_GetRegFP(frame.op);

  //initialize the stack pointer so that we have a correct value when
  //we need to insert loads and stores
  frame.stack_pointer = Frame_GetStackSize(frame.op);
  frame.lrid = Mapping::SSAName2LRID(frame.ssa_name);
}

/*
 *========================
 * SpillLocation()
 *========================
 * Returns the memory location to be used by the live range
 */
MemoryLocation SpillLocation(const LiveRange* lr)
{
  if(lr_mem_map[lr->orig_lrid] == MEM_UNASSIGNED)
  {
    lr_mem_map[lr->orig_lrid] = ReserveStackSpace(lr->Alignment());
  }

  return lr_mem_map[lr->orig_lrid];
}

/*
 *========================
 * SpillTag()
 *========================
 * Returns the memory location to be used by the live range
 */
Expr SpillTag(const LiveRange* lr)
{
  if(lr_tag_map[lr->orig_lrid] == TAG_UNASSIGNED)
  {
    char str[32];
    sprintf(str, "@SPILL_%d(%d)",lr->orig_lrid, SpillLocation(lr));
    lr_tag_map[lr->orig_lrid] = Expr_Install_String(str);
  }

  return lr_tag_map[lr->orig_lrid];
}

/*
 *========================
 * RewriteFrameOp()
 *========================
 * rewrite the frame statement to have the first register be
 * the frame pointer and to adjust the stack size
 */
void RewriteFrameOp()
{
  Frame_SetStackSize(frame.op, frame.stack_pointer);
  Frame_SetRegFP(frame.op, REG_FP);
}

}//end Spill namespace 

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/
namespace {
/*
 *=====================
 * ReserveStackSpace()
 *=====================
 * Reserves space on the call stack and returns a pointer to the
 * beginning offset of the reserved space
 **/
MemoryLocation ReserveStackSpace(Unsigned_Int size)
{
  MemoryLocation sp = Spill::frame.stack_pointer;
  Spill::frame.stack_pointer += size;
  return sp;
}

/*
 *============================
 * Frame_GetStackSize()
 *=============================
 * Gets the stack size on the frame operation
 **/
MemoryLocation Frame_GetStackSize(Operation* frame_op)
{
  return Expr_Get_Integer((frame_op)->arguments[0]);
}

/*
 *============================
 * Frame_SetStackSize()
 *=============================
 * Sets the stack size on the frame operation
 **/
void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp)
{
  frame_op->arguments[0] = Expr_Install_Int(sp);
}

/*
 *============================
 * Frame_SetRegFP()
 *=============================
 * Sets the frame pointer register to use the global constant
 **/
void Frame_SetRegFP(Operation* frame_op, Register reg)
{
  frame_op->arguments[(frame_op)->referenced] = reg;
}

/*
 *============================
 * Frame_GetRegFP()
 *=============================
 * Sets the frame pointer register to use the global constant
 **/
Variable Frame_GetRegFP(Operation* frame_op)
{
  return frame_op->arguments[(frame_op)->referenced];
}


/*
 *==========================
 * get_frame_operation()
 *==========================
 * Gets the operation corresponding to the FRAME statement in the iloc
 * code
 **/
Operation* get_frame_operation()
{
  //cache the frame operation so we only have to do the lookup once
  static Operation* frame_op = NULL;
  if(frame_op) return frame_op;

  Block* b;
  Inst* inst;
  Operation** op;
  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {

      Inst_ForAllOperations(op, inst)
      {
        //grab some info from the frame instruction
        if((*op)->opcode == FRAME)
        {
          frame_op = *op;
          return frame_op;
        }
      }
    }
  }

  assert(FALSE); //should not get here
  return NULL;
}

}//end anonymous namespace 

