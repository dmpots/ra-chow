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
#include "params.h"
#include "live_range.h"
#include "live_unit.h"
#include "union_find.h"
#include "reach.h"
#include "debug.h"
#include "rc.h" //RegisterClass definitions 
#include "assign.h" //Handles some aspects of register assignment
#include "cfg_tools.h" 
#include "spill.h" 
#include "color.h" 
#include "stats.h" 
#include "mapping.h" 


/* types */
//using namespace std;
typedef unsigned int LOOPVAR;

/* globals */
LRVec Chow::live_ranges;
Arena  chow_arena;
Unsigned_Int** mBlkIdSSAName_Color;

/* locals */
static Unsigned_Int clrInitial = 0; //count of initial live ranges

/* local functions */
static void assert_same_orig_name(LRID,Variable,VectorSet,Block* b);
static void AllocChowMemory();
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
static void MoveLoadsAndStores();
void AllocLiveRanges(Arena arena, Unsigned_Int num_lrs);
void SplitNeighbors(LiveRange* lr, LRSet* constr_lr, LRSet* unconstr_lr);
void UpdateConstrainedLists(LiveRange* , LiveRange* , LRSet*, LRSet*);
LiveUnit* AddLiveUnitOnce(LRID, Block*, VectorSet, Variable);
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs);

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
  using Chow::live_ranges;

  LiveRange* lr;
  LRSet constr_lrs;
  LRSet unconstr_lrs;

  //allocate any memory needed by the chow allocator
  InitChow();

  //separate unconstrained live ranges
  for(LRVec::size_type i = 0; i < live_ranges.size(); i++)
  {
    lr = live_ranges[i];
    //only look at candidates
    if(!lr->is_candidate) continue;

    if(lr->IsConstrained())
    {
      constr_lrs.insert(lr);
      debug("Constrained LR:    %d", lr->id);
    }
    else
    {
      unconstr_lrs.insert(lr);
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
    lr->AssignColor();
    debug("LR: %d is top priority, given color: %d", lr->id, lr->color);
    SplitNeighbors(lr, &constr_lrs, &unconstr_lrs);
  }

  //assign registers to unconstrained live ranges
  debug("assigning unconstrained live ranges colors");
  //some may no longe be candidates now
  for(LRSet::iterator i = 
      unconstr_lrs.begin(); i != unconstr_lrs.end(); i++)
  {
    lr = *i;
    if(!lr->is_candidate) continue;

    debug("choose color for unconstrained LR: %d", lr->id);
    lr->AssignColor();
    debug("LR: %d is given color:%d", lr->id, lr->color);
  }

  //record some statistics about the allocation
  Stats::chowstats.clrFinal = live_ranges.size();
}

/*
 *=======================================
 * ComputePriorityAndChooseTop()
 *=======================================
 *
 ***/
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs)
{
  float top_prio = -3.4e38; //a very small number
  LiveRange* top_lr = NULL;
  LiveRange* lr = NULL;
  
  //look at all candidates
  for(LRSet::iterator i = lrs->begin(); i != lrs->end(); i++)
  {
    lr = *i;
    if(!lr->is_candidate) continue;

    //priority has never been computed
    if(lr->priority == LiveRange::UNDEFINED_PRIORITY)
    {
      lr->ComputePriority();
      debug("priority for LR: %d is %.3f", lr->id, lr->priority);

      //check to see if this live range is a non-candidate for
      //allocation. I think we need to only check this the first time
      //we compute the priority function. if the priority changes due
      //to a live range split it should be reset to undefined so we
      //can compute it again.
      if(lr->priority < 0.0 || lr->IsEntirelyUnColorable())
      {
        lr->MarkNonCandidateAndDelete();
        continue;
      }
    }

    //see if this live range has a greater priority
    if(lr->priority > top_prio)
    {
      top_prio = lr->priority;
      top_lr = lr;
    }
  }

  if(top_lr != NULL)
  {
    debug("top priority is %.3f LR: %d", top_prio, top_lr->id);
    lrs->erase(top_lr);
  }
  return top_lr;
}

