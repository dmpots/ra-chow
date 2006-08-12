/*====================================================================
 * 
 *====================================================================
 * $Id: live_range.c 170 2006-08-02 01:14:10Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/live_range.c $
 ********************************************************************/

#include <Shared.h>
#include <SSA.h> /* for traversing def-use chains */
#include <assert.h>
#include <algorithm>
#include <queue>
#include <iterator>
#include <stdlib.h>
#include <math.h>

#include "live_range.h"
#include "debug.h"
#include "chow.h"
#include "util.h"
#include "ra.h" //for computing loop nesting depth

/* local variables */
static Arena lr_arena;
static const float UNDEFINED_PRIORITY = 666;
static const float MIN_PRIORITY = -3.4e38;
static const Color UNDEFINED_COLOR = -1u;
static const Color NO_COLOR = -2u;
static VectorSet* used_reg_map;
static VectorSet all_regs;
static VectorSet no_regs;
static VectorSet tmpset;
static VectorSet tmpbbset;
static VectorSet no_bbs;
static MemoryLocation* lr_mem_map;

/* globals */
Unsigned_Int liverange_count;

/* local functions */
static void Def_CollectUniqueUseNames(Variable, std::list<Variable>&);
static Priority LiveUnit_ComputePriority(LiveUnit*);

//iterate through interference list. if stmt used as a cheat for its
//side effect 
#define LiveRange_ForAllFears(lr, f_lr) \
for (LRSet::iterator i = (lr->fear_list->begin()); \
     i != (lr->fear_list->end());\
     i++) \
   if(((f_lr) = *i) || TRUE) 

//copy(lr->fear_list->begin(), lr->fear_list->end(), _tmpset.begin());
#define LiveRange_ForAllFearsCopy(lr, f_lr)\
LRSet _tmpset(*(lr->fear_list));\
for(LRSet::iterator i = _tmpset.begin();\
    i != _tmpset.end();\
    i++)\
   if(((f_lr) = *i) || TRUE) 
    
#define LiveRange_ForAllUnits(lr, unit) \
for(std::list<LiveUnit*>::iterator i = (lr)->units->begin(); \
    i != (lr)->units->end(); \
    i++) \
   if(((unit) = *i) || TRUE) 

#define LiveRange_NextId (liverange_count++);

/* Def_ForAllUseBlocks(Variable v, Block* b) */
#define Def_ForAllUseBlocks(v, b)\
Chains_List* _runner;\
Chain_ForAllUses(_runner, v)\
  if(((b = _runner->block)) || TRUE)




/*
 *============================
 * LiveRange_AllocLiveRanges()
 *============================
 * Allocates space for initial live ranges and sets default values.
 *
 ***/
void LiveRange_AllocLiveRanges(Arena arena, 
                               LRList &lrs, 
                               Unsigned_Int num_lrs)
{
  LOOPVAR i;
  lr_arena = arena;
  liverange_count = num_lrs;

  lrs.resize(liverange_count, NULL); //allocate space
  for(i = 0; i < liverange_count; i++) //allocate each live range
  {
    lrs[i] = LiveRange_Create(arena);
    lrs[i]->orig_lrid = i;
    lrs[i]->id = i;
  }

  //allocate a mapping from blocks to a set of used registers
  used_reg_map = (VectorSet*)
       Arena_GetMemClear(arena,sizeof(VectorSet)*(block_count+1));
  Block* b;
  ForAllBlocks(b)
  {
    i = id(b);
    used_reg_map[i] = VectorSet_Create(arena, mRegisters);
    VectorSet_Clear(used_reg_map[i]);
  } 

  //set where all register bits are set
  all_regs  = VectorSet_Create(arena, mRegisters);
  no_regs  = VectorSet_Create(arena, mRegisters);
  VectorSet_Clear(all_regs);
  VectorSet_Clear(no_regs);
  VectorSet_Complement(all_regs, all_regs);

  //temporary set used for various live range computations
  tmpset = VectorSet_Create(arena, mRegisters);
  tmpbbset = VectorSet_Create(arena, block_count+1);
  no_bbs = VectorSet_Create(arena, block_count+1);
  VectorSet_Clear(no_bbs);


  //allocate a mapping from live range to a memory location. this is
  //used for spilling
  lr_mem_map = (MemoryLocation*) Arena_GetMemClear(arena, 
       sizeof(MemoryLocation) * liverange_count);
  for(i = 0; i < liverange_count; i++)
    lr_mem_map[i] = MEM_UNASSIGNED;

  unsigned int seed = time(NULL);
  //seed = 1154486980;
  srand(seed);
  debug("SRAND: %u", seed);
  //srand(74499979);
  //srand(44979);
}

/*
 *============================
 * LiveRange_Create()
 *============================
 * Allocates and initializes a live range 
 ***/
LiveRange* LiveRange_Create(Arena arena)
{
  LiveRange* lr;
  lr = (LiveRange*)Arena_GetMemClear(arena, sizeof(LiveRange));
  lr->orig_lrid = (LRID)-1;
  lr->id = 0;
  lr->priority = UNDEFINED_PRIORITY;
  lr->color = UNDEFINED_COLOR;
  lr->bb_list = VectorSet_Create(arena, block_count+1);
  lr->fear_list = new std::set<LiveRange*, LRcmp>;
  lr->units = new std::list<LiveUnit*>;
  lr->forbidden = VectorSet_Create(arena, mRegisters);
  lr->is_candidate  = TRUE;
  lr->type = NO_DEFS;
  lr->tag = TAG_UNASSIGNED;

  return lr;
}

