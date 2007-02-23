/*====================================================================
 * 
 *====================================================================
 * $Id: chow.c 157 2006-07-26 19:19:34Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/chow.c $
 ********************************************************************/

#include <Shared.h>
#include <SSA.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
#include <set>
#include <vector>
#include <stack>
#include <map>
#include <algorithm>
#include <functional>
#include <utility>

#include "chow.h"
#include "chow_params.h"
#include "live.h"
#include "live_range.h"
#include "union_find.h"
#include "reach.h"
#include "debug.h"
#include "rc.h" //RegisterClass definitions 
#include "assign.h" //Handles some aspects of register assignment
#include "cfg_tools.h" 


/* types */
//using namespace std;
typedef unsigned int LOOPVAR;

/* globals */
LRList live_ranges;
Arena  chow_arena;
BB_Stats bb_stats;
Unsigned_Int** mBlkIdSSAName_Color;
Variable GBL_fp_origname;
Unsigned_Int* depths; //loop nesting depth
Chow_Stats chowstats = {0};

/* allocation parameters */
Boolean      PARAM_MoveLoadsAndStores;
bool         PARAM_EnhancedCodeMotion;
float        PARAM_LoopDepthWeight;
unsigned int PARAM_BBMaxInsts;
unsigned int PARAM_NumMachineRegs;
Boolean      PARAM_EnableRegisterClasses;
float        PARAM_MVCost = 1.0;
float        PARAM_LDSave = 1.0;
float        PARAM_STRSave = 1.0;
unsigned int PARAM_NumReservedRegs = 2;

/* allocation debugging */
unsigned int DEBUG_DotDumpLR = 0; //use to dump a lr and its splits
std::vector<unsigned int> DEBUG_WatchLRIDs; //lrids that were dotdumped

/* locals */
static LRID* lr_name_map;
static MemoryLocation stack_pointer = 0;
static LRID fp_lrid;
static Unsigned_Int clrInitial = 0; //count of initial live ranges

/* local functions */
static BB_Stats Compute_BBStats(Arena, Unsigned_Int);
static void CreateLiveRangeNameMap(Arena);
static void assert_same_orig_name(LRID,Variable,VectorSet,Block* b);
static void AllocChowMemory();
static Operation* get_frame_operation();
static MemoryLocation Frame_GetStackSize(Operation* frame_op);
static void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp);
static void Frame_SetRegFP(Operation* frame_op);
static Variable Frame_GetRegFP(Operation* frame_op);
static Inst* Inst_CreateLoad(Opcode_Names opcode,
                             Expr tag, 
                             Unsigned_Int alignment, 
                             Comment_Val comment,
                             Unsigned_Int offset,
                             Register base_reg,
                             Register dest_reg);
static Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val);
static void InitChow();
static UFSet* Find_Set(Variable v);
static void ConvertLiveInNamespaceSSAToLiveRange();
static void MoveLoadsAndStores();

//copy operations
namespace {
  struct CopyDescription
  {
    LiveRange* src_lr;
    LiveRange* dest_lr;
    Register   src_reg;
    Register   dest_reg;
  };

  typedef std::list<CopyDescription> CDL;
  typedef 
    std::list<std::pair<MovedSpillDescription,MovedSpillDescription> >
    CopyList ; 

  bool OrderCopies(const CopyList&, CDL* ordered_copies);
  Inst* Inst_CreateCopy(Opcode_Names opcode,
                             Comment_Val comment,
                             Register src,
                             Register dest);
  void Insert_Copy(const LiveRange* lrSrc, const LiveRange* lrDest,
                 Inst* around_inst, Register src, Register dest, 
                 InstInsertLocation loc);
}

/*
 *=======================
 * RunChow()
 *=======================
 *
 * Does the actual register allocation
 *
 ***/
