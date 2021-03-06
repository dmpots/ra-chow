/*====================================================================
 * chow.cc
 * 
 * contains an implementation of the chow register allocation
 * algorithm. 
 *====================================================================
 ********************************************************************/

/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <SSA.h>
#include <algorithm>
#include <utility>
#include <map>
#include <stack>
#include <queue>

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
#include "rematerialize.h" //Global namespace for iloc Shared vars
#include "heuristics.h" //heuristics for splitting, etc.
#include "reach.h"


/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
  /* local functions */
  void assert_same_orig_name(LRID,Variable,SparseSet,Block* b);
  void MoveLoadsAndStores();
  unsigned int FindLiveRanges(Arena uf_arena);
  void CreateLiveRanges(Arena arena, Unsigned_Int num_lrs);
  void SplitNeighbors(LiveRange*, LRSet*, LRSet*);
  void UpdateConstrainedLists(LiveRange* , LiveRange* , LRSet*, LRSet*);
  void UpdateConstrainedListsAfterDelete(LiveRange*, LRSet*, LRSet*);
  LiveUnit* AddLiveUnitOnce(LRID, Block*, SparseSet, Variable);
  LiveRange* ComputePriorityAndChooseTop(LRSet*, LRSet*);
  void BuildInitialLiveRanges(Arena);
  void BuildInterferences(Arena arena);
  void AllocateRegisters();
  void RenameRegisters();
  bool ShouldSplitLiveRange(LiveRange* lr);
  inline void AddToCorrectConstrainedList(LRSet*,LRSet*,LiveRange*);
  void CountLocals();
  void DumpLocals();
  void SeparateConstrainedLiveRanges(LRSet*, LRSet*);
  void ColorUnconstrained(LRSet* unconstr_lrs);
  void PullNodeFromGraph(LiveRange* lr, LRSet* constr_lrs);
  bool LiveIn(LRID orig_lrid, Block* blk);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow {
  /* globals */
  Arena arena;
  LRVec live_ranges;
  std::vector<std::vector<LiveUnit*> > live_units;
  std::map<Variable,bool> local_names;
  std::stack<LiveRange*> color_stack;
}

void Chow::Run()
{
  //--- Initialization for building live ranges ---//
  //arena for all chow memory allocations
  if(arena == NULL){arena = Arena_Create(); }

  //--- Build live ranges ---//
  BuildInitialLiveRanges(arena);
  //DumpLocals();
  if(Debug::dot_dump_lr){
    LiveRange* lr = live_ranges[Debug::dot_dump_lr];
    Debug::DotDumpLR(lr, "initial");
    Debug::dot_dumped_lrs.push_back(lr);
  }

  //--- Initialization for allocating registers ---//
  //compute loop nesting depth needed for computing priorities
  find_nesting_depths(arena); Globals::depths = depths;
  Spill::Init(arena);
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
    Stats::Start("Allocate Registers");
  AllocateRegisters();
  if(Debug::dot_dump_lr) Debug::DotDumpFinalLRs(); 
    Stats::Stop();
    Stats::Start("Rename Registers");
  RenameRegisters();
    Stats::Stop();
}

/*-----------------INTERNAL MODULE FUNCTIONS-------------------*/
namespace {
/*
 *=======================
 * AllocateRegisters()
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
  SeparateConstrainedLiveRanges(&constr_lrs, &unconstr_lrs);

  //assign registers to constrained live ranges
  lr = NULL;
  while(!(constr_lrs.empty()))
  {
    //steps 2: (a) - (c)
    lr = ComputePriorityAndChooseTop(&constr_lrs, &unconstr_lrs);
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
  ColorUnconstrained(&unconstr_lrs);

  //record some statistics about the allocation
  Stats::chowstats.clrFinal = live_ranges.size();
}

/*
 *=======================================
 * ComputePriorityAndChooseTop()
 *=======================================
 *
 ***/
LiveRange* 
ComputePriorityAndChooseTop(LRSet* constr_lrs, LRSet* unconstr_lrs)
{
  std::vector<LiveRange*> deletes;
  
  //compute priority for all live ranges
  for(LRSet::iterator i = constr_lrs->begin(); i != constr_lrs->end(); i++)
  {
    LiveRange* lr = *i;
    assert(lr->is_candidate);

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
      if(lr->priority <= 0.0 || lr->IsEntirelyUnColorable())
      {
        deletes.push_back(lr);
      }
    }
  }