/*
 *=============================
 * AllocChowMemory()
 *=============================
 * Allocates memory used for chow algorithm
 */
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
                        sizeof(Unsigned_Int) * Chow::live_ranges.size() );
      LOOPVAR j;
      for(j = 0; j < Chow::live_ranges.size(); j++)
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
  Spill::Init();
  Chow::live_ranges[Spill::frame.lrid]->MarkNonCandidateAndDelete();

  //clear out edge extensions if needed
  if(Params::Algorithm::move_loads_and_stores)
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
  using Chow::live_ranges;
  using Mapping::SSAName2LRID;

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
  Stats::chowstats.clrInitial = clrInitial;

  //create a mapping from ssa names to live range ids
  Mapping::CreateLiveRangeNameMap(uf_arena);
  Mapping::ConvertLiveInNamespaceSSAToLiveRange();
  if(Params::Machine::enable_register_classes)
  {
    RegisterClass_CreateLiveRangeTypeMap(uf_arena, clrInitial);
  }

  //initialize coloring structures based on number of register classes
  Coloring::Init(chow_arena, cRegisterClass);

  //now that we know how many live ranges we start with allocate them
  Stats::ComputeBBStats(uf_arena, SSA_def_count);
  AllocLiveRanges(chow_arena, clrInitial);

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
        if(lr->rc == lrT->rc)
        {
          //debug("%d conflicts with %d", v, i);
          lr->AddInterference(lrT);
        }
      }
    }
  }


  //compute where the loads and stores need to go in the live range
  for(LRVec::size_type i = 0; i < live_ranges.size(); i++)
    live_ranges[i]->MarkLoadsAndStores();

  Debug::LiveRange_DDumpAll(&live_ranges);
}

/*
 *============================
 * AllocLiveRanges()
 *============================
 * Allocates space for initial live ranges and sets default values.
 *
 ***/
void AllocLiveRanges(Arena arena, Unsigned_Int num_lrs)
{
  using Chow::live_ranges;

  //initialize LiveRange class
  LiveRange::Init(arena, num_lrs);

  //create initial live ranges
  live_ranges.resize(num_lrs, NULL); //allocate space
  for(LOOPVAR i = 0; i < num_lrs; i++) //allocate each live range
  {
    live_ranges[i] = 
      new LiveRange(RegisterClass_InitialRegisterClassForLRID(i), i);
  }
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
    LiveUnit* unit =  Chow::live_ranges[lrid]->LiveUnitForBlock(b);
    //debug("already present: %d, orig_name: %d new_orig: %d  block: %s (%d)", 
   //                       lrid, unit->orig_name, v, bname(b), id(b));
    assert(unit->orig_name == v);
  }
}

/*
 *=============================
 * AddLiveUnitOnce()
 *=============================
 *
 * adds the block to the live range, but only once depending on the
 * contents of the *lrset*
 * returns the new LiveUnit or NULL if it is already in the live range
 ***/
LiveUnit* 
AddLiveUnitOnce(LRID lrid, Block* b, VectorSet lrset, Variable orig_name)
{
  //debug("ADDING: %d BLOCK: %s (%d)", lrid, bname(b), id(b));
  LiveUnit* new_unit = NULL;
  if(!VectorSet_Member(lrset, lrid))
  {
    LiveRange* lr = Chow::live_ranges[lrid];
    VectorSet_Insert(lrset, lrid);
    Stats::BBStats bbstat = Stats::GetStatsForBlock(b, lr->id);
    new_unit = lr->AddLiveUnitForBlock(b, orig_name, bbstat);
  }

  return new_unit;
} 
 