void RunChow()
{
  LiveRange* lr;
  LRSet constr_lrs;
  LRSet unconstr_lrs;


  //allocate any memory needed by the chow allocator
  InitChow();

      
  //separate unconstrained live ranges
  LRList_ForAllCandidates(&live_ranges, lr)
  {
    //lr = live_ranges[i];
    if(LiveRange_Constrained(lr))
    {
      LRSet_Add(&constr_lrs, lr);
      debug("Constrained LR:    %d", lr->id);
    }
    else
    {
      LRSet_Add(&unconstr_lrs, lr);
      debug("UN-Constrained LR: %d", lr->id);
    }
  }


  //assign registers to constrained live ranges
  lr = NULL;
  while(!(constr_lrs.empty()))
  {
    //steps 2: (a) - (c)
    lr = ComputePriorityAndChooseTop(&constr_lrs);
    if(lr == NULL)
    {
      debug("No more constrained lrs can be assigned");
      break;
    }
    LiveRange_AssignColor(lr);
    debug("LR: %d is top priority, given color: %d", lr->id, lr->color);
    LiveRange_SplitNeighbors(lr, &constr_lrs, &unconstr_lrs);
  }

  //assign registers to unconstrained live ranges
  debug("assigning unconstrained live ranges colors");
  //while(!LRList_Empty(unconstr_lrs))
  //some may no longe be candidates now
  LRSet_ForAllCandidates(&unconstr_lrs, lr)
  {
    //lr = LRList_Pop(&unconstr_lrs);
    debug("choose color for unconstrained LR: %d", lr->id);
    LiveRange_AssignColor(lr);
    debug("LR: %d is given color:%d", lr->id, lr->color);
  }

  //record some statistics about the allocation
  chowstats.clrFinal = live_ranges.size();
}

void AllocChowMemory()
{
  //allocate a mapping of block x SSA_name -> register
  mBlkIdSSAName_Color = (Unsigned_Int**)
    Arena_GetMemClear(chow_arena, 
                      sizeof(Unsigned_Int*) * (1+block_count));
  LOOPVAR i;
  for(i = 0; i < block_count+1; i++)
  {
    mBlkIdSSAName_Color[i] = (Unsigned_Int*)
      Arena_GetMemClear(chow_arena, 
                        sizeof(Unsigned_Int) * liverange_count );
      LOOPVAR j;
      for(j = 0; j < liverange_count; j++)
        mBlkIdSSAName_Color[i][j] = NO_COLOR;
  }

}

/*
 *=============================
 * InitChow()
 *=============================
 * Initialize global variables used during allocation
 */
void InitChow()
{
  AllocChowMemory();


  //the register that holds the frame pointer is not a candidate for
  //allocation since it resides in a special reserved register. remove
  //this from the interference graph and remember which lrid holds the
  //frame pointer
  Operation* frame_op = get_frame_operation();
  fp_lrid = SSAName2LRID(frame_op->arguments[frame_op->referenced]);
  LiveRange_MarkNonCandidateAndDelete(live_ranges[fp_lrid]);
  //LRList_Remove(&live_ranges, live_ranges[fp_lrid]);


  //initialize the stack pointer so that we have a correct value when
  //we need to insert loads and stores
  stack_pointer = Frame_GetStackSize(frame_op);
  GBL_fp_origname = Frame_GetRegFP(frame_op);

  //clear out edge extensions if needed
  if(PARAM_MoveLoadsAndStores)
  {
    Block* b;
    Edge* e;
    ForAllBlocks(b)
    {
      Block_ForAllPreds(e,b) e->edge_extension = NULL;
      Block_ForAllSuccs(e,b) e->edge_extension = NULL;
    }
  }
}

/*
 *=============================
 * LiveRange_BuildInitialSSA()
 *=============================
 * Each live range starts out where no definition reaches a use
 * outside the live range. This means that no stores will be necessary
 * at first. When we allocate a register it will stay across the
 * entire live range.
 *
 * If a successor block needs a load then we must generate a store
 * 
 * If the def reaches a use outside the live range we must generate a
 * store.
 * 
 * 
 */