/*
 *=============================
 * AddLiveUnitOnce()
 *=============================
 *
 * adds the block to the live range, but only once depending on the
 * contents of the *lrset*
 ***/
LiveUnit* AddLiveUnitOnce(LRID lrid, 
                          Block* b, 
                          VectorSet lrset, 
                          Variable orig_name)
{
  debug("ADDING: %d BLOCK: %s (%d)", lrid, bname(b), id(b));
  LiveUnit* new_unit = NULL;
  if(!VectorSet_Member(lrset, lrid))
  {
    LiveRange* lr = live_ranges[lrid];
    VectorSet_Insert(lrset, lrid);
    new_unit = LiveRange_AddLiveUnitBlock(lr, b);
    new_unit->orig_name = orig_name;
  }

  return new_unit;
} 
 

/*
 *=============================
 * LiveRange_AddLiveUnitBlock()
 *=============================
 *
 ***/
LiveUnit* LiveRange_AddLiveUnitBlock(LiveRange* lr, Block* b)
{
  LiveUnit* unit = LiveUnit_Alloc(lr_arena);

  //assign initial values
  unit->block = b;
  BB_Stat stat = bb_stats[id(b)][lr->id];
  unit->uses = stat.uses;
  unit->defs = stat.defs;
  unit->start_with_def = stat.start_with_def;
  
  LiveRange_AddLiveUnit(lr, unit);
  return unit;
}

/*
 *=============================
 * LiveRange_AddLiveUnit()
 *=============================
 *
 ***/
LiveUnit* LiveRange_AddLiveUnit(LiveRange* lr, LiveUnit* unit)
{
  LiveRange_AddBlock(lr, unit->block);

  lr->units->push_back(unit);
  return unit;
}

/*
 *=============================
 * LiveRange_LiveUnitForBlock()
 *=============================
 *
 ***/
LiveUnit* LiveRange_LiveUnitForBlock(LiveRange* lr, Block* b)
{
  LiveUnit* unit;
  LiveRange_ForAllUnits(lr, unit)
  {
    if(id(unit->block) == id(b))
    {
      return unit;
    }
  }

  return NULL;
}


/*
 *=============================
 * LiveRange_RemoveLiveUnitBlock()
 *=============================
 *
 ***/
void LiveRange_RemoveLiveUnitBlock(LiveRange* lr, Block* b)
{
  LiveUnit* remove_unit = NULL;
  remove_unit = LiveRange_LiveUnitForBlock(lr, b);

  if(remove_unit != NULL)
  {
    LiveRange_RemoveLiveUnit(lr, remove_unit);
  }
}

/*
 *=============================
 * LiveRange_RemoveLiveUnitBlock()
 *=============================
 *
 ***/
void LiveRange_RemoveLiveUnit(LiveRange* lr, LiveUnit* unit)
{
  //remove from the basic block set
  VectorSet_Delete(lr->bb_list, id(unit->block));

  std::list<LiveUnit*>::iterator elem;
  elem = find(lr->units->begin(), lr->units->end(), unit);
  if(elem != lr->units->end())
  {
    lr->units->erase(elem);
  }
}


/*
 *=============================
 * LiveRange_InterferesWith()
 *=============================
 *
 * Used to determine if two live ranges interfere
 * true - if the live ranges interfere
 ***/
Boolean LiveRange_InterferesWith(LiveRange* lr1, LiveRange* lr2)
{
  VectorSet_Intersect(tmpbbset, lr1->bb_list, lr2->bb_list);
  return (!VectorSet_Equal(no_bbs, tmpbbset));
}


/*
 *=============================
 * LiveRange_AddInterference()
 *=============================
 *
 ***/
void LiveRange_AddInterference(LiveRange* lr1, LiveRange* lr2)
{
  //lr2 --interfer--> lr1
  LRSet_Add(lr1->fear_list, lr2);

  //lr1 --interfer--> lr2
  LRSet_Add(lr2->fear_list, lr1);
}

/*
 *=============================
 * LRSet_Add()
 *=============================
 *
 ***/
void LRSet_Add(LRSet* lrs, LiveRange* lr)
{
  lrs->insert(lr);
}


/*
 *=============================
 * LiveRange_RemoveInterference()
 *=============================
 *
 ***/
void LiveRange_RemoveInterference(LiveRange* lr1, LiveRange* lr2)
{
    //remove lr2 from lr1
    LRSet_Remove(lr1->fear_list, lr2);

    //remove lr1 from lr2
    LRSet_Remove(lr2->fear_list, lr1);
}

/*
 *=============================
 * LRSet_Remove()
 *=============================
 *
 ***/
void LRSet_Remove(LRSet* lrs, LiveRange* lr)
{
  lrs->erase(lr);
}




/*
 *=========================
 * LiveRange_Constrained()
 *=========================
 *
 ***/
Boolean LiveRange_Constrained(LiveRange* lr)
{
  return lr->fear_list->size() >= mRegisters;
}


/*
 *=======================================
 * LiveRange_ComputePriority()
 *=======================================
 *
 ***/
