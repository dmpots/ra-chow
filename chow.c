/*====================================================================
 * 
 *====================================================================
 ********************************************************************/

/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <SSA.h>
#include <algorithm>
#include <utility>

#include "chow.h"
#include "chow_extensions.h"
#include "params.h"
#include "live_range.h"
#include "live_unit.h"
#include "union_find.h"
#include "rc.h" //RegisterClass definitions 
#include "assign.h" //Handles some aspects of register assignment
#include "cfg_tools.h" 
#include "spill.h" 
#include "color.h" 
#include "stats.h" 
#include "mapping.h" 
#include "cleave.h"
#include "depths.h" //for computing loop nesting depth
#include "shared_globals.h" //Global namespace for iloc Shared vars

#include "/home/compiler/installed/parse/src/Inst.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
  /* local functions */
  void assert_same_orig_name(LRID,Variable,VectorSet,Block* b);
  void MoveLoadsAndStores();
  void AllocLiveRanges(Arena arena, Unsigned_Int num_lrs);
  void SplitNeighbors(LiveRange*, LRSet*, LRSet*);
  void UpdateConstrainedLists(LiveRange* , LiveRange* , LRSet*, LRSet*);
  LiveUnit* AddLiveUnitOnce(LRID, Block*, VectorSet, Variable);
  LiveRange* ComputePriorityAndChooseTop(LRSet* lrs);
  void BuildInitialLiveRanges(Arena);
  void AllocateRegisters();
  void RenameRegisters();
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow {
  /* globals */
  Arena arena;
  LRVec live_ranges;
}

void Chow::Run()
{
  //arena for all chow memory allocations
  arena = Arena_Create(); 

  //--- Initialization for building live ranges ---//
  if(Params::Algorithm::bb_max_insts > 0)
  {
    //split basic blocks to desired size
    InitCleaver(arena, Params::Algorithm::bb_max_insts);
    CleaveBlocks();
  }
  RegisterClass::Init(arena, 
                      Params::Machine::num_registers,
                      Params::Machine::enable_register_classes,
                      Params::Algorithm::num_reserved_registers);

  //--- Build live ranges ---//
  BuildInitialLiveRanges(arena);
  if(Debug::dot_dump_lr){
    LiveRange* lr = live_ranges[Debug::dot_dump_lr];
    Debug::DotDumpLR(lr, "initial");
    Debug::dot_dumped_lrs.push_back(lr);
  }

  //--- Initialization for allocating registers ---//
  //compute loop nesting depth needed for computing priorities
  find_nesting_depths(arena); Globals::depths = depths;
  Spill::Init(arena);
  Assign::Init(arena);
  if(Params::Algorithm::move_loads_and_stores)
  {
    //clear out edge extensions 
    Block* b;
    Edge* e;
    ForAllBlocks(b)
    {
      Block_ForAllPreds(e,b) e->edge_extension = NULL;
      Block_ForAllSuccs(e,b) e->edge_extension = NULL;
    }
  }

  //--- Run the priority algorithm ---//
  AllocateRegisters();
  RenameRegisters();
  if(Debug::dot_dump_lr) Debug::DotDumpFinalLRs(); 
}