void LiveRange_BuildInitialSSA()
{
  //build ssa
  Unsigned_Int ssa_options = 0;
  ssa_options |= SSA_PRUNED;
  ssa_options |= SSA_BUILD_DEF_USE_CHAINS;
  ssa_options |= SSA_BUILD_USE_DEF_CHAINS;
  ssa_options |= SSA_CONSERVE_LIVE_IN_INFO;
  ssa_options |= SSA_CONSERVE_LIVE_OUT_INFO;
  ssa_options |= SSA_IGNORE_TAGS;
  SSA_Build(ssa_options);
  //Dump();

  Arena uf_arena = Arena_Create();
  UFSets_Init(uf_arena, SSA_def_count);

  //visit each phi node and union the parameters and destination
  //register together since they should all be part of the same live
  //range
  Block* b;
  Phi_Node* phi;
  UFSet* set;
  Unsigned_Int* v;
  ForAllBlocks(b)
  {
    Block_ForAllPhiNodes(phi, b)
    {
      debug("process phi: %d at %s (%d)", phi->new_name, bname(b), id(b));
      set = Find_Set(phi->new_name);
      Phi_Node_ForAllParms(v, phi)
      {
        if(*v != 0)
          set = UFSet_Union(set, Find_Set(*v));
      }
    }
  }
  clrInitial = uf_set_count - 1; //no lr for SSA name 0
  debug("SSA NAMES: %d", SSA_def_count);
  debug("UNIQUE LRs: %d", clrInitial);
  chowstats.clrInitial = clrInitial;

  //create a mapping from ssa names to live range ids
  CreateLiveRangeNameMap(uf_arena);
  ConvertLiveInNamespaceSSAToLiveRange();
  if(PARAM_EnableRegisterClasses) //classes are by type
  {
    RegisterClass_CreateLiveRangeTypeMap(uf_arena,
                                       clrInitial,
                                       lr_name_map);
  }

  //now that we know how many live ranges we start with allocate them
  bb_stats = Compute_BBStats(uf_arena, SSA_def_count);
  LiveRange_AllocLiveRanges(chow_arena, live_ranges, clrInitial);

  //build the interference graph
  //find all live ranges that are referenced or live out in this
  //block. those are the live ranges that need to include this block.
  //each of those live ranges will interfere with every other live
  //range in the block. walk through the graph and examine each block
  //to build the live ranges
  LiveRange* lr;
  LRID lrid;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  LOOPVAR j;
  VectorSet lrset = VectorSet_Create(uf_arena, clrInitial);
  ForAllBlocks(b)
  {

    //we need to acccount for any variable that is referenced in this
    //block as it may not be in the live_out set, but should still be
    //included in the live range if it is referenced in this block. A
    //name may be in a live range under more than one original name.
    //for example when a name is defined in two branches of an if
    //statement it will be added to the live range by that definition,
    //but will also be live out in those blocks under a different name
    //(the name defined by the phi-node for those definitions). as
    //long as we get the last definition in the block we should be ok
    VectorSet_Clear(lrset);
    Block_ForAllInstsReverse(inst, b)
    {
      //go in reverse because we want the last def that we see to be
      //the orig_name for the live range, this must be so because we
      //use that name in the use-def chains to decide where to put a
      //store for the defs of a live range
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllDefs(reg, *op)
        {
          debug("DEF: %d", *reg);
          lrid = SSAName2LRID(*reg);
          //better not have two definitions in the same block for the
          //same live range
          assert_same_orig_name(lrid, *reg, lrset, b); 
          AddLiveUnitOnce(lrid, b, lrset, *reg);

          //set the type of the live range according to the type
          //defined by this operation
          live_ranges[lrid]->type = Operation_Def_Type(*op, *reg);
        }

        Operation_ForAllUses(reg, *op)
        {
          debug("USE: %d", *reg);
          lrid = SSAName2LRID(*reg);
          AddLiveUnitOnce(lrid, b, lrset, *reg);
        }
      } 
    }

    //Now add in the live_out set to the variables that include this
    //block in their live range
    Liveness_Info info;
    info = SSA_live_out[id(b)];
    for(j = 0; j < info.size; j++)
    {
      //debug("LIVE: %d is LRID: %d",info.names[j], SSAName2LRID(info.names[j]));
      //add block to each live range
      lrid = SSAName2LRID(info.names[j]);
      //VectorSet_Insert(lrset, lrid);
      AddLiveUnitOnce(lrid, b, lrset, info.names[j]);
    }

    //now that we have the full set of lrids that need to include this
    //block we can add the live units to the live ranges and update
    //the interference graph
    Unsigned_Int v, i;
    debug("LIVE SIZE: %d\n", VectorSet_Size(lrset));
    VectorSet_ForAll(v, lrset)
    {
      lr = live_ranges[v];

      //update the interference lists
      VectorSet_ForAll(i, lrset)
      {
        if(v == i) continue; //skip yourself
        //add interference if in the same class
        LiveRange* lrT = live_ranges[i];
        if(LiveRange_RegisterClass(lr) == LiveRange_RegisterClass(lrT))
        {
          //debug("%d conflicts with %d", v, i);
          LiveRange_AddInterference(lr, lrT);
        }
      }
    }
  }


  //compute where the loads and stores need to go in the live range
  LRList_ForAll(&live_ranges, lr)
    LiveRange_MarkLoadsAndStores(lr);
  LiveRange_DDumpAll(&live_ranges);
}