Priority LiveRange_ComputePriority(LiveRange* lr)
{
  //return .1*(rand() % 100);
  LiveUnit* lu;
  Priority pr = 0.0;
  Unsigned_Int clu = 0; //count of live units
  LiveRange_ForAllUnits(lr, lu)
  {
    pr += LiveUnit_ComputePriority(lu) * 
          pow(wLoopDepth, depths[id(lu->block)]);
    clu++;
  }
  return pr/clu;
}

/*
 *=======================================
 * LiveUnit_ComputePriority()
 *=======================================
 *
 ***/
Priority LiveUnit_ComputePriority(LiveUnit* lu)
{
  return
    mLDSave  * lu->uses 
  + mSTRSave * lu->defs 
  - mMVCost  * (lu->need_store + lu->need_load);
}

/*
 *=======================================
 * LiveRange_UnColorable()
 *=======================================
 * returns true if the live range is uncolorable
 *
 ***/
Boolean LiveRange_UnColorable(LiveRange* lr)
{
  //a live range is uncolorable when all registers have been used
  //throughout the entire length of the live range (i.e. each live
  //unit has no available registers). we need to have a mapping of
  //basic blocks to used registers for that block in order to see
  //which registers are available.
  
  LiveUnit* unit;
  LiveRange_ForAllUnits(lr, unit)
  {
    //must have a free register where we have a def or a use
    if(unit->defs > 0 || unit->uses > 0)
    {
      if(!RegisterSet_Full(used_reg_map[id(unit->block)]))
      {
        return false;
      }
    }
  }

  debug("LR: %d is UNCOLORABLE", lr->id);
  return true;
}

/*
 *=======================================
 * LiveRange_MarkNonCandidateAndDelete()
 *=======================================
 * mark the live range so that we know it is not a candidate for a
 * register and delete it from the interference graph.
 *
 ***/
void LiveRange_MarkNonCandidateAndDelete(LiveRange* lr)
{
  LiveRange* intf_lr;
  lr->color = NO_COLOR;
  lr->is_candidate = FALSE;

  debug("deleting LR: %d from interference graph", lr->id);
  LiveRange_ForAllFearsCopy(lr, intf_lr)
  {
    LiveRange_RemoveInterference(intf_lr, lr);
  }

  //clear the bb_list so that this live range will no longer interfere
  //with any other live ranges
  VectorSet_Clear(lr->bb_list);
}


/*
 *=======================================
 * LiveRange_AssignColor()
 *=======================================
 * assign an available color to a live range
 *
 ***/
void LiveRange_AssignColor(LiveRange* lr)
{
  //for now just pick the first available color
  VectorSet_Complement(tmpset, lr->forbidden);
  Color color = VectorSet_ChooseMember(tmpset);
  assert(color < mRegisters);
  lr->color = color;
  lr->is_candidate = FALSE; //no longer need a color
  debug("assigning color: %d to lr: %d", color, lr->id);

  //update the interfering live ranges forbidden set
  LiveRange* intf_lr;
  LiveUnit* unit;
  LiveRange_ForAllFears(lr, intf_lr)
  {
    VectorSet_Insert(intf_lr->forbidden, color);
    debug("adding color: %d to forbid list for LR: %d", color,
                                                        intf_lr->id)
  }

  //update the basic block taken set
  LiveRange_ForAllUnits(lr, unit)
  {
    assert(!VectorSet_Member(used_reg_map[id(unit->block)], color));
    VectorSet_Insert(used_reg_map[id(unit->block)], color);
    register_map[id(unit->block)][lr->orig_lrid] = color;

    if(unit->need_load)
    {
      LiveRange_InsertLoad(lr, unit);
    }
    if(unit->need_store)
    {
      LiveRange_InsertStore(lr, unit);
    }
  }
}

/*
 *============================
 * LiveRange_InsertLoad()
 *============================
 * Inserts a load for the live range in the given live unit. The load
 * is inserted for the original name since the rewriting process will
 * take care of changing the name to the allocated register.
 */
void LiveRange_InsertLoad(LiveRange* lr, LiveUnit* unit)
{
  Block* b = unit->block;
  debug("INSERTING LOAD: lrid: %d, block: %s, to: %d",
        lr->id, bname(unit->block), unit->orig_name);
  Insert_Load(lr->id, Block_FirstInst(b), unit->orig_name,
              GBL_fp_origname);
}

/*
 *============================
 * LiveRange_InsertStore()
 *============================
 * Inserts a store for the live range in the given live unit. The 
 * store is inserted for the original name since the rewriting 
 * process will take care of changing the name to the allocated
 * register.
 */
void LiveRange_InsertStore(LiveRange*lr, LiveUnit* unit)
{
  Block* b = unit->block;
  debug("INSERTING STORE: lrid: %d, block: %s, to: %d",
        lr->id, bname(unit->block), unit->orig_name);
  (void)
   Insert_Store(lr->id, Block_LastInst(b), unit->orig_name,
                 GBL_fp_origname, BEFORE_INST);
}


