
/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <SSA.h> 
#include <assert.h>
#include <algorithm>
#include <queue>
#include <iterator>
#include <stdlib.h>
#include <math.h>

#include "live_range.h"
#include "live_unit.h"
#include "debug.h"
#include "chow.h"
#include "params.h"
#include "util.h"
#include "globals.h" 
#include "rc.h" //RegisterClass definitions 
#include "cfg_tools.h" //control graph manipulation utilities
#include "dot_dump.h" //control graph manipulation utilities

/*------------------MODULE LOCAL DEFINITIONS-------------------*/

/* globals */
unsigned int Chow::liverange_count;

namespace {
/* local constants */
  const Expr TAG_UNASSIGNED = (Expr) -1;

/* local variables */
  Arena lr_arena;
  const float UNDEFINED_PRIORITY = 666;
  const float MIN_PRIORITY = -3.4e38;
  VectorSet** mRcBlkId_VsUsedColor;
  VectorSet tmpbbset;
  VectorSet no_bbs;
  MemoryLocation* lr_mem_map;

/* local types */
  /* hold pairs of live range */
  struct LRTuple
  {
    LiveRange* fst;
    LiveRange* snd;
  };

/* local functions */
  void Def_CollectUniqueUseNames(Variable, std::list<Variable>&);
  Priority LiveUnit_ComputePriority(LiveRange*, LiveUnit*);
  Boolean LiveRange_ColorsAvailable(LiveRange* lr);
  Boolean VectorSet_Full(VectorSet vs);
  LiveRange* LiveRange_Create(Arena, RegisterClass);
  VectorSet LiveRange_UsedColorSet(LiveRange* lr, Block* blk);
  void AddEdgeExtensionNode(Edge*, LiveRange*, LiveUnit*,SpillType);

  bool LiveUnit_CanMoveLoad(LiveRange* lr, LiveUnit* lu);
  int LiveUnit_LoadLoopDepth(LiveRange*  lr, LiveUnit* lu);

  LiveUnit* LiveRange_AddLiveUnit(LiveRange*, LiveUnit*);
  LiveUnit* LiveRange_AddLiveUnitBlock(LiveRange*, Block*);
  void LiveRange_RemoveLiveUnit(LiveRange* , LiveUnit* );
  void LiveRange_RemoveLiveUnitBlock(LiveRange* lr, Block* b);
  void LiveRange_TransferLiveUnit(LiveRange*, LiveRange*, LiveUnit*);
  Boolean LiveRange_UnColorable(LiveRange* lr);
  void LiveRange_RemoveInterference(LiveRange* from, LiveRange* with);
  LRTuple LiveRange_Split(LiveRange* lr, LRSet* , LRSet* );
  LiveUnit* LiveRange_ChooseSplitPoint(LiveRange*);
  LiveUnit* LiveRange_IncludeInSplit(LiveRange*, LiveRange*, Block*);
  void LiveRange_AddBlock(LiveRange* lr, Block* b);
  void LiveRange_UpdateAfterSplit(LiveRange*,LiveRange*,LRSet*,LRSet*);
  Boolean LiveRange_EntryPoint(LiveRange* lr, LiveUnit* unit);
  void LiveRange_MarkLoads(LiveRange* lr);
  void LiveRange_MarkStores(LiveRange* lr);
  void LiveRange_InsertLoad(LiveRange* lr, LiveUnit* unit);
  void LiveRange_InsertStore(LiveRange*lr, LiveUnit* unit);
  LiveRange* LiveRange_SplitFrom(LiveRange* origlr);
  Boolean LiveRange_InterferesWith(LiveRange* lr1, LiveRange* lr2);
  Priority LiveRange_OrigComputePriority(LiveRange* lr);
  Priority LiveRange_ComputePriority(LiveRange* lr);
  void UpdateConstrainedLists(LRSet* , LRSet* , LRSet* ); 
}



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

#define LiveRange_NextId (Chow::liverange_count++);

/* Def_ForAllUseBlocks(Variable v, Block* b) */
#define Def_ForAllUseBlocks(v, b)\
Chains_List* _runner;\
Chain_ForAllUses(_runner, v)\
  if(((b = _runner->block)) || TRUE)



/*--------------------BEGIN IMPLEMENTATION---------------------*/