//make sure that if we have already added a live unit for this lrid
//that the original name matches this name. this is important for the
//rewriting step, but this check does not need to be made when adding
//from the live_out set since those names may be different but it is
//ok because they occur in a different block
void assert_same_orig_name(LRID lrid, Variable v, VectorSet set,
Block* b)
{
  if(VectorSet_Member(set,lrid))
  {
    LiveUnit* unit = LiveRange_LiveUnitForBlock(live_ranges[lrid],b);
    //debug("already present: %d, orig_name: %d new_orig: %d  block: %s (%d)", 
   //                       lrid, unit->orig_name, v, bname(b), id(b));
    assert(unit->orig_name == v);
  }
}

/*
 *========================
 * CreateLiveRangeNameMap
 *========================
 * Makes a mapping from SSA name to live range id and stores the
 * result in the global variable lr_name_map
 ***/
void CreateLiveRangeNameMap(Arena arena)
{
  lr_name_map = (LRID*)
        Arena_GetMemClear(arena,sizeof(LRID) * (SSA_def_count));
  Unsigned_Int idcnt = 0; 
  Unsigned_Int setid; //a setid may be 0
  LOOPVAR i;

  //initialize to known value
  for(i = 0; i < SSA_def_count; i++)
    lr_name_map[i] = NO_LRID;
  
  for(i = 1; i < SSA_def_count; i++)
  {
    setid = Find_Set(i)->id;
    //debug("name: %d setid: %d lrid: %d", i, setid, lr_name_map[setid]);
    if(lr_name_map[setid] != NO_LRID)
    {
      lr_name_map[i] = lr_name_map[setid]; //already seen this lr
    } 
    else
    {
      //assign next available lr id
      lr_name_map[i] = lr_name_map[setid] = idcnt++;
    }

    debug("SSAName: %4d ==> LRID: %4d", i, lr_name_map[i]);
  }
  assert(idcnt == (clrInitial)); //we start lrids at 0
}

/*
 *========================================
 * ConvertLiveInNamespaceSSAToLiveRange()
 *========================================
 * Changes live range name space to use live range ids rather than the
 * ssa namespace.
 *
 * NOTE: Make sure you call this after building initial live ranges
 * because the live units need to know which SSA name they contain and
 * that information is taken from the ssa liveness info.
 ***/
void ConvertLiveInNamespaceSSAToLiveRange() 
{

  Liveness_Info info;
  Block* blk;
  LOOPVAR j;
  LRID lrid;
  ForAllBlocks(blk)
  {
    //TODO: do we need to convert live out too?
    info = SSA_live_in[id(blk)];
    for(j = 0; j < info.size; j++)
    {
      Variable vLive = info.names[j];
      if(!(vLive < SSA_def_count))
      {
        error("invalid live in name %d", vLive);
        continue;
      }
      //debug("Converting LIVE: %d to LRID: %d",vLive,
      //       SSAName2LRID(vLive));
      lrid = SSAName2LRID(vLive);
      info.names[j] = lrid;
    }
  }
}


/*
 *===================
 * RenameRegisters()
 *===================
 * Renames the variables in the code to use the registers assigned by
 * coloring
 ***/