/*
 *============================
 * LiveRange_SplitNeighbors()
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
void LiveRange_SplitNeighbors(LiveRange* lr, 
                              LRSet* constr_lr,
                              LRSet* unconstr_lr)
{
  debug("BEGIN SPLITTING");

  //make a copy of the interference list as a worklist since splitting
  //may add and remove items to the original interference list
  LRList worklist(lr->fear_list->size());
  copy(lr->fear_list->begin(), lr->fear_list->end(), worklist.begin());

  //our neighbors are the live ranges we interfere with
  LiveRange* intf_lr;
  LRTuple lr_tup;
  while(!LRList_Empty(worklist))
  {
    intf_lr = LRList_Pop(&worklist);
    //only check allocation candidates, may not be a candidate if it
    //has already been assigned a color
    if(!(intf_lr->is_candidate)) continue;

    //split if no registers available
    if(!LiveRange_RegistersAvailable(intf_lr))
    {
      debug("Need to split LR: %d", intf_lr->id);
      if(LiveRange_UnColorable(intf_lr))
      {
        //delete this live range from the interference graph. we dont
        //need to update the constrained lists at this point because
        //deleting this live range should have no effect on whether
        //the live ranges it interferes with are constrained or not
        LiveRange_MarkNonCandidateAndDelete(intf_lr);
      }
      else //try to split
      {
        lr_tup = LiveRange_Split(intf_lr, constr_lr, unconstr_lr);

        //if the remainder of the live range we just split 
        //interferes with the live range we assigned a color to then 
        //add it to the work list because it may need to be split more
        if(LiveRange_InterferesWith(lr_tup.snd, lr))
          LRList_Add(&worklist, lr_tup.snd);

        debug("split complete for LR: %d", intf_lr->id);
        LiveRange_DDump(lr_tup.fst);
        LiveRange_DDump(lr_tup.snd);
      }
    }
  }
  debug("DONE SPLITTING");
} 

/*
 *============================
 * LiveRange_Split()
 *============================
 *
 * Does the majority of the work in splitting a live range. the live
 * range will be split into two live ranges. the new live range may
 * interfere with the live range we just assigned a color to, so we
 * pass the intereference list in case we need to add to it.
 * 
 * splitting the neighbors may shuffle them around on the constrained
 * and unconstrained lists so we pass them in for possible
 * modification.
 *
 * return value:
 * fst - the new live range carved out from the original
 * snd - the remainder of the original live range
 ***/
LRTuple LiveRange_Split(LiveRange* origlr,
                     LRSet* constr_lrs,
                     LRSet* unconstr_lrs)
{

  //create a new live range and initialize values
  LiveRange* newlr = LiveRange_SplitFrom(origlr);

  //chose the live unit that will start the new live range
  LiveUnit* startunit;
  LiveUnit* unit;
  startunit = LiveRange_ChooseSplitPoint(origlr);
  assert(startunit != NULL);

  debug("adding block: %s to  lr'", bname(startunit->block));
  LiveRange_TransferLiveUnit(newlr, origlr, startunit);
  LRList_Add(&live_ranges, newlr);
  debug("ADDED LR: %d, size: %d", newlr->id, (int)live_ranges.size());

  //keep a queue of successors that we may add to the new live range
  std::list<Block*> succ_list;
  succ_list.push_back(startunit->block);
  while(!succ_list.empty())
  {
    Edge* e;
    Block* b = succ_list.front();
    succ_list.pop_front();

    Block_ForAllSuccs(e, b)
    {
      Block* succ = e->succ;
      if((unit = LiveRange_IncludeInSplit(newlr, origlr, succ)) != NULL)
      {
        debug("adding block: %s to  lr'", bname(succ));
        LiveRange_TransferLiveUnit(newlr, origlr, unit);
        succ_list.push_back(succ); //explore the succs of this node
      }
    }
  }

  LiveRange_UpdateAfterSplit(newlr, origlr, constr_lrs, unconstr_lrs);
  
  LRTuple ret;
  ret.fst = newlr; ret.snd = origlr;
  return ret;
}


/*
 *================================
 * LiveRange_SplitFrom()
 *================================
 * Creates a new live range that will contain live unit split from the
 * passed in original live range 
 */
LiveRange* LiveRange_SplitFrom(LiveRange* origlr)
{
  LiveRange* newlr = LiveRange_Create(lr_arena); //lr'
  newlr->orig_lrid = origlr->orig_lrid;
  newlr->id = LiveRange_NextId;
  newlr->is_candidate = TRUE;
  newlr->type = origlr->type;
  newlr->tag = origlr->tag;

  //some sanity checks
  assert(origlr->color == UNDEFINED_COLOR);
  assert(origlr->is_candidate == TRUE);

  return newlr;
}


/*
 *================================
 * LiveRange_UpdateAfterSplit()
 *================================
 *
 */