/*
 *============================
 * SplitNeighbors()
 *============================
 *
 * Checks a live range for neighbors that need to be split because we
 * just assigned a color to this live range. a neighbor will need to
 * be split when its forbidden set is equal to the set of all
 * registers.
 *
 * splitting the neighbors may shuffle them around on the constrained
 * and unconstrained lists so we pass them in for possible
 * modification.
 ***/
void SplitNeighbors(LiveRange* lr, LRSet* constr_lr, LRSet* unconstr_lr)
{
  debug("BEGIN SPLITTING");

  //make a copy of the interference list as a worklist since splitting
  //may add and remove items to the original interference list
  LRVec worklist(lr->fear_list->size());
  copy(lr->fear_list->begin(), lr->fear_list->end(), worklist.begin());

  //our neighbors are the live ranges we interfere with
  LiveRange* intf_lr;
  while(!worklist.empty())
  {
    intf_lr = worklist.back(); worklist.pop_back();
    //only check allocation candidates, may not be a candidate if it
    //has already been assigned a color
    if(!(intf_lr->is_candidate)) continue;

    //split if no registers available
    if(!intf_lr->HasColorAvailable())
    {
      debug("Need to split LR: %d", intf_lr->id);
      if(intf_lr->IsEntirelyUnColorable())
      {
        //delete this live range from the interference graph. we dont
        //need to update the constrained lists at this point because
        //deleting this live range should have no effect on whether
        //the live ranges it interferes with are constrained or not
        //since it was never given a color
        intf_lr->MarkNonCandidateAndDelete();
      }
      else //try to split
      {
        //Split() returns the new live range that we know is colorable
        LiveRange* newlr = intf_lr->Split();
        //add new liverange to list of live ranges
        Chow::live_ranges.push_back(newlr);
        debug("ADDED LR: %d", newlr->id);

        //make sure constrained lists are up-to-date after split
        UpdateConstrainedLists(newlr, intf_lr, constr_lr, unconstr_lr);

        //if the remainder of the live range we just split from
        //interferes with the live range we assigned a color to then 
        //add it to the work list because it may need to be split more
        if(intf_lr->InterferesWith(lr)) worklist.push_back(intf_lr);

        debug("split complete for LR: %d", intf_lr->id);
        Debug::LiveRange_DDump(intf_lr);
        Debug::LiveRange_DDump(newlr);
      }
    }
  }
  debug("DONE SPLITTING");
} 


/*
 *================================
 * UpdateConstrainedLists()
 *================================
 * Makes sure that the live ranges are in the constrained lists if
 * they are constrained. This is used to update the lists after a live
 * range split for the live ranges that interfere with both the old
 * and the new live range.
 *
 ***/