  //remove any live ranges not deemed worthy
  for(unsigned int i = 0; i < deletes.size(); i++)
  {
    LiveRange* lr = deletes[i];
    lr->MarkNonCandidateAndDelete(); Stats::chowstats.cSpills++;
    UpdateConstrainedListsAfterDelete(lr, constr_lrs, unconstr_lrs);
  }

  //find the top priority live range
  float top_prio = -3.4e38; //a very small number
  LiveRange* top_lr = NULL;
  for(LRSet::iterator i = constr_lrs->begin(); i != constr_lrs->end(); i++)
  {
    LiveRange* lr = *i;

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
    constr_lrs->erase(top_lr);
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

  //run union find over phi nodes to get initial live ranges
    Stats::Start("Create LiveRanges");
  unsigned int clrInitial = FindLiveRanges(chow_arena);
  --clrInitial; //no lr for SSA name 0

  debug("SSA NAMES: %d", SSA_def_count);
  debug("UNIQUE LRs: %d", clrInitial);
  Stats::chowstats.clrInitial = clrInitial;

  //create a mapping from ssa names to live range ids
  Mapping::CreateLiveRangeNameMap(chow_arena);
  Mapping::CreateLiveRangeTypeMap(chow_arena, clrInitial);
  Mapping::ConvertLiveInNamespaceSSAToLiveRange();

  //initialize coloring structures based on number of register classes
  Coloring::Init(chow_arena, clrInitial);

  //now that we know how many live ranges we start with allocate them
  Stats::ComputeBBStats(chow_arena, SSA_def_count);
  CreateLiveRanges(chow_arena, clrInitial);
    Stats::Stop();

  //find all interferenes for each live range
    Stats::Start("Build Interferences");
  BuildInterferences(chow_arena);
    Stats::Stop();

  if(Params::Algorithm::rematerialize)
  {
    Stats::Start("Split Rematerializable");
    //run through and find the live ranges that are rematerializable
    //and set the appropriate flags. these may exist separately from
    //the split live ranges if the entire live range is
    //rematerialiazible. so we set them all in this loop. if we split
    //out part that is not rematerializable it will be set in th
    //function below
    for(unsigned int ssa_name = 0; ssa_name < SSA_def_count; ssa_name++)
    {
      if(Remat::tags[ssa_name].val == Remat::CONST)
      {
        LRID lrid = Mapping::SSAName2OrigLRID(ssa_name);
        live_ranges[lrid]->rematerializable = true;
        live_ranges[lrid]->remat_op = Remat::tags[ssa_name].op;
      }
    }
    Remat::SplitRematerializableLiveRanges();
    Stats::chowstats.clrRemat = live_ranges.size();
    Stats::Stop();
  }

  //compute where the loads and stores need to go in the live range
  for(LRVec::size_type i = 0; i < live_ranges.size(); i++)
    live_ranges[i]->MarkLoadsAndStores();

  Debug::LiveRange_DDumpAll(&live_ranges);
}

/*
 *=============================
 * FindLiveRanges()
 *=============================
 * Uses a fast union find algorithm to union phi-params to find out
 * which values belong in the same live range
 * 
 */
unsigned int FindLiveRanges(Arena uf_arena)
{
  UFSets_Init(uf_arena, SSA_def_count);
  if(Params::Algorithm::rematerialize)
  {
    Remat::ComputeTags();
    Remat::remat_sets = UFSet_Create(SSA_def_count);
  }

  Block* b;
  Phi_Node* phi;
  UFSet* set;
  unsigned int liverange_count = SSA_def_count;
  ForAllBlocks(b)
  {
    //visit each phi node and union the parameters and destination
    //register together since they should all be part of the same live
    //range
    Block_ForAllPhiNodes(phi, b)
    {
      debug("process phi: %d at %s (%d)", phi->new_name, bname(b), bid(b));
      Variable* v_ptr;
      //find current sets for the phi node
      set = Find_Set(phi->new_name);

      Phi_Node_ForAllParms(v_ptr, phi)
      {
        Variable v = *v_ptr;
        if(v != 0)
        {
          //union sets together unless they are alredy part of the
          //same live range
          if(set != Find_Set(v))
          {
            set = UFSet_Union(set, Find_Set(v));
            --liverange_count;
            debug("union: %d U %d = %d(setid)", phi->new_name, v, set->id);
          }

          if(Params::Algorithm::rematerialize)
          {
            UFSet* remat_set = Find_Set(phi->new_name, Remat::remat_sets);
            //selectively union if using rematerialization
            if(Remat::tags[v].val == Remat::tags[phi->new_name].val)
            {
              debug("live range union ok by remat: %d",v);
              remat_set = 
                UFSet_Union(remat_set, Find_Set(v, Remat::remat_sets));
            }
            else //split live ranges
            {
              debug("live range split by remat: %d",v);
              Remat::AddSplit(phi->new_name, v);
            }
          }
        }
      }
    }
  }

  return liverange_count;
}

/*
 *=============================
 * BuildInterferences()
 *=============================
 * Construct the interferences for each live range
 * 
 */

void BuildInterferences(Arena arena)
{
  using Chow::live_ranges;
  using Mapping::SSAName2OrigLRID;

  //build the interference graph
  //find all live ranges that are referenced or live out in this
  //block. those are the live ranges that need to include this block.
  //each of those live ranges will interfere with every other live
  //range in the block. walk through the graph and examine each block
  //to build the live ranges
  LiveRange* lr;
  LRID lrid;
  Block* blk;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  SparseSet lrset = SparseSet_Create(arena, live_ranges.size());
  ForAllBlocks(blk)
  {
    debug("processing blk:%s (%d)", bname(blk), bid(blk));

    //we need to acccount for any variable that is referenced in this
    //block as it may not be in the live_out set, but should still be
    //included in the live range if it is referenced in this block. A
    //name may be in a live range under more than one original name.
    //for example when a name is defined in two branches of an if
    //statement it will be added to the live range by that definition,
    //but will also be live out in those blocks under a different name
    //(the name defined by the phi-node for those definitions). as
    //long as we get the last definition in the block we should be ok
    SparseSet_Clear(lrset);
    Block_ForAllInstsReverse(inst, blk)
    {
      //go in reverse because we want the last def that we see to be
      //the orig_name for the live range, this must be so because we
      //use that name in the use-def chains to decide where to put a
      //store for the defs of a live range
      debug("processing inst:\n%s", Debug::StringOfInst(inst));
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2OrigLRID(*reg);
          //better not have two definitions in the same block for the
          //same live range
          assert_same_orig_name(lrid, *reg, lrset, blk); 
          AddLiveUnitOnce(lrid, blk, lrset, *reg);
          debug("(def) %d (lrid) as r%d", lrid,*reg);
        }

        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2OrigLRID(*reg);
          AddLiveUnitOnce(lrid, blk, lrset, *reg);
          debug("(use) %d (lrid) as r%d", lrid,*reg);
        }
      } 
    }

    //Now add in the live_out set to the variables that include this
    //block in their live range
    Liveness_Info info;
    info = SSA_live_out[bid(blk)];
    for(unsigned int j = 0; j < info.size; j++)
    {
      //add block to each live range
      lrid = SSAName2OrigLRID(info.names[j]);
      AddLiveUnitOnce(lrid, blk, lrset, info.names[j]);
      debug("(liveout) %d (lrid) as r%d", lrid, info.names[j]);
    }

    //now that we have the full set of lrids that need to include this
    //block we can add the live units to the live ranges and update
    //the interference graph
    Unsigned_Int v, i;
    debug("LIVE SIZE: %d\n", SparseSet_Size(lrset));
    SparseSet_ForAll(v, lrset)
    {
      lr = live_ranges[v];

      //update the interference lists
      SparseSet_ForAll(i, lrset)
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
}