void LiveRange_UpdateAfterSplit(LiveRange* newlr, 
                                LiveRange* origlr,
                                LRSet* constr_lrs,
                                LRSet* unconstr_lrs)
{

  //1. rebuild interferences of those live ranges that interfere with 
  //the original live range. work on a copy of the list because
  //rebuilding interferences may modify the fear_list of the origlr
  LiveRange* fearlr;
  LiveRange_ForAllFearsCopy(origlr, fearlr)
  {
    //update newlr interference
    if(LiveRange_InterferesWith(newlr, fearlr))
    {
      LiveRange_AddInterference(newlr, fearlr);
    }

    //update origlr interference
    if(!LiveRange_InterferesWith(origlr, fearlr))
    {
      LiveRange_RemoveInterference(origlr, fearlr);
    }
  }

  //update constrained lists, only need to update for any live range
  //that interferes with both the new and original live range because
  //those are the only live ranges that could have changed status
  LRSet updates;
  set_intersection(newlr->fear_list->begin(), newlr->fear_list->end(),
                origlr->fear_list->begin(), origlr->fear_list->end(),
                inserter(updates,updates.begin()));
  LRSet_UpdateConstrainedLists(&updates,constr_lrs,unconstr_lrs);
  //also, need to update the new and original live range positions
  //updates.insert(newlr);
  //updates.insert(origlr);
  if(LiveRange_Constrained(newlr))
  {
    LRSet_Add(constr_lrs, newlr);
  }
  else
  {
    LRSet_Add(unconstr_lrs, newlr);
  }
  if(!LiveRange_Constrained(origlr))
  {
    LRSet_Remove(constr_lrs, origlr);
    LRSet_Add(unconstr_lrs, origlr);
  }

  //the need_load and need_store flags actually depend on the
  //boundries of the live range so we must recompute them
  LiveRange_MarkLoadsAndStores(newlr);
  LiveRange_MarkLoadsAndStores(origlr);


  //removing live units means we need to update the forbidden list
  //only do this for the orig live range since the new live range
  //keeps track as it goes
  LiveUnit* unit;
  VectorSet_Clear(origlr->forbidden);
  LiveRange_ForAllUnits(origlr, unit)
  {
    unit = *i;
    VectorSet_Union(origlr->forbidden, 
                    origlr->forbidden,
                    used_reg_map[id(unit->block)]);
  }

  //reset the priorites on the split live ranges since they are no
  //longer current. they will be recomputed if needed
  newlr->priority = UNDEFINED_PRIORITY;
  origlr->priority = UNDEFINED_PRIORITY;
}

 
/*
 *============================
 * LiveRange_AddBlock()
 *============================
 *
 */
void LiveRange_AddBlock(LiveRange* lr, Block* b)
{
  VectorSet_Insert(lr->bb_list, id(b));
  VectorSet_Union(lr->forbidden, lr->forbidden, used_reg_map[id(b)]);
}


/*
 *============================
 * LiveRange_TransferLiveUnit()
 *============================
 *
 */ 
void LiveRange_TransferLiveUnit(LiveRange* to,
                           LiveRange* from,
                           LiveUnit* unit)
{
  LiveRange_AddLiveUnit(to, unit);
  LiveRange_RemoveLiveUnit(from, unit);
}

/*
 *============================
 * LiveRange_ChooseSplitPoint()
 *============================
 *
 */ 
LiveUnit* LiveRange_ChooseSplitPoint(LiveRange* lr)
{
  LiveUnit* unit = NULL;
  LiveUnit* startunit = NULL;
  LiveUnit* first = NULL;
  LiveUnit* startdefunit = NULL;

  LiveRange_ForAllUnits(lr, unit)
  {
    if(!RegisterSet_Full(used_reg_map[id(unit->block)]))
    {
      //only take it if there is a use
      if(unit->uses > 0)
        first = first ? first : unit;

      //prefer starting with a def
      if(unit->start_with_def)
      {
        startdefunit = unit;

        //and if its an entry point we cant do better than that
        if(LiveRange_EntryPoint(lr,startdefunit))
        {
          break;
        }
      }
    }
  }
  
  startunit = startdefunit ? startdefunit : first;
  assert(startunit != NULL);
  return startunit;
}

/*
 *============================
 * LiveRange_IncludeInSplit()
 *============================
 *
 */ 
LiveUnit* LiveRange_IncludeInSplit(LiveRange* newlr,
                                   LiveRange* origlr,
                                   Block* b)
{
  LiveUnit* unit = NULL;
  //we can include this block in the split if it does not max out the
  //forbidden set
  VectorSet_Union(tmpset, newlr->forbidden, used_reg_map[id(b)]);
  if(!RegisterSet_Full(tmpset))
  {
    unit = LiveRange_LiveUnitForBlock(origlr,b);
  }
  return unit;
}


/*
 *=================================
 * LiveRange_MarkLoadsAndStores
 *=================================
 * Calculates where to place the needed loads and stores in a live
 * range.
 */ 
void LiveRange_MarkLoadsAndStores(LiveRange* lr)
{
  LiveRange_MarkLoads(lr);
  LiveRange_MarkStores(lr);
}

/*
 *=================================
 * LiveRange_MarkLoads
 *=================================
 * Calculates where to place the needed loads
 */
void LiveRange_MarkLoads(LiveRange* lr)
{
  LiveUnit* unit;
  LiveRange_ForAllUnits(lr, unit)
  {
    //need a load if we don't start with a def and we are an entry
    //point to the live range
    if(!(unit->start_with_def))
    {
      if(LiveRange_EntryPoint(lr,unit))
      {
        debug("load need for LR: %d in %s", lr->id, bname(unit->block));
        unit->need_load = TRUE;
      }
    }
  }
}

/*
 *=================================
 * LiveRange_MarkStores
 *=================================
 * Calculates where to place the needed stores 
 */