LiveRange::iterator LiveRange::begin() 
{
  return units->begin();
}
LiveRange::iterator LiveRange::end() 
{
  return units->end();
}


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
  Chow::liverange_count = num_lrs;

  lrs.resize(Chow::liverange_count, NULL); //allocate space
  for(i = 0; i < Chow::liverange_count; i++) //allocate each live range
  {
    lrs[i] = LiveRange_Create(arena,
                              RegisterClass_InitialRegisterClassForLRID(i));
    lrs[i]->orig_lrid = i;
    lrs[i]->id = i;
  }

  //allocate a mapping from blocks to a set of used colors
  mRcBlkId_VsUsedColor = (VectorSet**)
       Arena_GetMemClear(arena,sizeof(VectorSet*)*(cRegisterClass));

  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    mRcBlkId_VsUsedColor[rc] = (VectorSet*)
        Arena_GetMemClear(arena,sizeof(VectorSet)*(block_count+1));
    Block* b;
    ForAllBlocks(b)
    {
      Unsigned_Int bid = id(b);
      Unsigned_Int cReg = RegisterClass_NumMachineReg(rc);
      mRcBlkId_VsUsedColor[rc][bid] = VectorSet_Create(arena, cReg);
      VectorSet_Clear(mRcBlkId_VsUsedColor[rc][bid]);
    } 
  }

  //set where all register bits are set

  //temporary set used for various live range computations
  tmpbbset = VectorSet_Create(arena, block_count+1);
  no_bbs = VectorSet_Create(arena, block_count+1);
  VectorSet_Clear(no_bbs);


  //allocate a mapping from live range to a memory location. this is
  //used for spilling
  lr_mem_map = (MemoryLocation*) Arena_GetMemClear(arena, 
       sizeof(MemoryLocation) * Chow::liverange_count);
  for(i = 0; i < Chow::liverange_count; i++)
    lr_mem_map[i] = MEM_UNASSIGNED;

  unsigned int seed = time(NULL);
  //seed = 1154486980;
  srand(seed);
  debug("SRAND: %u", seed);
  //srand(74499979);
  //srand(44979);
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
  lr1->fear_list->insert(lr2);

  //lr1 --interfer--> lr2
  lr2->fear_list->insert(lr1);
}

/*
 *=========================
 * LiveRange_Constrained()
 *=========================
 *
 ***/
