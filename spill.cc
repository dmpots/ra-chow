
#include <map>

#include "spill.h"
#include "live_range.h"
#include "mapping.h"
#include "cfg_tools.h"

namespace {
  //local constants//
  const MemoryLocation MEM_UNASSIGNED = 0; 
  const MemoryLocation TAG_UNASSIGNED = 0; 

  //local variables//
  std::map<LRID, MemoryLocation> lr_mem_map;
  std::map<LRID, Expr> lr_tag_map;
  Arena spill_arena;

  //local functions//
  Operation* get_frame_operation();
  MemoryLocation Frame_GetStackSize(Operation* frame_op);
  void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp);
  void Frame_SetRegFP(Operation* frame_op, Register reg);
  Variable Frame_GetRegFP(Operation* frame_op);
  MemoryLocation ReserveStackSpace(unsigned int size);
  Inst* Inst_CreateLoad(Opcode_Names opcode,
                             Expr tag, 
                             Unsigned_Int alignment, 
                             Comment_Val comment,
                             Unsigned_Int offset,
                             Register base_reg,
                             Register dest_reg);
  Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val);

  Inst* Inst_CreateCopy(Opcode_Names opcode,
                             Comment_Val comment,
                             Register src,
                             Register dest);
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
void Init(Arena arena)
{
  //get and keep a reference to the frame operation
  frame.op = get_frame_operation();
  frame.ssa_name = Frame_GetRegFP(frame.op);

  //initialize the stack pointer so that we have a correct value when
  //we need to insert loads and stores
  frame.stack_pointer = Frame_GetStackSize(frame.op);
  frame.lrid = Mapping::SSAName2LRID(frame.ssa_name);

  //keep arena for allocating new instructions
  spill_arena = arena;
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

/*
 *===================
 * InsertLoad()
 *===================
 * Inserts a load instruction for the given live range before the
 * passed instruction.
 */
void InsertLoad(LiveRange* lr, Inst* before_inst, Register dest, 
                                               Register base)
{
  //LiveRange* lr = Chow::live_ranges[lrid];
  Expr tag = Spill::SpillTag(lr);

  Opcode_Names opcode = lr->LoadOpcode();
  Unsigned_Int alignment = lr->Alignment();
  Unsigned_Int offset = Spill::SpillLocation(lr);
  //assert(offset != MEM_UNASSIGNED); //can happen with use b4 def
  debug("Inserting load for LR: %d, to reg: %d, from offset: %d,"
         "base: %d", lrid, dest, offset, base);

  //generate a comment
  char str[64];
  sprintf(str, "LOAD %d_%d", lr->orig_lrid, lr->id); 
  Comment_Val comment = Comment_Install(str);

  Inst* ld_inst = 
    Inst_CreateLoad(opcode, tag, alignment, comment, offset, base, dest);

  //finally insert the new instruction
  Block_Insert_Instruction(ld_inst, before_inst);

}


/*
 *===================
 * InsertStore()
 *===================
 * Inserts a store instruction for the given live range around the
 * passed instruction based on the loc paramerter (before or after).
 *
 * returns the instruction inserted
 */
Inst* InsertStore(LiveRange* lr, Inst* around_inst, Register src,
                   Register base, InstInsertLocation loc)
{
  //LiveRange* lr = Chow::live_ranges[lrid];
  Expr tag = Spill::SpillTag(lr);

  MemoryLocation offset = Spill::SpillLocation(lr);
  debug("Inserting store for LR: %d, from reg: %d to offset: %d",
         lrid, src, offset);

  //generate a comment
  char str[64]; 
  sprintf(str, "STORE %d_%d", lr->orig_lrid, lr->id); 
  Comment_Val comment = Comment_Install(str);

  //get opcode and alignment for live range
  Opcode_Names opcode = lr->StoreOpcode();
  Unsigned_Int alignment = lr->Alignment();

  //create a new store instruction
  Inst* st_inst = 
    Inst_CreateStore(opcode, tag, alignment, comment, offset, base, src);

  //finally insert the new instruction
  if(loc == AFTER_INST)
    Block_Insert_Instruction(st_inst, around_inst->next_inst);
  else
    Block_Insert_Instruction(st_inst, around_inst);

  return st_inst;
}

/*
 *===================
 * InsertCopy()
 *===================
 * Inserts a copy from one live range to another, using the registers
 * passed to the function.
 */
void InsertCopy(const LiveRange* lrSrc, const LiveRange* lrDest,
                 Inst* around_inst, Register src, Register dest, 
                 InstInsertLocation loc)
{
  //generate a comment
  char str[64]; char lrname[32]; char lrname2[32]; 
  LRName(lrSrc, lrname); LRName(lrDest, lrname2);
  sprintf(str, "RR COPY for %s --> %s", lrname, lrname2);
  Comment_Val comment = Comment_Install(str);

  //get opcode and alignment for live range
  Opcode_Names opcode = lrSrc->CopyOpcode();

  //create a new store instruction
  Inst* cp_inst = Inst_CreateCopy(opcode, comment, src, dest);

  //insert the new instruction
  if(loc == AFTER_INST)
    InsertInstAfter(cp_inst, around_inst);
  else
    InsertInstBefore(cp_inst, around_inst);
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

/*
 *===================
 * Inst_CreateLoad()
 *===================
 * Creates a new load Inst based on the passed parameters
 * load instruction format:
 *  iLDor  @ref align offset base => reg
 */
Inst* Inst_CreateLoad(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register dest_reg)
{
  //allocate a new instruction
  const int LD_OPSIZE = 7;
  Inst* ld_inst = (Inst*)Inst_Allocate(spill_arena, 1);
  Operation* ld_op = (Operation*)Operation_Allocate(spill_arena, 
                                                    LD_OPSIZE);
  ld_inst->operations[0] = ld_op;
  ld_inst->operations[1] = NULL;

  //fill in struct
  ld_op->opcode  = opcode;
  ld_op->comment = comment;
  ld_op->source_line_ref = Expr_Install_String("0");
  ld_op->constants = 3;
  ld_op->referenced = 4;
  ld_op->defined = 5;
  ld_op->critical = TRUE;

  //fill in arguments array
  ld_op->arguments[0] = tag;
  ld_op->arguments[1] = Expr_Install_Int(alignment);
  ld_op->arguments[2] = Expr_Install_Int(offset);
  ld_op->arguments[3] = base_reg;
  ld_op->arguments[4] = dest_reg;
  ld_op->arguments[5] = 0;
  ld_op->arguments[6] = 0;

  return ld_inst;
}

/*
 *===================
 * Inst_CreateStore()
 *===================
 * Creates a new store Inst based on the passed parameters
 * store instruction format:
 *  iSSTor @ref align offset base val
 */
Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val)
{
  //allocate a new instruction
  const int ST_OPSIZE = 7;
  Inst* st_inst = (Inst*)Inst_Allocate(spill_arena, 1);
  Operation* st_op = (Operation*)Operation_Allocate(spill_arena, 
                                                    ST_OPSIZE);
  st_inst->operations[0] = st_op;
  st_inst->operations[1] = NULL;

  //fill in struct
  st_op->opcode  = opcode;
  st_op->comment = comment;
  st_op->source_line_ref = Expr_Install_String("0");
  st_op->constants = 3;
  st_op->referenced = 5;
  st_op->defined = 5;
  st_op->critical = TRUE;

  //fill in arguments array
  st_op->arguments[0] = tag;
  st_op->arguments[1] = Expr_Install_Int(alignment);
  st_op->arguments[2] = Expr_Install_Int(offset);
  st_op->arguments[3] = base_reg;
  st_op->arguments[4] = val;
  st_op->arguments[5] = 0;
  st_op->arguments[6] = 0;

  return st_inst;
}

/*
 *===================
 * Inst_CreateCopy()
 *===================
 * Creates a new copy instruction based on the passed paramerters
 * store instruction format:
 * i2i src => dest
 */
Inst* Inst_CreateCopy(Opcode_Names opcode,
                             Comment_Val comment,
                             Register src,
                             Register dest)
{
  const int CP_OPSIZE = 7;
  //allocate a new instruction
  Inst* cp_inst = (Inst*)Inst_Allocate(spill_arena, 1);
  Operation* cp_op = (Operation*)Operation_Allocate(spill_arena, 
                                                    CP_OPSIZE);
  cp_inst->operations[0] = cp_op;
  cp_inst->operations[1] = NULL;

  //fill in struct
  cp_op->opcode  = opcode;
  cp_op->comment = comment;
  cp_op->source_line_ref = Expr_Install_String("0");
  cp_op->constants = 0;
  cp_op->referenced = 1;
  cp_op->defined = 2;
  cp_op->critical = TRUE;

  //fill in arguments array
  cp_op->arguments[0] = src;
  cp_op->arguments[1] = dest;
  cp_op->arguments[2] = 0;
  cp_op->arguments[3] = 0;
  cp_op->arguments[4] = 0;
  cp_op->arguments[5] = 0;
  cp_op->arguments[6] = 0;

  return cp_inst;
}




}//end anonymous namespace 