void LiveRange_MarkStores(LiveRange* lr)
{
debug("*** MARKING STORES for LR: %d ***\n", lr->id);
  //walk through the live units and look for a store 
  LiveUnit* unit;
  Boolean fStore = FALSE; //lr contains a store
  std::list<Variable> def_list;

  LiveRange_ForAllUnits(lr, unit)
  {
    if(unit->defs > 0)
    {
      fStore = TRUE; //TODO: can optimize by caching this in LR
      Def_CollectUniqueUseNames(unit->orig_name, def_list);
    }//if(nstores > 1)
  }//ForAllUnits()

  if(fStore)
  {
    LiveRange_ForAllUnits(lr, unit)
    {
      //check if any successor blocks in the live range need a load
      Edge* edg;
      Block* blkSucc;
      LiveUnit* luSucc;
      Block_ForAllSuccs(edg, unit->block)
      {
        blkSucc = edg->succ;
        if(LiveRange_ContainsBlock(lr,blkSucc))
        { 
          debug("checking lr: %d successor block %s(%d)",
                lr->id, bname(blkSucc), id(blkSucc));
          luSucc = LiveRange_LiveUnitForBlock(lr, blkSucc);

          //a direct successor in our live range needs a load
          if(luSucc->need_load) 
          {
            unit->need_store = TRUE;
            debug("store needed for lr: %d block(%s) "
                  "because load need in %s(%d)", lr->id, 
                  bname(unit->block), bname(blkSucc), id(blkSucc));
          }
        }
        else //unit is an exit point of the live range
        {
          Variable def = unit->orig_name;
          debug("checking def %d\n", def);
          if(find(def_list.begin(), def_list.end(), def) !=
              def_list.end())
          {
            //test for liveness
            Liveness_Info info = SSA_live_in[id(blkSucc)];
            for(LOOPVAR j = 0; j < info.size; j++)
            {
              debug("LIVE_IN: %d in %s",info.names[j], bname(blkSucc));
              //def is live along this path
              if(lr->orig_lrid == info.names[j])
              {
                unit->need_store = TRUE;
                debug("store needed for lr: %d block(%s) "
                      "because it is live in at successor %s(%d)\n",
                      lr->id, bname(unit->block), 
                      bname(blkSucc), id(blkSucc));
                break;
              }
            }
            if(!unit->need_store) {debug("NO STORE: %d\n",unit->orig_name);}
          }
        }//else
      }//ForAllSuccs
    }//ForAllUnits()
  }//if(fStore)

debug("\n*** DONE MARKING STORES for LR: %d ***\n", lr->id);
}//MarkStores

/*
 *=================================
 * Def_CollectUniqueUseNames()
 *=================================
 * Collects all the names that a definition could be known under
 * within a live range. This is done by following the def to any phi
 * nodes and adding those phi node names, and then recursively
 * following those phi nodes.
 *
 * The names are collected uniquely based on the contents of the vgrp
 * list.
 *
 * vgrp - Variable Group
 */
void Def_CollectUniqueUseNames(Variable v, std::list<Variable>& vgrp)
{
  //gather all of the phi nodes this def reaches and count them as
  //defs in this live range too since we are interested in finding
  //out all the defs active in this live range and a def that
  //reaches a phi node continues to be active through that phi
  //node
  std::list<Variable> vgrpWork;
  vgrpWork.push_back(v);

  debug("Collecting uses for def: %d", v);
  while(! vgrpWork.empty())
  {
    Variable vDef = vgrpWork.back(); vgrpWork.pop_back();
    debug("examining %d in queue", vDef);
    if(find(vgrp.begin(), vgrp.end(), vDef) == vgrp.end())
    {
      debug("added name %d", vDef); 
      vgrp.push_back(vDef); //add use to the group

      Chains_List* pcln;
      Chain_ForAllUses(pcln, vDef)
      {
        Block* blk = pcln->block; blk = blk; //only for debug
        //if it is a phi node then add the uses of the phi node
        if(pcln->chain.is_phi_node)
        {
          Variable vPhi;
          vPhi = pcln->chain.op_pointer.phi_node->new_name;
          if(find(vgrp.begin(), vgrp.end(), vPhi) == vgrp.end())
          {
            debug("use (%d) is a phi node in %s(%d)", vPhi, bname(blk), id(blk)); 
            vgrpWork.push_back(vPhi); //follow this phi def
            debug("adding %d to work queue", vPhi); 
          }
        }
        //debug("use (%d) in block %s", vUse, bname(blk));
      }
    }
  }

}

/*
 *=================================
 * LiveRange_EntryPoint
 *=================================
 * return true if the live unit is an entry point to the live range
 */
Boolean LiveRange_EntryPoint(LiveRange* lr, LiveUnit* unit)
{
  Block* pred;
  Edge* e;
  Block_ForAllPreds(e, unit->block)
  {
    pred = e->pred;
    if(!LiveRange_ContainsBlock(lr, pred))
    {
      debug("LR: %d is entry at block %s(%d) from block %s(%d)",
            lr->id, bname(unit->block), id(unit->block),
                    bname(pred), id(pred));
      return TRUE;
    }
  }

  return FALSE;
}


/*
 *=================================
 * LiveRange_ContainsBlock
 *=================================
 * return true if the live range contains this block
 */
Boolean LiveRange_ContainsBlock(LiveRange* lr, Block* b)
{
  return VectorSet_Member(lr->bb_list, id(b));
}


/*
 *============================
 * LiveRange_RegistersAvailable()
 *============================
 * return true if there are registers that can be assigned to this
 * live range in at least on basic block
 */ 