/*-----------------INTERNAL MODULE FUNCTIONS-------------------*/
namespace {
/*
 *=======================
 * RunChow()
 *=======================
 *
 * Does the actual register allocation
 *
 ***/
void AllocateRegisters()
{
  using Chow::live_ranges;

  LiveRange* lr;
  LRSet constr_lrs;
  LRSet unconstr_lrs;

  //the register that holds the frame pointer is not a candidate for
  //allocation since it resides in a special reserved register. remove
  //this from the interference graph 
  live_ranges[Spill::frame.lrid]->MarkNonCandidateAndDelete();


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
 * BuildInitialLiveRanges()
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
void BuildInitialLiveRanges(Arena chow_arena)
{
  using Chow::live_ranges;
  using Mapping::SSAName2OrigLRID;

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

  unsigned int clrInitial = uf_set_count - 1; //no lr for SSA name 0
  debug("SSA NAMES: %d", SSA_def_count);
  debug("UNIQUE LRs: %d", clrInitial);
  Stats::chowstats.clrInitial = clrInitial;

  //create a mapping from ssa names to live range ids
  Mapping::CreateLiveRangeNameMap(uf_arena);
  Mapping::ConvertLiveInNamespaceSSAToLiveRange();
  if(Params::Machine::enable_register_classes)
  {
    RegisterClass::CreateLiveRangeTypeMap(uf_arena, clrInitial);
  }

  //initialize coloring structures based on number of register classes
  Coloring::Init(chow_arena, clrInitial);

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
          lrid = SSAName2OrigLRID(*reg);
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
          lrid = SSAName2OrigLRID(*reg);
          AddLiveUnitOnce(lrid, b, lrset, *reg);
        }
      } 
    }

    //Now add in the live_out set to the variables that include this
    //block in their live range
    Liveness_Info info;
    info = SSA_live_out[id(b)];
    for(unsigned int j = 0; j < info.size; j++)
    {
      //add block to each live range
      lrid = SSAName2OrigLRID(info.names[j]);
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
  for(unsigned int i = 0; i < num_lrs; i++) //allocate each live range
  {
    live_ranges[i] = 
      new LiveRange(RegisterClass::InitialRegisterClassForLRID(i), i);
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
  using Mapping::SSAName2OrigLRID;
  using Assign::GetMachineRegAssignment;
  using Assign::ResetFreeTmpRegs;
  using Assign::EnsureReg;
  using Assign::UnEvict;

  debug("allocation complete. renaming registers...");

  //stack pointer is the initial size of the stack frame
  debug("STACK: %d", Spill::frame.stack_pointer);

  Block* b;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  std::vector<Register> instUses;
  std::vector<Register> instDefs;

  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {
      debug("renaming inst:\n%s", Debug::StringOfInst(inst));
      /* collect a list of uses and defs used in this regist that is
       * used in algorithms when deciding which registers can be
       * evicted for temporary uses */
      instUses.clear();
      instDefs.clear();
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          LRID lrid = SSAName2OrigLRID(*reg);
          instUses.push_back(lrid);
        }

        Operation_ForAllDefs(reg, *op)
        {
          LRID lrid = SSAName2OrigLRID(*reg);
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
          //make sure the live range is in a register
          EnsureReg(reg, b, origInst, updatedInst,
                    *op, FOR_USE,
                    instUses, instDefs);
        }

        Operation_ForAllDefs(reg, *op)
        {
          //make sure the live range is in a register
          EnsureReg(reg, b, origInst, updatedInst,
                    *op, FOR_DEF,
                    instUses, instDefs);
        }
      } 
      UnEvict(updatedInst);
    }
    //make available the tmp regs used in this instruction
    //pass in a pointer to the last instruction in the block so
    //that we can insert loads for evicted registers
    ResetFreeTmpRegs(b->inst->prev_inst); 
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

        //enhanced code motion attempts to replace a load/store pair
        //on an edge with a copy
        if(Params::Algorithm::enhanced_code_motion)
        {
          Chow::Extensions::EnhancedCodeMotion(edg, blkLD);
        } 

        //now we know where to move the ld/store lets actually do it
        for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
        {
          MovedSpillDescription msd = (*ee);
          //use machine registers for these loads/stores 
          Register mReg =
            Assign::GetMachineRegAssignment(msd.orig_blk, msd.lr->orig_lrid);
          if(msd.spill_type == STORE_SPILL)
          {
            debug("moving store from %s to %s for lrid: %d_%d",
                   bname(msd.orig_blk), bname(blkST),
                   msd.lr->orig_lrid, msd.lr->id);
            Spill::InsertStore(msd.lr, Block_FirstInst(blkST),
                               mReg, Spill::REG_FP,
                               BEFORE_INST);
          }
          else
          {
            debug("moving load from %s to %s for lrid: %d_%d",
                   bname(msd.orig_blk), bname(blkLD),
                   msd.lr->orig_lrid, msd.lr->id);
            Spill::InsertLoad(msd.lr, Block_LastInst(blkLD), 
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

}

/*
 *===========
 * Function()
 *===========
 *
 ***/