bool LiveRange_Constrained(LiveRange* lr)
{
  return 
    (lr->fear_list->size() >=
     RegisterClass_NumMachineReg(LiveRange_RegisterClass(lr)));
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
  chowstats.cSpills++;

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
  //TODO: pick a better color, i.e. one that is used by neighbors
  //neighbors
  //for now just pick the first available color
  VectorSet vsT =
    RegisterClass_TmpVectorSet(LiveRange_RegisterClass(lr));
  VectorSet_Complement(vsT, lr->forbidden);
  Color color = VectorSet_ChooseMember(vsT);
  assert(color < RegisterClass_NumMachineReg(LiveRange_RegisterClass(lr)));
  lr->color = color;
  lr->is_candidate = FALSE; //no longer need a color
  chowstats.clrColored++;
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

  //update the basic block taken set and add loads and stores
  LiveRange_ForAllUnits(lr, unit)
  {
    VectorSet vs = LiveRange_UsedColorSet(lr, unit->block);
    assert(!VectorSet_Member(vs, color));
    VectorSet_Insert(vs, color);
    mBlkIdSSAName_Color[id(unit->block)][lr->orig_lrid] = color;

    // ----------------  LOAD STORE OPTIMIZATION -----------------
    if(Params::Algorithm::move_loads_and_stores)
    {
      if(unit->need_load)
      {
        debug("unit: %s needs load", bname(unit->block));
        //look at all block predecessors, if find one not in the live
        //range then move the load onto that edge
        Edge* e;
        int lrPreds = 0;

        //count the number of predecessor in this live range 
        Block_ForAllPreds(e, unit->block)
        {
          if(LiveRange_ContainsBlock(lr, e->pred)) lrPreds++;
        }

        //we actually move the load if we are using the non-standard
        //chow method, or if we are using chow method and there is at
        //least one predecessor block that is not part of the live
        //range
        bool moveit = 
          (Params::Algorithm::enhanced_code_motion || lrPreds > 0);

        if(moveit) 
        {
          Block_ForAllPreds(e, unit->block)
          {
            if(!LiveRange_ContainsBlock(lr, e->pred))
            {
              debug("moving load for lr: %d_%d to edge %s --> %s",
                lr->orig_lrid, lr->id, bname(e->pred), bname(e->succ));

              //add this spill to list of spills on this edge
              AddEdgeExtensionNode(e, lr, unit, LOAD_SPILL);
            }
          }
        }
        else
        {
          debug("load for lr: %d_%d will not be moved. Inserting now",
                 lr->orig_lrid, lr->id);
          LiveRange_InsertLoad(lr, unit);
        }
      }
      //insert a store unless the store is only internal in which case
      //we can delete it (by never inserting it in the first place :)
      if(unit->need_store && !unit->internal_store)
      {
        debug("unit: %s needs store", bname(unit->block));
        //look at all block successors, if find one not in the live
        //range then move the store onto that edge
        Edge* e;
        bool moved = FALSE;
        int lrSuccs = 0;

        //count the number of successors in this live range 
        Block_ForAllSuccs(e, unit->block)
        {
          if(LiveRange_ContainsBlock(lr, e->succ)) lrSuccs++;
        }

        //we actually move the store if we are not using the standard
        //chow method, or if we are using chow method and there is at
        //least one successor block that is not part of the live
        //range
        bool moveit = 
          (Params::Algorithm::enhanced_code_motion || lrSuccs > 0);

        if(moveit)
        {
          Block_ForAllSuccs(e, unit->block)
          {
            if(!LiveRange_ContainsBlock(lr, e->succ))
            {
              //add this spill to list of spills on this edge
              debug("moving store for lr: %d_%d to edge %s --> %s",
                lr->orig_lrid, lr->id, bname(e->pred), bname(e->succ));
              AddEdgeExtensionNode(e, lr, unit, STORE_SPILL);
              moved = TRUE;
            }
          }
        }
        else
        {
          debug("store for lr: %d_%d will not be moved. Inserting now",
                 lr->orig_lrid, lr->id);
          LiveRange_InsertStore(lr, unit);
        }
      }
    }
    // -------------- NO LOAD STORE OPTIMIZATION ----------------
    else
    {
      if(unit->need_load)
        LiveRange_InsertLoad(lr, unit);
      if(unit->need_store)
        LiveRange_InsertStore(lr, unit);
    }
  }
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
  while(!worklist.empty())
  {
    intf_lr = worklist.back(); worklist.pop_back();
    //only check allocation candidates, may not be a candidate if it
    //has already been assigned a color
    if(!(intf_lr->is_candidate)) continue;

    //split if no registers available
    if(!LiveRange_ColorsAvailable(intf_lr))
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
          worklist.push_back(lr_tup.snd)

        debug("split complete for LR: %d", intf_lr->id);
        Debug::LiveRange_DDump(lr_tup.fst);
        Debug::LiveRange_DDump(lr_tup.snd);
      }
    }
  }
  debug("DONE SPLITTING");
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
  //debug("ADDING: %d BLOCK: %s (%d)", lrid, bname(b), id(b));
  LiveUnit* new_unit = NULL;
  if(!VectorSet_Member(lrset, lrid))
  {
    LiveRange* lr = Chow::live_ranges[lrid];
    VectorSet_Insert(lrset, lrid);
    new_unit = LiveRange_AddLiveUnitBlock(lr, b);
    new_unit->orig_name = orig_name;
  }

  return new_unit;
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
 * LiveRange_CopyOpcode()
 *================================
 *
 ***/
static const Opcode_Names copy_opcodes[] = 
  {NOP,    /* NO_DEFS */
   i2i, /* INT_DEF */ 
   f2f, /* FLOAT_DEF */
   d2d, /* DOUBLE_DEF */
   c2c, /* COMPLEX_DEF */
   q2q, /* DCOMPLEX_DEF */ 
   NOP};   /* MULT_DEFS */