Boolean LiveRange_RegistersAvailable(LiveRange* lr)
{
  return (!RegisterSet_Full(lr->forbidden));
}
Boolean RegisterSet_Full(VectorSet set)
{
  return VectorSet_Equal(all_regs, set);
}


/*
 *======================
 * LiveUnit_Alloc()
 *======================
 *
 ***/
LiveUnit* LiveUnit_Alloc(Arena a)
{
  LiveUnit* unit =  (LiveUnit*)Arena_GetMemClear(a, sizeof(LiveUnit));
  unit->need_load = FALSE;
  unit->need_store = FALSE;
  unit->block = 0;
  return unit;
}


/*
 *======================
 * LiveRange_DDumpAll()
 *======================
 *
 ***/
void LiveRange_DDumpAll(LRList* lrs)
{
#ifdef __DEBUG
  LiveRange_DumpAll(lrs);
#endif
}

/*
 *======================
 * LiveRange_DumpAll()
 *======================
 *
 ***/
void LiveRange_DumpAll(LRList* lrs)
{
  LiveRange* lr = NULL;
  LRList_ForAll(lrs, lr)
  {
    LiveRange_Dump(lr);
  }
}


/*
 *======================
 * LiveRange_DDump()
 *======================
 *
 ***/
void LiveRange_DDump(LiveRange* lr)
{
#ifdef __DEBUG
  LiveRange_Dump(lr);
#endif
}

/*
 *======================
 * LiveRange_Dump()
 *======================
 *
 ***/
static char* type_str[] = 
  {"NO-TYPE",    /* NO_DEFS */
   "INTEGER", /* INT_DEF */ 
   "FLOAT", /* FLOAT_DEF */
   "DOUBLE", /* DOUBLE_DEF */
   "COMPLEX", /* COMPLEX_DEF */
   "DCOMPLEX", /* DCOMPLEX_DEF */ 
   "MULT-TYPE"};   /* MULT_DEFS */
void LiveRange_Dump(LiveRange* lr)
{

  fprintf(stderr,"************ BEGIN LIVE RANGE DUMP **************\n");
  fprintf(stderr,"LR: %d\n",lr->id);
  fprintf(stderr,"type: %s\n",type_str[lr->type]);
  fprintf(stderr,"color: %d\n", lr->color);
  fprintf(stderr,"orig_lrid: %d\n", lr->orig_lrid);
  fprintf(stderr,"candidate?: %c\n", lr->is_candidate ? 'T' : 'F');
  fprintf(stderr,"forbidden colors: \n");
    VectorSet_Dump(lr->forbidden);
  fprintf(stderr, "BB LIST:\n");
    Unsigned_Int b;
    VectorSet_ForAll(b, lr->bb_list)
    {
      fprintf(stderr, "  %d\n", (b));
    }

  fprintf(stderr, "Live Unit LIST:\n");
    LiveUnit* unit;
    LiveRange_ForAllUnits(lr, unit)
    {
      LiveUnit_Dump(*i);
    }

  fprintf(stderr, "Interference List:\n");
    LiveRange* intf_lr;
    LiveRange_ForAllFears(lr,intf_lr)
    {
      fprintf(stderr, "  %d\n", (intf_lr)->id);
    }


  fprintf(stderr,"************* END LIVE RANGE DUMP ***************\n"); 
}


/*
 *======================
 * LiveUnit_DDump()
 *======================
 *
 ***/
void LiveUnit_DDump(LiveUnit* unit)
{
#ifdef __DEBUG
  LiveUnit_Dump(unit);
#endif
}

/*
 *======================
 * LiveUnit_Dump()
 *======================
 *
 ***/
void LiveUnit_Dump(LiveUnit* unit)
{
  fprintf(stderr,"LR Unit: %s (%d)\n",bname(unit->block),
                                      id(unit->block));
  fprintf(stderr,"  Need Load: %c\n",unit->need_load ? 'Y' : 'N');
  fprintf(stderr,"  Need Store: %c\n",unit->need_store ? 'Y' : 'N');
  fprintf(stderr,"  Uses: %d\n",unit->uses);
  fprintf(stderr,"  Defs: %d\n",unit->defs);
  fprintf(stderr,"  Start With Def: %c\n",unit->start_with_def?'Y':'N');
  fprintf(stderr,"  SSA    Name: %d\n",unit->orig_name);
  fprintf(stderr,"  Source Name: %d\n",SSA_name_map[unit->orig_name]);
}