/*
 *============================
 * CreateLiveRanges()
 *============================
 * Allocates space for initial live ranges and sets default values.
 *
 ***/
void CreateLiveRanges(Arena arena, Unsigned_Int num_lrs)
{
  using Chow::live_ranges;
  using Chow::live_units;

  //initialize LiveRange class
  LiveRange::Init(arena, num_lrs);

  //create initial live ranges
  live_ranges.resize(num_lrs, NULL); //allocate space for live ranges
  live_units.resize(block_count+1);
  for(unsigned int lrid = 0; lrid < num_lrs; lrid++) 
  {
    LiveRange* lr = 
      new LiveRange(RegisterClass::InitialRegisterClassForLRID(lrid), 
                    lrid,
                    Mapping::LiveRangeDefType(lrid),
                    num_lrs);

    //initialize blockmap here since there should only be one tied to
    //the original live range that is shared by all live ranges split
    //from this one
    lr->blockmap = new std::map<unsigned int, LiveRange*>;
    lr->splits = new std::vector<LiveRange*>;
    live_ranges[lrid] = lr;
  }
}

//make sure that if we have already added a live unit for this lrid
//that the original name matches this name. this is important for the
//rewriting step, but this check does not need to be made when adding
//from the live_out set since those names may be different but it is
//ok because they occur in a different block
void assert_same_orig_name(LRID lrid, Variable v, SparseSet set,
Block* b)
{
  if(SparseSet_Member(set,lrid))
  {
    LiveUnit* unit =  Chow::live_ranges[lrid]->LiveUnitForBlock(b);
    //debug("already present: %d, orig_name: %d new_orig: %d  block: %s (%d)", 
   //                       lrid, unit->orig_name, v, bname(b), bid(b));
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
AddLiveUnitOnce(LRID lrid, Block* b, SparseSet lrset, Variable orig_name)
{
  //debug("ADDING: %d BLOCK: %s (%d)", lrid, bname(b), bid(b));
  LiveUnit* new_unit = NULL;
  if(!SparseSet_Member(lrset, lrid))
  {
    bool do_add = true;
    LiveRange* lr = Chow::live_ranges[lrid];
    if(Chow::local_names[orig_name])
    {
      lr->is_local = true;
      do_add = Params::Algorithm::allocate_locals;
    }
    if(do_add)
    {
      SparseSet_Insert(lrset, lrid);
      Stats::BBStats bbstat = Stats::GetStatsForBlock(b, lr->id);
      new_unit = lr->AddLiveUnitForBlock(b, orig_name, bbstat);
      Chow::live_units[bid(b)].push_back(new_unit);
    }
    //block map must be initialized regardless of local or not
    (*(lr->blockmap))[bid(b)] = lr; 
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
  using Stats::chowstats;
  using Params::Algorithm::spill_instead_of_split;
  using Params::Algorithm::split_limit;

  //make a copy of the interference list as a worklist since splitting
  //may add and remove items to the original interference list
  LRVec worklist(lr->fear_list->size());
  if(lr->fear_list->size() > 0)
  {
    copy(lr->fear_list->begin(), lr->fear_list->end(), worklist.begin());
  }

  //our neighbors are the live ranges we interfere with
  while(!worklist.empty())
  {
    LiveRange* intf_lr = worklist.back(); worklist.pop_back();
    //only check allocation candidates, may not be a candidate if it
    //has already been assigned a color
    if(!(intf_lr->is_candidate)) continue;

    //split if no registers available
    if(ShouldSplitLiveRange(intf_lr))
    {
      debug("Need to split LR: %d", intf_lr->id);
      if((split_limit && (int)chowstats.cSplits >= split_limit) ||
          spill_instead_of_split ||
          intf_lr->IsEntirelyUnColorable())
      {
        debug("LR: %d is uncolorable - will not split", intf_lr->id);
        //delete this live range from the interference graph. update
        //the constrained lists since live ranges may shuffle around
        //after we delete this from the interference graph
        intf_lr->MarkNonCandidateAndDelete(); chowstats.cSpills++;
        UpdateConstrainedListsAfterDelete(intf_lr, constr_lr, unconstr_lr);
      }
      else //try to split
      {
        //Split() returns the new live range that we know is colorable
        LiveRange* newlr = intf_lr->Split();
        //add new liverange to list of live ranges
        Chow::live_ranges.push_back(newlr);
        assert(newlr->id == (Chow::live_ranges.size() - 1));
        debug("ADDED LR: %d", newlr->id);

        if(intf_lr->IsZeroOccurrence())
        {
          intf_lr->MarkNonCandidateAndDelete(); chowstats.cZeroOccurrence++;
          UpdateConstrainedListsAfterDelete(intf_lr, constr_lr, unconstr_lr);
          if(Params::Algorithm::optimistic && !newlr->IsConstrained())
            PullNodeFromGraph(newlr, constr_lr);
          else
            AddToCorrectConstrainedList(constr_lr, unconstr_lr, newlr);
        }
        else
        {
          //make sure constrained lists are up-to-date after split
          UpdateConstrainedLists(newlr, intf_lr, constr_lr, unconstr_lr);
          //if the remainder of the live range we just split from
          //interferes with the live range we assigned a color to then 
          //add it to the work list because it may need to be split more
          if(intf_lr->InterferesWith(lr))
          {
            if(!Params::Algorithm::optimistic  ||
               (Params::Algorithm::optimistic && !intf_lr->simplified))
            {
                    worklist.push_back(intf_lr);
            }
          }
        }

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
  //if optimistic
  //if origlr is no longer constraiend then remove from constraiend
  //and pull the node out of the graph. next check what needs to be
  //done with the new live range.
  if(Params::Algorithm::optimistic)
  {
    if(!origlr->IsConstrained())
    {
      constr_lrs->erase(origlr);
      PullNodeFromGraph(origlr, constr_lrs);
    }
    if(newlr->IsConstrained())
    {
      constr_lrs->insert(newlr);
    }
    else
    {
      PullNodeFromGraph(newlr, constr_lrs);
    }

    return; //exit early
  }

  //update constrained lists, only need to update for any live range
  //that interferes with both the new and original live range because
  //those are the only live ranges that could have changed status
  /*LRSet updates;
  set_intersection(newlr->fear_list->begin(), newlr->fear_list->end(),
                   origlr->fear_list->begin(), origlr->fear_list->end(),
                   inserter(updates,updates.begin()));
  for(LRSet::iterator i = updates.begin(); i != updates.end(); i++)
  */
  for(LazySet::iterator i = newlr->fear_list->begin(); 
      i != newlr->fear_list->end(); 
      i++)
  {

    LiveRange* lr = *i;
    if(origlr->fear_list->member(lr))
    {
      //skip anyone that has already been assigned a color
      if(!lr->is_candidate) continue;

      if(lr->IsConstrained())
      {
        debug("ensuring LR: %d is in constr", lr->id);
        unconstr_lrs->erase(lr);
        constr_lrs->insert(lr);
      }
    }
  }

  //also, need to update the new and original live range positions
  AddToCorrectConstrainedList(constr_lrs, unconstr_lrs, newlr);
  if(!origlr->IsConstrained())
  {
    debug("shifting LR: %d to unconstrained", origlr->id);
    constr_lrs->erase(origlr);
    unconstr_lrs->insert(origlr);
  }
}

/*
 *====================================
 * UpdateConstrainedListsAfterDelete()
 *====================================
 * Updates the lists after a live range is deleted from the
 * interference graph to ensure that the live range is in the correct
 * bucket.
 ***/
void UpdateConstrainedListsAfterDelete(LiveRange* lr,
                                        LRSet* constr_lrs, 
                                        LRSet* unconstr_lrs)
{
  for(LazySet::iterator i = lr->fear_list->begin(); 
      i != lr->fear_list->end(); 
      i++)
  {
    LiveRange* fear_lr = *i;
    //skip anyone that has already been assigned a color
    if(!(fear_lr)->is_candidate) continue;
    if((fear_lr)->IsConstrained())
    {
      if(unconstr_lrs->erase(fear_lr))
        constr_lrs->insert(fear_lr);
    }
    else
    {
      if(constr_lrs->erase(fear_lr)) 
      {
        if(Params::Algorithm::optimistic)
          PullNodeFromGraph(fear_lr, constr_lrs);
        else
          unconstr_lrs->insert(fear_lr);
      }
    }
  }
  constr_lrs->erase(lr);
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
  using Assign::HandleCopy;
  using Assign::UnEvict;
  using Assign::InitLocalAllocation;

  debug("allocation complete. renaming registers...");
  Assign::Init(Chow::arena);

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
    InitLocalAllocation(b);
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
        //treat copies special so that we don't load and then copy,
        //but rather just load into the dest if needed
        if(opcode_specs[(*op)->opcode].details & COPY)
        {
          HandleCopy(b, origInst, updatedInst, op, instUses, instDefs);
        }
       else
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
      } 
      UnEvict(updatedInst);
    }
    //make available the tmp regs used in this block
    ResetFreeTmpRegs(b);
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
void HandleCopyDefs();
void MoveLoadsAndStores()
{
  using namespace std; //for list, pair
  typedef list<MovedSpillDescription>::iterator LI;
  InitCFGTools(Chow::arena); //for adding edges

  //first handle any copy-def spills so that they are turned into
  //normal copies and stores placed on the edges as needed
  HandleCopyDefs();

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
        //3) we are inserting a copy and the successor has more than
        //one pred
        Boolean need_split = FALSE;
        for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
        {
          MovedSpillDescription msd = (*ee);
          switch(msd.spill_type){
            case STORE_SPILL:
              if(Block_PredCount(edg->succ) > 1)
              {
                need_split = TRUE;
              }
              break;
            case LOAD_SPILL:
              if(Block_SuccCount(edg->pred) > 1)
              {
                need_split = TRUE;
                break;
              }
            case COPY_SPILL:
            {
              //in the case of a copy, we always split the edge
              //because we want the copy to come after all of the
              //stores and the easiest way to do that is split the
              //edge and insert the copy in the same manner as a ld
              need_split = true;
              break;
            }
            default:
              error("got invalid spill type: %d", msd.spill_type);
              assert(false);
          }
          if(need_split) break; //out of loop
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
          if(msd.mreg == -1) {
            msd.mreg = Assign::GetMachineRegAssignment(msd.orig_blk, msd.lr->orig_lrid);
          }
            
          switch(msd.spill_type) {
            case STORE_SPILL:
            {
              debug("moving store from %s to %s for lrid: %d_%d",
                    bname(msd.orig_blk), bname(blkST),
                    msd.lr->orig_lrid, msd.lr->id);
              Spill::InsertStore(msd.lr, Block_FirstInst(blkST),
                                msd.mreg, Spill::REG_FP,
                                BEFORE_INST);
              break;
            }
            case LOAD_SPILL:
            {
              debug("moving load from %s to %s for lrid: %d_%d",
                    bname(msd.orig_blk), bname(blkLD),
                    msd.lr->orig_lrid, msd.lr->id);
              LiveRange* lr = msd.lr;
              if(Params::Algorithm::rematerialize)
              {
                if(lr->blockmap->find(bid(edg->pred)) != lr->blockmap->end())
                {
                  debug("remat OPPORTUNITY");
                  LiveRange* lrPred = (*lr->blockmap)[bid(edg->pred)];
                  if(lrPred->rematerializable)
                  {
                    debug("remat SUCCESS");
                    lr = lrPred;
                  }
                }
              }
              Spill::InsertLoad(lr, Block_LastInst(blkLD), 
                                msd.mreg, Spill::REG_FP);
              break;
            }
            case COPY_SPILL:
            {
              debug("inserting copy from %s to %s for lrid: %d",
                    bname(msd.orig_blk), bname(blkLD), msd.lr->id);
              //error("LOOKING FOR THIS"); assert(false);
              Spill::InsertCopy(
                msd.lr, msd.lr_dest,     Block_LastInst(blkLD),
                msd.cp_src, msd.cp_dest, BEFORE_INST
              );
              break;
            }
            default:
              error("unknown spill type: %d", msd.spill_type);
              assert(false);
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

void HandleCopyDefs()
{
  typedef std::list<MovedSpillDescription>::iterator LI;
  Block* blk;

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
        //3) we are inserting a copy and the successor has more than
        //one pred
        for(LI ee = edg->edge_extension->spill_list->begin(); 
               ee != edg->edge_extension->spill_list->end(); 
               ee++)
        {
          MovedSpillDescription msd = *ee;
          if(msd.spill_type == COPYDEF_SPILL)
          {
            //imagine we have a def in the succesor block and look at
            //all the blocks that def would reach that belong to the
            //succssor live range.
            LiveRange* lr = msd.lr_dest;

            //interset the blocks that the def would reach with this
            //live ranges blocks to limit our scope
            VectorSet_Intersect(
              LiveRange::tmpbbset, 
              Reach::ReachableBlocks(edg->succ),
              lr->bb_list
            );

            //check all the blocks the def would reach to see if a
            //store is necessary
            for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
            {
              LiveUnit* unit = *it;
              if(!VectorSet_Member(LiveRange::tmpbbset, bid(unit->block)))
                continue;

              //check all the succesor blocks to find any exits of the
              //live range and insert a store if they are live in at
              //the successor block
              Edge* eSucc;
              Block_ForAllSuccs(eSucc, unit->block)
              {
                Block* blkSucc = eSucc->succ;
                if(!lr->ContainsBlock(blkSucc))
                { 
                  //make sure that there is not already a store for
                  //this live range on this edge
                  bool existing_store = false;
                  if(eSucc->edge_extension)
                  {
                    for(LI eeSucc = eSucc->edge_extension->spill_list->begin(); 
                        eeSucc != eSucc->edge_extension->spill_list->end(); 
                        eeSucc++)
                    {
                      if(eeSucc->lr->orig_lrid == lr->orig_lrid &&
                         eeSucc->spill_type == STORE_SPILL)
                      {
                        existing_store = true; break;
                      }
                    }
                  }
                  if(!existing_store && LiveIn(lr->orig_lrid, blkSucc))
                  {
                    MovedSpillDescription msd_store = {0};
                    msd_store.lr = lr;
                    msd_store.spill_type = STORE_SPILL;
                    msd_store.orig_blk = unit->block;
                    msd_store.mreg = msd.cp_dest;
                    Chow::Extensions::AddEdgeExtensionNode(eSucc,msd_store);
                  }
                }
              }
            }
            //finally we change the copydef to be a simple copy now
            //that the def part has been handled
            ee->spill_type = COPY_SPILL;
          }//handle copydef
        }//ForAll MovedSpills
      }//has a spill
    }//ForAllSuccs
  }//ForAllBlocks
}
bool LiveIn(LRID orig_lrid, Block* blk)
{
  Liveness_Info info = SSA_live_in[bid(blk)];
  for(LOOPVAR j = 0; j < info.size; j++)
  {
    if(orig_lrid == info.names[j])
    {
      return true;
    }
  }
  return false;
}

bool ShouldSplitLiveRange(LiveRange* lr)
{
  debug("should split LR: %d ?", lr->id);
  if(Params::Algorithm::optimistic && lr->simplified) return false;
  return (*Chow::Heuristics::when_to_split_strategy)(lr);
}

inline void AddToCorrectConstrainedList(LRSet* constr_lrs, 
                                        LRSet* unconstr_lrs,
                                        LiveRange* lr)
{
  if(lr->IsConstrained())
  {
    constr_lrs->insert(lr);
    debug("Constrained LR:    %d", lr->id);
  }
  else
  {
    unconstr_lrs->insert(lr);
    debug("UN-Constrained LR:    %d", lr->id);
  }
}

/****************************************************************
 *                     LOCAL NAMES DEBUG STUFF
 ****************************************************************/
void DumpLocals()
{ 
  using Chow::live_ranges;
  using Chow::local_names;

  printf("LIVE RANGE\n");
  printf("LIVE RANGE LOCAL NAMES\n");
  printf("LIVE RANGE\n");
  int sum = 0;
  for(uint i = 0; i < live_ranges.size(); i++)
  {
    LiveRange* lr = live_ranges[i];
    if(lr->units->size() == 1)
    {
      sum++;
      printf("LR: %d\n", live_ranges[i]->id);
    }
  }
  printf("---------------------\n");
  printf("LR Total:  %d\n", sum);

  printf("DOM\n");
  printf("DOM TREE LOCAL NAMES\n");
  printf("DOM\n");
  sum = 0;
  for(uint i = 0; i < local_names.size(); i++)
  {
    Variable ssa_name = i;
    if(local_names[ssa_name])
    {
      sum++;
      printf("LR: %d, SSA: %d\n", 
        Mapping::SSAName2OrigLRID(ssa_name), ssa_name);
    }
  }
  printf("---------------------\n");
  printf("DOM Total: %d\n", sum);
  exit(EXIT_SUCCESS);
}

void CountLocals()
{
  using Chow::live_ranges;
  typedef unsigned int uint;
  std::map<Block*, int> lcnt;
  for(uint i = 0; i < live_ranges.size(); i++)
  {
    LiveRange* lr = live_ranges[i];
    if(lr->units->size() == 1)
    {
      lcnt[lr->units->front()->block]++;
    }
  }

  typedef std::map<Block*, int>::iterator MI;
  int sum = 0;
  for(MI i = lcnt.begin(); i != lcnt.end(); i++)
  {
    printf("%s: %d\n", bname(i->first), i->second);
    sum += i->second;
  }
  printf("---------------------\n");
  printf("Total: %d\n", sum);
  exit(EXIT_SUCCESS);
}


/****************************************************************
 *                     OPTIMISTIC CHOW
 ****************************************************************/
void SimplifyGraph(LRSet* constr_lrs);
void ColorFromStack();
void PullNodesFromGraph(std::list<LiveRange*>&, LRSet* constr_lrs, LRSet* init=NULL);
void SeparateConstrainedLiveRanges(LRSet* constr_lrs, LRSet* unconstr_lrs)
{
  using Chow::live_ranges;

  if(Params::Algorithm::optimistic)
  {
    SimplifyGraph(constr_lrs);
  }
  else
  {
    // skipping lrid 0 (frame lr)
    for(LRVec::size_type i = 1; i < live_ranges.size(); i++)
    {
      LiveRange* lr = live_ranges[i];
      if(Params::Algorithm::allocate_locals || !lr->is_local)
        AddToCorrectConstrainedList(constr_lrs, unconstr_lrs, lr);
      else
        lr->MarkNonCandidateAndDelete();
    }
  }
}

void ColorUnconstrained(LRSet* unconstr_lrs)
{
  using Params::Algorithm::allocate_all_unconstrained;

  if(Params::Algorithm::optimistic)
  {
    ColorFromStack();
  }
  else
  {
    for(LRSet::iterator i = 
        unconstr_lrs->begin(); i != unconstr_lrs->end(); i++)
    {
      LiveRange* lr = *i;

      debug("choose color for unconstrained LR: %d", lr->id);
      assert(lr->is_candidate);
      if(allocate_all_unconstrained || (lr->GetPriority() > 0))
      {
        lr->AssignColor();
        debug("LR: %d is given color:%d", lr->id, lr->color);
      }
      else
      {
        debug("LR: %d is has bad priority: %.3f, no color given",
              lr->id, lr->priority);
        lr->MarkNonCandidateAndDelete();
        Stats::chowstats.cSpills++;
      }
    }
  }
}

void SimplifyGraph(LRSet* constr_lrs)
{
  using Chow::live_ranges;
  using Chow::color_stack;

  std::list<LiveRange*> worklist;
  LRSet pulled;

  //pull out initial unconstrained live ranges
  for(LRVec::size_type i = 1; i < live_ranges.size(); i++)
  {
    LiveRange* lr = live_ranges[i];
    if(!lr->IsConstrained())
    {
      if(Params::Algorithm::allocate_locals || !lr->is_local)
      {
        debug("initial unconstrained LR: %d", lr->id);
        worklist.push_back(lr);
        pulled.insert(lr);
      }
      else
      {
        lr->MarkNonCandidateAndDelete();
      }
    }
  }

  //note that the nodes are pulled from the graph
  PullNodesFromGraph(worklist, constr_lrs, &pulled);

  //fill in constrained list with any node not removed
  for(LRVec::size_type i = 1; i < live_ranges.size(); i++)
  {
    LiveRange* lr = live_ranges[i];
    if(lr->IsConstrained())
    {
      debug("CONSTR: LR: %d", lr->id);
      constr_lrs->insert(lr);
    }
  }
}

void PullNodeFromGraph(LiveRange* lr, LRSet* constr_lrs)
{
  std::list<LiveRange*> worklist;
  worklist.push_back(lr);
  PullNodesFromGraph(worklist, constr_lrs);
}

void PullNodesFromGraph(
 std::list<LiveRange*>& worklist,
 LRSet* constr_lrs,
 LRSet* initial_pulled
)
{
  typedef LRSet::iterator LRSI;
  typedef std::list<LiveRange*>::iterator LI;
  static LRSet pulled;
  if(initial_pulled) { pulled = *initial_pulled;}
  else 
  {
    for(LI i = worklist.begin(); i != worklist.end();) 
    {
      debug("checking pull worklist LR: %d", (*i)->id);
      if((*i)->simplified)
      {
        debug("lr has already been pulled from the graph");
        LI del = i++;
        worklist.erase(del);
      }
      else {pulled.insert(*i); i++;}
    }
  }

  //pull out live ranges that become unconstrained when others are
  //pulled from the live range becuase their degree goes down
  while(!worklist.empty())
  {
    //remove from graph and add any neighbors that can now be removed
    LiveRange* lr = worklist.back(); worklist.pop_back();
    debug("pulling LR: %d from graph", lr->id);
    assert(!lr->simplified);
    Chow::color_stack.push(lr);
    lr->simplified = true;

    //pull any neighbors that become unconstrained when this node is
    //removed
    for(LazySet::iterator it = lr->fear_list->begin(); it != lr->fear_list->end(); it++)
    {
      LiveRange* fear_lr = *it;
      fear_lr->simplified_neighbor_count++;
      fear_lr->simplified_width += RegisterClass::RegWidth(lr->type);

      if(fear_lr->is_candidate &&
        !fear_lr->IsConstrained() && 
        (pulled.find(fear_lr) == pulled.end()))
      {
        debug("pulling additional unconstrained LR: %d", fear_lr->id);
        pulled.insert(fear_lr);
        constr_lrs->erase(fear_lr);
        worklist.push_back(fear_lr);
        Stats::chowstats.cFoundOptimist++;
      }
    }
  }
}

void ColorFromStack()
{
  using Chow::color_stack;
  using Params::Algorithm::allocate_all_unconstrained;
  while(!color_stack.empty())
  {
    LiveRange* lr = color_stack.top(); color_stack.pop();
    debug("assigning color for unconstrained LR: %d", lr->id);
    if(Coloring::NumColorsAvailable(lr, lr->forbidden) > 0)
    {
      if(allocate_all_unconstrained || (lr->GetPriority() > 0))
      {
        lr->AssignColor();
      }
      else
      {
        lr->MarkNonCandidateAndDelete();
        Stats::chowstats.cSpills++;
      }
    }
    else
    {
      //have to spill
      debug("LR: %d could not be colored, optimist failed", lr->id);
      lr->MarkNonCandidateAndDelete();
      Stats::chowstats.cSpills++;
      Stats::chowstats.cSpilledOptimist++;
    }
  }
}
}

/*
 *===========
 * Function()
 *===========
 *
 ***/