Opcode_Names LiveRange_CopyOpcode(const LiveRange* lr)
{
  assert(lr->type != NO_DEFS && lr->type != MULT_DEFS);
  return copy_opcodes[lr->type];
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
    sprintf(str, "@SPILL_%d(%d)", lr->orig_lrid, 
                                  LiveRange_MemLocation(lr));
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

/*
 *==========================
 * LiveRange_RegisterClass()
 *==========================
 */
RegisterClass LiveRange_RegisterClass(LiveRange* lr)
{
  return lr->rc;
}

/*
 *=======================================
 * ComputePriorityAndChooseTop()
 *=======================================
 *
 ***/
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs)
{
  float top_prio = MIN_PRIORITY;
  LiveRange* top_lr = NULL;
  LiveRange* lr = NULL;
  
  //look at all candidates
  for(LRSet::iterator i = lrs->begin(); i != lrs->end(); i++)
  {
    lr = *i;
    if(!lr->is_candidate) continue;

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
    lrs->erase(top_lr);
  }
  return top_lr;
}


/*------------------INTERNAL MODULE FUNCTIONS--------------------*/
namespace {
/*
 *============================
 * LiveRange_Create()
 *============================
 * Allocates and initializes a live range 
 ***/
LiveRange* LiveRange_Create(Arena arena, RegisterClass rc)
{
  LiveRange* lr;
  lr = (LiveRange*)Arena_GetMemClear(arena, sizeof(LiveRange));
  lr->orig_lrid = (LRID)-1;
  lr->id = 0;
  lr->priority = UNDEFINED_PRIORITY;
  lr->color = NO_COLOR;
  lr->bb_list = VectorSet_Create(arena, block_count+1);
  lr->fear_list = new std::set<LiveRange*, LRcmp>;
  lr->units = new std::list<LiveUnit*>;
  lr->forbidden = VectorSet_Create(arena, RegisterClass_NumMachineReg(rc));
  lr->is_candidate  = TRUE;
  lr->type = NO_DEFS;
  lr->tag = TAG_UNASSIGNED;
  lr->rc  = rc;

  return lr;
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
  unit->internal_store = FALSE;
  
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
  if(LiveRange_RegisterClass(lr1) != LiveRange_RegisterClass(lr2))
  {
    return FALSE;
  }
  VectorSet_Intersect(tmpbbset, lr1->bb_list, lr2->bb_list);
  return (!VectorSet_Equal(no_bbs, tmpbbset));
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
    lr1->fear_list->erase(lr2);

    //remove lr1 from lr2
    lr2->fear_list->erase(lr1);
}



/*
 *=======================================
 * LiveRange_ComputePriority()
 *=======================================
 *
 ***/
Priority LiveRange_OrigComputePriority(LiveRange* lr);
Priority LiveRange_ComputePriority(LiveRange* lr)
{
  //return .1*(rand() % 100);
  LiveUnit* lu;
  Priority pr = 0.0;
  Unsigned_Int clu = 0; //count of live units
  LiveRange_ForAllUnits(lr, lu)
  {
    pr += LiveUnit_ComputePriority(lr, lu);
    clu++;
  }
  return pr/clu;
}

//kept for prosperity in case we want to compare orig priority to the
//priority used when moving loads and stores
Priority LiveRange_OrigComputePriority(LiveRange* lr)
{
  using Params::Machine::load_save_weight;
  using Params::Machine::store_save_weight;
  using Params::Machine::move_cost_weight;
  using Params::Algorithm::loop_depth_weight;

  LiveUnit* lu;
  int clu = 0; //count of live units
  Priority pr = 0.0;
  LiveRange_ForAllUnits(lr, lu)
  {
    Priority unitPrio = 
        load_save_weight  * lu->uses 
      + store_save_weight * lu->defs 
      - move_cost_weight  * lu->need_store
      - move_cost_weight  * lu->need_load;
    pr += unitPrio 
          * pow(loop_depth_weight, Globals::depths[id(lu->block)]);
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
Priority LiveUnit_ComputePriority(LiveRange* lr, LiveUnit* lu)
{
  using Params::Machine::load_save_weight;
  using Params::Machine::store_save_weight;
  using Params::Machine::move_cost_weight;
  using Params::Algorithm::loop_depth_weight;

  Priority unitPrio = 
      load_save_weight  * lu->uses 
    + store_save_weight * lu->defs 
    - move_cost_weight  * lu->need_store;
  unitPrio *= pow(loop_depth_weight, Globals::depths[id(lu->block)]);

  //treat load loop cost separte in case we can move it up from a loop
  int loadLoopDepth = LiveUnit_LoadLoopDepth(lr, lu);
  unitPrio -=   (move_cost_weight * lu->need_load)
                * pow(loop_depth_weight, loadLoopDepth);
  return unitPrio;
}

/*
 *=======================================
 * LiveUnit_LoadLoopDepth()
 * computes loop depth level for inserting loads. the level could be
 * decreased by one to account for moving a load up from a loop header
 *=======================================
 *
 ***/
int LiveUnit_LoadLoopDepth(LiveRange*  lr, LiveUnit* lu)
{
    int depth = depths[id(lu->block)];
    if(Block_IsLoopHeader(lu->block) && LiveUnit_CanMoveLoad(lr, lu))
    {
      depth -= 1;
    }
    return depth;
}

/*
 *=======================================
 * LiveUnit_CanMoveLoad()
 *=======================================
 *
 ***/
bool LiveUnit_CanMoveLoad(LiveRange* lr, LiveUnit* lu)
{
  if(lu->need_load) return false;

  //the load can be moved if there is at least on predecessor *in* the
  //live range. we know there must be at least one *NOT* in the live
  //range because the unit needs a load and this only happens when it
  //is an entry point
  int lrPreds = 0;
  Edge* e;
  Block_ForAllPreds(e, lu->block)
  {
    if(LiveRange_ContainsBlock(lr, e->pred)) lrPreds++;
  }
  return (lrPreds > 0 && Params::Algorithm::move_loads_and_stores);
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
      VectorSet vsUsedColors = LiveRange_UsedColorSet(lr, unit->block);
      if(!VectorSet_Full(vsUsedColors))
      {
        return false;
      }
    }
  }

  debug("LR: %d is UNCOLORABLE", lr->id);
  return true;
}


void AddEdgeExtensionNode(Edge* e, LiveRange* lr, LiveUnit* unit, 
                          SpillType spillType)
{
  //always add the edge extension to the predecessor version of the edge
  Edge* edgPred = FindEdge(e->pred, e->succ, PRED_OWNS);
  Edge_Extension* ee = edgPred->edge_extension;
  if(ee == NULL)
  {
    //create and add the edge extension
    ee = (Edge_Extension*) 
      Arena_GetMemClear(lr_arena, sizeof(Edge_Extension));
    ee->spill_list = new std::list<MovedSpillDescription>;
    edgPred->edge_extension = ee;
  }

  //record the relavant info
  MovedSpillDescription msd;
  msd.lr = lr;
  msd.spill_type = spillType;
  msd.orig_blk = unit->block;
  ee->spill_list->push_back(msd);
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
  chowstats.cSplits++;

  //create a new live range and initialize values
  LiveRange* newlr = LiveRange_SplitFrom(origlr);

  //chose the live unit that will start the new live range
  LiveUnit* startunit;
  LiveUnit* unit;
  startunit = LiveRange_ChooseSplitPoint(origlr);
  assert(startunit != NULL);

  debug("adding block: %s to  lr'", bname(startunit->block));
  LiveRange_TransferLiveUnit(newlr, origlr, startunit);
  Chow::live_ranges.push_back(newlr);
  debug("ADDED LR: %d, size: %d", newlr->id, (int)Chow::live_ranges.size());

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

  if(Debug::dot_dump_lr && Debug::dot_dump_lr == newlr->orig_lrid)
  {
    char fname[32] = {0};
    sprintf(fname, "tmp_%d_%d.dot", newlr->orig_lrid, newlr->id);
    dot_dump_lr(newlr, fname);
    Debug::dot_dumped_lrids.push_back(newlr->id);
  }
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
  LiveRange* newlr = 
    LiveRange_Create(lr_arena, LiveRange_RegisterClass(origlr));
  newlr->orig_lrid = origlr->orig_lrid;
  newlr->id = LiveRange_NextId;
  newlr->is_candidate = TRUE;
  newlr->type = origlr->type;
  newlr->tag = origlr->tag;

  //some sanity checks
  assert(origlr->color == NO_COLOR);
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
  UpdateConstrainedLists(&updates,constr_lrs,unconstr_lrs);
  //also, need to update the new and original live range positions
  //updates.insert(newlr);
  //updates.insert(origlr);
  if(LiveRange_Constrained(newlr))
  {
    constr_lrs->insert(newlr);
  }
  else
  {
    unconstr_lrs->insert(newlr);
  }
  if(!LiveRange_Constrained(origlr))
  {
    constr_lrs->erase(origlr);
    unconstr_lrs->insert(origlr);
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
    VectorSet vsUsed = LiveRange_UsedColorSet(origlr, unit->block);
    VectorSet_Union(origlr->forbidden, 
                    origlr->forbidden,
                    vsUsed);
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
  VectorSet vsUsed = LiveRange_UsedColorSet(lr, b);
  VectorSet_Union(lr->forbidden, 
                  lr->forbidden,
                  vsUsed);
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
    VectorSet vsUsed = LiveRange_UsedColorSet(lr, unit->block);
    if(!VectorSet_Full(vsUsed))
    {
      //only take it if there is a use
      if(unit->uses > 0)
        first = first ? first : unit;

      //prefer starting with a def
      if(unit->start_with_def)
      {
        startdefunit = (startdefunit == NULL) ? unit : startdefunit;

        //and if its an entry point we cant do better than that
        if(LiveRange_EntryPoint(lr,unit))
        {
          //need the assignment here because we may not have assigned
          //to startdefunit if this was not the first unit that has a
          //def
          startdefunit = unit; 
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
  VectorSet vsUsed = LiveRange_UsedColorSet(origlr, b);
  VectorSet vsT =
    RegisterClass_TmpVectorSet(LiveRange_RegisterClass(origlr));
  VectorSet_Union(vsT,
                  newlr->forbidden, 
                  vsUsed);
  if(!VectorSet_Full(vsT))
  {
    unit = LiveRange_LiveUnitForBlock(origlr,b);
  }
  return unit;
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
      Boolean only_internal_store = TRUE; //keep track of why store needed
      unit->internal_store = FALSE; //reset to false, make true later
      Block_ForAllSuccs(edg, unit->block)
      {
        blkSucc = edg->succ;
        if(LiveRange_ContainsBlock(lr,blkSucc))
        { 
          //debug("checking lr: %d successor block %s(%d)",
          //      lr->id, bname(blkSucc), id(blkSucc));
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
              //debug("LIVE_IN: %d in %s",info.names[j], bname(blkSucc));
              //def is live along this path
              if(lr->orig_lrid == info.names[j])
              {
                unit->need_store = TRUE;
                only_internal_store = FALSE;
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
      //now mark whether this store is internal to the live range only
      if(unit->need_store && only_internal_store)
      {
        unit->internal_store = TRUE;
      }
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
          //TODO: I think the test for membership in vgrp is
          //unecessary here since we also test when removing from the
          //worklist
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
 *============================
 * LiveRange_ColorsAvailable()
 *============================
 * return true if there are registers that can be assigned to this
 * live range in at least on basic block
 */ 
Boolean LiveRange_ColorsAvailable(LiveRange* lr)
{
  return (!VectorSet_Full(lr->forbidden));
}
//this breaks all encapsulation for VectorSets but i need this 
Boolean VectorSet_Full(VectorSet vs)
{
  return (VectorSet_Size(vs) == vs->universe_size);
}


/*
 *=============================
 * LiveRange_UsedColorSet()
 *=============================
 *
 ***/
VectorSet LiveRange_UsedColorSet(LiveRange* lr, Block* blk)
{
  RegisterClass rc = LiveRange_RegisterClass(lr);
  return mRcBlkId_VsUsedColor[rc][id(blk)];
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
void UpdateConstrainedLists(LRSet* for_lrs, 
                                   LRSet* constr_lrs,
                                   LRSet* unconstr_lrs)
{
  LiveRange* lr;
  for(LRSet::iterator i = for_lrs->begin(); i != for_lrs->end(); i++)
  {
    lr = *i;
    if(!lr->is_candidate) continue;

    if(LiveRange_Constrained(lr))
    {
      debug("ensuring LR: %d is in constr", lr->id);
      unconstr_lrs->erase(lr);
      constr_lrs->insert(lr);
    }
  }
}

}//end anonymous namespace