/*
 *=======================================
 * LRList_ComputePriorityAndChooseTop()
 *=======================================
 *
 ***/
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs)
{
  float top_prio = MIN_PRIORITY;
  LiveRange* top_lr = NULL;
  LiveRange* lr = NULL;
  
  LRSet_ForAllCandidates(lrs, lr)
  {
    //priority has never been computed
    if(lr->priority == UNDEFINED_PRIORITY)
    {
      //compute priority
      lr->priority = LiveRange_ComputePriority(lr);
      debug("priority for LR: %d is %.3f", lr->id, lr->priority);

      //check to see if this live range is a non-candidate for
      //allocation. I think we need to only check this the first time
      //we compute the priority function. if the priority changes due
      //to a live range split it should be reset to undefined so we
      //can compute it again.
      if(lr->priority < 0.0 || LiveRange_UnColorable(lr))
      {
        LiveRange_MarkNonCandidateAndDelete(lr);
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
    LRSet_Remove(lrs, top_lr);
  }
  return top_lr;
}



/*
 *==============
 * LRList_Add()
 *==============
 *
 ***/
void LRList_Add(LRList* lrs, LiveRange* lr)
{
  lrs->push_back(lr);
}

/*
 *==============
 * LRList_Empty()
 *==============
 *
 ***/
Boolean LRList_Empty(LRList lrs)
{
  return lrs.empty();
}


/*
 *==============
 * LRList_Size()
 *==============
 *
 ***/
Unsigned_Int LRList_Size(LRList* lrs)
{
  return lrs->size();
}

/*
 *==============
 * LRList_PopTop()
 *==============
 *
 ***/
LiveRange* LRList_Pop(LRList* lrs)
{
  LiveRange* lr = lrs->back();
  lrs->pop_back();
  return lr;
}


/*
 *================================
 * LRList_UpdateConstrainedLists()
 *================================
 * Makes sure that the live ranges are in the constrained lists if
 * they are constrained. This is used to update the lists after a live
 * range split for the live ranges that interfere with both the old
 * and the new live range.
 *
 ***/
void LRSet_UpdateConstrainedLists(LRSet* for_lrs, 
                                   LRSet* constr_lrs,
                                   LRSet* unconstr_lrs)
{
  LiveRange* lr;
  LRSet_ForAllCandidates(for_lrs, lr)
  {
      if(LiveRange_Constrained(lr))
      {
        debug("ensuring LR: %d is in constr", lr->id);
        LRSet_Remove(unconstr_lrs, lr);
        LRSet_Add(constr_lrs, lr);
      }
  }
}


/*
 *================================
 * LRList_Remove()
 *================================
 *
 ***/
void LRList_Remove(LRList* lrs, LiveRange* lr)
{
  LRList::iterator elem;
  elem = find(lrs->begin(), lrs->end(), lr);
  if(elem != lrs->end())
  {
    lrs->erase(elem);
  }
}


/*
 *================================
 * LRList_AddUnique()
 *================================
 *
 ***/
void LRList_AddUnique(LRList* lrs, LiveRange* lr)
{
  LRList::iterator elem;
  elem = find(lrs->begin(), lrs->end(), lr);
  if(elem == lrs->end())
  {
    LRList_Add(lrs, lr);
  }
}

/*
 *================================
 * LiveRange_StoreOpcode()
 *================================
 *
 ***/
static const Opcode_Names store_opcodes[] = 
  {NOP,    /* NO_DEFS */
   iSSTor, /* INT_DEF */ 
   fSSTor, /* FLOAT_DEF */
   dSSTor, /* DOUBLE_DEF */
   cSSTor, /* COMPLEX_DEF */
   qSSTor, /* DCOMPLEX_DEF */ 
   NOP};   /* MULT_DEFS */
Opcode_Names LiveRange_StoreOpcode(LiveRange* lr)
{
  assert(lr->type != NO_DEFS && lr->type != MULT_DEFS);
  return store_opcodes[lr->type];
}

/*
 *================================
 * LiveRange_LoadOpcode()
 *================================
 *
 ***/
static const Opcode_Names load_opcodes[] = 
  {NOP,    /* NO_DEFS */
   iSLDor, /* INT_DEF */ 
   fSLDor, /* FLOAT_DEF */
   dSLDor, /* DOUBLE_DEF */
   cSLDor, /* COMPLEX_DEF */
   qSLDor, /* DCOMPLEX_DEF */ 
   NOP};   /* MULT_DEFS */
Opcode_Names LiveRange_LoadOpcode(LiveRange* lr)
{
  assert(lr->type != NO_DEFS && lr->type != MULT_DEFS);
  return load_opcodes[lr->type];
}

/*
 *================================
 * LiveRange_GetAlignemnt()
 *================================
 *
 ***/
static const Unsigned_Int alignment_size[] = 
  {0, /* NO_DEFS */
   sizeof(Int), /* INT_DEF */ 
   sizeof(float), /* FLOAT_DEF */
   sizeof(Double), /* DOUBLE_DEF */
   2*sizeof(Double), /* COMPLEX_DEF */ //TODO:figire sizeof(Complex)
   4*sizeof(Double),/* DCOMPLEX_DEF */ 
   0};/* MULT_DEFS */
Unsigned_Int LiveRange_GetAlignment(LiveRange* lr)
{
  assert(lr->type != NO_DEFS && lr->type != MULT_DEFS);
  return alignment_size[lr->type];
}

/*
 *================================
 * LiveRange_GetTag()
 *================================
 *
 ***/
Expr LiveRange_GetTag(LiveRange* lr)
{
  if(lr->tag == TAG_UNASSIGNED)
  {
    char str[32];
    sprintf(str, "@lr_%d_%d", lr->orig_lrid, lr->id);
    lr->tag = Expr_Install_String(str);
  }

  return lr->tag;
}

/*
 *========================
 * LiveRange_MemLocation()
 *========================
 * Returns the memory location to be used by the live range
 */
MemoryLocation LiveRange_MemLocation(LiveRange* lr)
{
  if(lr_mem_map[lr->orig_lrid] == MEM_UNASSIGNED)
  {
    lr_mem_map[lr->orig_lrid] = 
      ReserveStackSpace(LiveRange_GetAlignment(lr));
  }

  return lr_mem_map[lr->orig_lrid];
}