void UpdateConstrainedLists(LiveRange* newlr, 
                            LiveRange* origlr,
                            LRSet* constr_lrs, 
                            LRSet* unconstr_lrs)
{
  //update constrained lists, only need to update for any live range
  //that interferes with both the new and original live range because
  //those are the only live ranges that could have changed status
  LRSet updates;
  set_intersection(newlr->fear_list->begin(), newlr->fear_list->end(),
                   origlr->fear_list->begin(), origlr->fear_list->end(),
                   inserter(updates,updates.begin()));

  for(LRSet::iterator i = updates.begin(); i != updates.end(); i++)
  {
    LiveRange* lr = *i;
    if(!lr->is_candidate) continue;

    if(lr->IsConstrained())
    {
      debug("ensuring LR: %d is in constr", lr->id);
      unconstr_lrs->erase(lr);
      constr_lrs->insert(lr);
    }
  }

  //also, need to update the new and original live range positions
  if(newlr->IsConstrained())
  {
    constr_lrs->insert(newlr);
  }
  else
  {
    unconstr_lrs->insert(newlr);
  }
  if(!origlr->IsConstrained())
  {
    constr_lrs->erase(origlr);
    unconstr_lrs->insert(origlr);
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
  using Mapping::SSAName2LRID;

  debug("allocation complete. renaming registers...");

  //stack pointer is the initial size of the stack frame
  debug("STACK: %d", Spill::frame.stack_pointer);

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
  if(Params::Algorithm::move_loads_and_stores)
  {
    debug("moving loads and stores to \"optimal\" position");
    MoveLoadsAndStores();
  }

  //finally rewrite the frame statement to have the first register be
  //the frame pointer and to adjust the stack size
  Spill::RewriteFrameOp();
}


/*
 *==========================
 * GetMachineRegAssignment()
 *==========================
 * Gets the machine register assignment for a lrid in a given block
 **/
Register GetMachineRegAssignment(Block* b, LRID lrid)
{

  if(lrid == Spill::frame.lrid)
    return Spill::REG_FP;

   /* return REG_UNALLOCATED; spill everything */

  Register color = mBlkIdSSAName_Color[id(b)][lrid];
  if(color == NO_COLOR)
    return REG_UNALLOCATED;

  RegisterClass rc = Chow::live_ranges[lrid]->rc;
  return RegisterClass_MachineRegForColor(rc, color);
}
/*
 *===================
 * Insert_Load()
 *===================
 * Inserts a load instruction for the given live range before the
 * passed instruction.
 */
//FIXME: move this to spill.cc
void Insert_Load(LRID lrid, Inst* before_inst, Register dest, 
                                               Register base)
{
  LiveRange* lr = Chow::live_ranges[lrid];
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
  LiveRange* lr = Chow::live_ranges[lrid];
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
  Opcode_Names opcode = lrSrc->CopyOpcode();

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
 *====================================
 * CheckRegisterLimitFeasibility()
 *====================================
 * This function makes sure that we can allocate the code given the
 * number of machine registers. We walk the code and look for the
 * maximum number of registers used/defined in any instruction and
 * make sure that number is fewer than the number of machine registers
 * we are given.
 *
 * if ForceMinimumRegisterCount is enabled then we will modify
 * the number of machine registers.
 ***/
void CheckRegisterLimitFeasibility()
{
  Block* b;
  Inst* inst;
  Operation** op;
  Register* reg;
  int cRegUses;
  int cRegDefs;
  int cRegMax = 0;

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
        if(cRegUses > cRegMax ){cRegMax = cRegUses; }
        if(cRegDefs > cRegMax) {cRegMax = cRegDefs; }
      }
    } 
  }
  if(cRegMax > Params::Machine::num_registers)
  {
    if(Params::Program::force_minimum_register_count)
    {
      Params::Machine::num_registers = max(cRegMax,4); //4 is my minimum
      fprintf(stderr, 
      "Adjusting the number of machine registers "
      "to permit allocation: %d\n", Params::Machine::num_registers);
    }
    else
    {
      //Block_Dump(b, NULL, TRUE);
          fprintf(stderr, 
"Impossible allocation.\n\
You asked me to allocate with %d registers, but I found an operation\n\
that needs %d registers. Sorry, but this is a research compiler not \n\
a magic wand.\n", Params::Machine::num_registers, cRegMax);
          exit(EXIT_FAILURE);
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
        if(need_split || Params::Algorithm::enhanced_code_motion)
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
        if(Params::Algorithm::enhanced_code_motion)
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
                Stats::chowstats.cInsertedCopies++;
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
                Stats::chowstats.cThwartedCopies++;
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
                         mReg, Spill::REG_FP,
                         BEFORE_INST);
          }
          else
          {
            debug("moving load from %s to %s for lrid: %d_%d",
                   bname(msd.orig_blk), bname(blkLD),
                   msd.lr->orig_lrid, msd.lr->id);
            Insert_Load(msd.lr->id, Block_LastInst(blkLD), 
                        mReg, Spill::REG_FP);
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
        if(inserted)
        {
          error("cyclic dependence in copies");
          return false;
        }
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