void RenameRegisters()
{
  debug("allocation complete. renaming registers...");

  //stack pointer is the initial size of the stack frame
  debug("STACK: %d", stack_pointer);

  Block* b;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  LRID lrid;
  std::vector<Register> instUses;
  std::vector<Register> instDefs;

  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {
      /* collect a list of uses and defs used in this regist that is
       * used in algorithms when deciding which registers can be
       * evicted for temporary uses */
      instUses.clear();
      instDefs.clear();
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          LRID lrid = SSAName2LRID(*reg);
          instUses.push_back(lrid);
        }

        Operation_ForAllDefs(reg, *op)
        {
          LRID lrid = SSAName2LRID(*reg);
          instDefs.push_back(lrid);
        }
      } 

      /* rename uses and then defs */
      //we need to keep the original inst separate. when we call
      //ensure_reg_assignment it may be the case that we insert some
      //stores after this instruction. we don't want to process those
      //store instructions in the register allocator so we have to
      //update the value of the inst pointer. thus we keep origInst
      //and updatedInst separate.
      Inst* origInst = inst;
      Inst** updatedInst = &inst;
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          *reg = GetMachineRegAssignment(b, lrid);
          //make sure the live range is in a register
          ensure_reg_assignment(reg, lrid, b, origInst, updatedInst,
                                *op, FOR_USE,
                                instUses, instDefs);
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          *reg = GetMachineRegAssignment(b, lrid); 
          //make sure the live range is in a register
          ensure_reg_assignment(reg, lrid, b, origInst, updatedInst,
                               *op, FOR_DEF,
                                instUses, instDefs);
        }

      } 
    }
    //make available the tmp regs used in this instruction
    //pass in a pointer to the last instruction in the block so
    //that we can insert loads for evicted registers
    reset_free_tmp_regs(b->inst->prev_inst); 
  }

  //if we are to optimize positions of loads and stores, do so after
  //assigning registers
  if(PARAM_MoveLoadsAndStores)
  {
    debug("moving loads and stores to \"optimal\" position");
    MoveLoadsAndStores();
  }

  //finally rewrite the frame statement to have the first register be
  //the frame pointer and to adjust the stack size
  Operation* frame_op = get_frame_operation();
  assert(frame_op->opcode == FRAME);
  Frame_SetStackSize(frame_op, stack_pointer);
  Frame_SetRegFP(frame_op);
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
void Frame_SetRegFP(Operation* frame_op)
{
  frame_op->arguments[(frame_op)->referenced] = REG_FP;
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
 *==========================
 * GetMachineRegAssignment()
 *==========================
 * Gets the machine register assignment for a lrid in a given block
 **/
Register GetMachineRegAssignment(Block* b, LRID lrid)
{

  if(lrid == fp_lrid)
    return REG_FP;

   /* return REG_UNALLOCATED; spill everything */

  Register color = mBlkIdSSAName_Color[id(b)][lrid];
  if(color == NO_COLOR)
    return REG_UNALLOCATED;

  RegisterClass rc = LiveRange_RegisterClass(live_ranges[lrid]);
  return RegisterClass_MachineRegForColor(rc, color);
}
/*
 *===================
 * Insert_Load()
 *===================
 * Inserts a load instruction for the given live range before the
 * passed instruction.
 */
void Insert_Load(LRID lrid, Inst* before_inst, Register dest, 
                                               Register base)
{
  LiveRange* lr = live_ranges[lrid];
  Expr tag = LiveRange_GetTag(lr);

  Opcode_Names opcode = LiveRange_LoadOpcode(lr);
  Unsigned_Int alignment = LiveRange_GetAlignment(lr);
  Unsigned_Int offset = LiveRange_MemLocation(lr);
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
 * Inst_CreateLoad()
 *===================
 * Creates a new load Inst based on the passed parameters
 * load instruction format:
 *  iLDor  @ref align offset base => reg
 */
static Inst* Inst_CreateLoad(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register dest_reg)
{
  //allocate a new instruction
  const int LD_OPSIZE = 7;
  Inst* ld_inst = (Inst*)Inst_Allocate(chow_arena, 1);
  Operation* ld_op = (Operation*)Operation_Allocate(chow_arena, 
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
 * Insert_Store()
 *===================
 * Inserts a store instruction for the given live range around the
 * passed instruction based on the loc paramerter (before or after).
 *
 * returns the instruction inserted
 */
Inst* Insert_Store(LRID lrid, Inst* around_inst, Register src,
                   Register base, InstInsertLocation loc)
{
  LiveRange* lr = live_ranges[lrid];
  Expr tag = LiveRange_GetTag(lr);

  MemoryLocation offset = LiveRange_MemLocation(lr);
  assert(offset != MEM_UNASSIGNED);
  debug("Inserting store for LR: %d, from reg: %d to offset: %d",
         lrid, src, offset);

  //generate a comment
  char str[64]; 
  sprintf(str, "STORE %d_%d", lr->orig_lrid, lr->id); 
  Comment_Val comment = Comment_Install(str);

  //get opcode and alignment for live range
  Opcode_Names opcode = LiveRange_StoreOpcode(lr);
  Unsigned_Int alignment = LiveRange_GetAlignment(lr);

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
 * Inst_CreateStore()
 *===================
 * Creates a new store Inst based on the passed parameters
 * store instruction format:
 *  iSSTor @ref align offset base val
 */
static Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Comment_Val comment,
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val)
{
  //allocate a new instruction
  const int ST_OPSIZE = 7;
  Inst* st_inst = (Inst*)Inst_Allocate(chow_arena, 1);
  Operation* st_op = (Operation*)Operation_Allocate(chow_arena, 
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
 *=====================
 * ReserveStackSpace()
 *=====================
 * Reserves space on the call stack and returns a pointer to the
 * beginning offset of the reserved space
 **/
MemoryLocation ReserveStackSpace(Unsigned_Int size)
{
  MemoryLocation sp = stack_pointer;
  stack_pointer += size;
  return sp;
}

/*
 *===================
 * Insert_Copy()
 *===================
 * Inserts a copy from one live range to another, using the registers
 * passed to the function.
 */
namespace {
void Insert_Copy(const LiveRange* lrSrc, const LiveRange* lrDest,
                 Inst* around_inst, Register src, Register dest, 
                 InstInsertLocation loc)
{
  //generate a comment
  char str[64]; char lrname[32]; char lrname2[32]; 
  LRName(lrSrc, lrname); LRName(lrDest, lrname2);
  sprintf(str, "RR COPY for %s --> %s", lrname, lrname2);
  Comment_Val comment = Comment_Install(str);

  //get opcode and alignment for live range
  Opcode_Names opcode = LiveRange_CopyOpcode(lrSrc);

  //create a new store instruction
  Inst* cp_inst = Inst_CreateCopy(opcode, comment, src, dest);

  //insert the new instruction
  if(loc == AFTER_INST)
    InsertInstAfter(cp_inst, around_inst);
  else
    InsertInstBefore(cp_inst, around_inst);
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
  Inst* cp_inst = (Inst*)Inst_Allocate(chow_arena, 1);
  Operation* cp_op = (Operation*)Operation_Allocate(chow_arena, 
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
}

/*
 *===================
 * Find_Set()
 *===================
 * Returns pointer to the UFSet structure for the given variable
 **/
UFSet* Find_Set(Variable v)
{
  return UFSet_Find(uf_sets[v]);
}

/*
 *===================
 * SSAName2LRID()
 *===================
 * Maps a variable to the initial live range id to which that variable
 * belongs. Once the splitting process starts this mapping may not be
 * valid and should not be used.
 **/
LRID SSAName2LRID(Variable v)
{
  assert(v < SSA_def_count);
  assert((lr_name_map[v] != NO_LRID) || v == 0);
  return lr_name_map[v];
}

/*
 *===================
 * Compute_BBStats()
 *===================
 * Gathers statistics about variables on a per-live range basis.
 * Collects:
 *  1) number of uses in the block
 *  2) number of defs in the block
 *  3) if the first occurance is a def
 ***/
BB_Stats Compute_BBStats(Arena arena, Unsigned_Int variable_count)
{
  Block* b;
  Inst* inst;
  Operation** op;
  Variable* reg;
  BB_Stats stats;
  BB_Stat* bstats;


  //allocate space to hold the stats
  stats = (BB_Stat**)
    Arena_GetMemClear(arena, sizeof(BB_Stat*) * (block_count + 1));
  ForAllBlocks(b)
  {
    stats[id(b)] = (BB_Stat*) 
      Arena_GetMemClear(arena, sizeof(BB_Stat) * variable_count);
  }

  LRID lrid;
  ForAllBlocks(b)
  {
    bstats = stats[id(b)];
    Block_ForAllInsts(inst, b)
    {
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].uses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].defs++;
          if(bstats[lrid].uses == 0)
            bstats[lrid].start_with_def = TRUE;
        }
      } 
    }
  }

  return stats;
}

/*
 *====================================
 * CheckRegisterLimitFeasibility()
 *====================================
 * This function makes sure that we can allocate the code given the
 * number of machine registers. We walk the code and look for the
 * maximum number of registers used/defined in any instruction and
 * make sure that number is fewer than the number of machine registers
 * we are given.
 ***/
void CheckRegisterLimitFeasibility(Unsigned_Int cRegMax)
{
  Block* b;
  Inst* inst;
  Operation** op;
  Register* reg;
  Unsigned_Int cRegUses;
  Unsigned_Int cRegDefs;

  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {
      cRegUses = 0;
      cRegDefs = 0;
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          cRegUses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          cRegDefs++;
        }
        if(cRegUses > cRegMax || cRegDefs > cRegMax)
        {
          Block_Dump(b, NULL, TRUE);
          fprintf(stderr, 
"Impossible allocation.\n\
You asked me to allocate with %d registers, but I found an operation\n\
with %d registers used and %d registers defined: %s. Sorry, but this\n\
is a research compiler not a magic wand. The offending block is \n\
printed above.\n", cRegMax,cRegUses,cRegDefs, oname(*op));
          exit(EXIT_FAILURE);
        }
      }
    } 


  }
}


/*
 *=======================
 * MoveLoadsAndStores()
 *=======================
 * Moves the loads and stores to a better position so that they might
 * be executed less times.
 *
 * IMPORTANT NOTE: This function must be called after the RenameRegisters
 * function. The reason is that we are inserting loads and stores for
 * MACHINE REGISTERS, not for SSA names. We have to do this because we
 * keep a map from <block id, lrid> --> allocated color. Since we are
 * moving instructions to different blocks and possibly adding blocks
 * this map would be difficult to maintain. So we solve that by simply
 * moving the stores and loads after renaming and using the machine
 * register assignments
 ***/
void MoveLoadsAndStores()
{
  using namespace std; //for list, pair
  typedef list<MovedSpillDescription>::iterator LI;
  //we should already have the loads and stores moved onto the
  //appropriate edge. all that remains is to walk the graph and
  //actually insert the instructions, splitting edges as needed.
  Block* blk;
  Boolean need_reorder = FALSE;
  ForAllBlocks(blk)
  {
    Edge* edg;
    Block_ForAllSuccs(edg, blk)
    {
      //if edg->edge_extension is not NULL then there is a load or
      //store moved onto this edge and we must process it
      if(edg->edge_extension)
      {
        //first we look at whether we need to split this edge in order
        //to move the loads and stores to their destinations
        //we need to split the block if:
        //1) we are inserting a store and the successor has more than
        //one pred
        //2) we are inserting a load and the predecessor has more than
        //one successor
        Boolean need_split = FALSE;
        for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
        {
          MovedSpillDescription msd = (*ee);
          if( msd.spill_type == STORE_SPILL && 
              Block_PredCount(edg->succ) > 1)
          {
            need_split = TRUE;
            break;
          }
          else if(msd.spill_type == LOAD_SPILL &&
                  Block_SuccCount(edg->pred) > 1)
          {
            need_split = TRUE;
            break;
          }
        }

        //split edge if needed
        Block* blkLD = edg->pred;
        Block* blkST = edg->succ;
        if(need_split || PARAM_EnhancedCodeMotion)
        {
          Block* blkT = SplitEdge(edg->pred, edg->succ);
          blkLD = blkST = blkT;
          need_reorder = TRUE;
        }

        //Now if we are using the enhanced code motion algorithm we
        //look for a store to a live range that has a load on the same
        //edge. This will only happen when the live range is split and
        //both parts get a register. if we find this situation then we
        //replace the load store with a register to register copy
        if(PARAM_EnhancedCodeMotion)
        {
          CopyList rr_copies;
          vector<LI> removals;
          //search through the spill list and look for load store to
          //same live range. if we find one then add the pair to the
          //rr_copies list and add the load and store to the removals
          //list. the removals list is used to remove the load/store
          //from the original spill list since we don't want to insert
          //those memory accesses anymore
          for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
          {
            LI eeT = ee; eeT++; //start with next elem of list
            for(;eeT != edg->edge_extension->spill_list->end(); eeT++)
            {
              if(ee->lr->orig_lrid == eeT->lr->orig_lrid)
              {
                //we put this pair into the copy list.
                //we can also remove the load now since that will get
                //its value from a copy. we need to keep the store
                //since its value may need to be loaded somewhere else
                //in the live range
                if(ee->spill_type == STORE_SPILL)
                {
                  rr_copies.push_back(make_pair(*ee, *eeT));
                  removals.push_back(eeT);
                }
                else
                {
                  rr_copies.push_back(make_pair(*eeT, *ee));
                  removals.push_back(ee);
                }
              }
            }
          }

          //remove all loads/store that will be turned into copies
          for(vector<LI>::iterator rIt = removals.begin();
              rIt != removals.end();
              rIt++)
          {
            edg->edge_extension->spill_list->erase((*rIt));
          }
         
          //if we found some load/store pairs that can be converted
          //to copies then insert those now.
          if(rr_copies.size() > 0)
          {
            debug("converting some load/store pairs to copies");
            //order the copies so that we don't write to a register
            //before reading its value
            CDL ordered_copies;
            if(OrderCopies(rr_copies, &ordered_copies))
            {
              
              //run through the ordered copies and insert them
              for(CDL::iterator cdIT = ordered_copies.begin();
                  cdIT != ordered_copies.end();
                  cdIT++)
              {
                //better than the other.
                //insert the copy in the newly created block right
                //before the last instruction. this will ensure that the
                //copies are sandwiched between the loads and stores in
                //this block (when they are inserted below) which is 
                //necessary for correct code
                Insert_Copy(cdIT->src_lr, cdIT->dest_lr,
                            Block_LastInst(blkLD),
                            cdIT->src_reg, cdIT->dest_reg, BEFORE_INST);
              }
            }
            //could not find a suitable order for the copies
            //revert to inserting loads for all the variables that
            //would be copies
            else 
            {
              error("reverting to using loads instead or copies");
              //return all the loads to the spill list for this edge
              //so they can be inserted below
              for(CopyList::const_iterator clIT = rr_copies.begin();
                  clIT != rr_copies.end(); clIT++)
              {
                edg->edge_extension->spill_list->push_back(clIT->second);
                chowstats.cThwartedCopies++;
              }
            }
          }
        } // -- END EnhancedCodeMotion -- //


        //now we know where to move the ld/store lets actually do it
        for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
        {
          MovedSpillDescription msd = (*ee);
          //use machine registers for these loads/stores 
          Register mReg =
            GetMachineRegAssignment(msd.orig_blk, msd.lr->orig_lrid);
          if(msd.spill_type == STORE_SPILL)
          {
            debug("moving store from %s to %s for lrid: %d_%d",
                   bname(msd.orig_blk), bname(blkST),
                   msd.lr->orig_lrid, msd.lr->id);
            Insert_Store(msd.lr->id, Block_FirstInst(blkST),
                         mReg, REG_FP,
                         BEFORE_INST);
          }
          else
          {
            debug("moving load from %s to %s for lrid: %d_%d",
                   bname(msd.orig_blk), bname(blkLD),
                   msd.lr->orig_lrid, msd.lr->id);
            Insert_Load(msd.lr->id, Block_LastInst(blkLD), 
                        mReg, REG_FP);
          }
        }
      }
    }
  }// -- END: for all blocks -- //
  if(need_reorder) //needed if we split an edge
  {
    Block_Order();
  }
}

namespace{
/*
 *=======================
 * OrderCopies()
 *=======================
 * Tries to order the copies so that no value is written before it is
 * read. Returns true if a successful order was found or false
 * otherwise.
*/ 
bool OrderCopies(const CopyList& rr_copies, CDL* ordered_copies)
{
  //go through all the copies that we are going to insert and order
  //them so that we do not overwrite any values before they are copied
  //to their destination registers
  for(CopyList::const_iterator clIT = rr_copies.begin();
                clIT != rr_copies.end(); clIT++)
  {
    MovedSpillDescription msdSrc = clIT->first;
    MovedSpillDescription msdDest = clIT->second;
    assert(msdSrc.spill_type == STORE_SPILL &&
            msdDest.spill_type == LOAD_SPILL &&
            msdSrc.lr->orig_lrid == msdDest.lr->orig_lrid);

    CopyDescription cd;
    cd.src_lr = msdSrc.lr;
    cd.src_reg = GetMachineRegAssignment(msdSrc.orig_blk, 
                                        msdSrc.lr->orig_lrid);
    cd.dest_lr = msdDest.lr;
    cd.dest_reg = GetMachineRegAssignment(msdDest.orig_blk, 
                                        msdDest.lr->orig_lrid);
    

    //insert this copy in the correct place in the ordered list
    bool inserted = false;
    for(CDL::iterator cdIT = ordered_copies->begin();
        cdIT != ordered_copies->end();
        cdIT++)
    {
      //check for copies of the form r1 => r2, r2 => r1
      //it:  y  => x
      //cd:  x  => y
      if(cdIT->src_reg == cd.dest_reg && cdIT->dest_reg == cd.src_reg)
      {
        error("cyclic dependence in same register copy");
        return false;
      }

      //do I define someone you use?
      //it:  y  => ...
      //cd: ... => y
      if(cdIT->src_reg == cd.dest_reg)
      {
        //I must come after you, keep going
        if(inserted){error("cyclic dependence in copies"); abort();}
      }

      //do you define someone I use?
      //cd:  z  => ...
      //it: ... => z
      if(cdIT->dest_reg == cd.src_reg)
      {
        //I must come before you, insert here
        if(!inserted){ordered_copies->insert(cdIT, cd); inserted=true;}
      }
    }
    //if we got to the end of the ordered list without inserting then
    //just put it at the end of the list (no dependencies)
    if(!inserted){ordered_copies->push_back(cd);}
  }

  return true;
}
}

/*
 *===========
 * Function()
 *===========
 *
 ***/

