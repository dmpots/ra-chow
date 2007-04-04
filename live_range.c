
/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <SSA.h> 
#include <cassert>
#include <algorithm>
#include <cmath>

#include "live_range.h"
#include "live_unit.h"
#include "params.h"
#include "shared_globals.h" 
#include "rc.h" //RegisterClass definitions 
#include "cfg_tools.h" //control graph manipulation utilities
#include "spill.h"
#include "color.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
/* local constants */
  const Opcode_Names load_opcodes[] =  /* load */
    {NOP,    /* NO_DEFS */
    iSLDor, /* INT_DEF */ 
    fSLDor, /* FLOAT_DEF */
    dSLDor, /* DOUBLE_DEF */
    cSLDor, /* COMPLEX_DEF */
    qSLDor, /* DCOMPLEX_DEF */ 
    NOP};   /* MULT_DEFS */
  const Opcode_Names store_opcodes[] = /* store */
    {NOP,    /* NO_DEFS */
    iSSTor, /* INT_DEF */ 
    fSSTor, /* FLOAT_DEF */
    dSSTor, /* DOUBLE_DEF */
    cSSTor, /* COMPLEX_DEF */
    qSSTor, /* DCOMPLEX_DEF */ 
    NOP};   /* MULT_DEFS */
  const Opcode_Names copy_opcodes[] = /* copy */
    {NOP,    /* NO_DEFS */
    i2i, /* INT_DEF */ 
    f2f, /* FLOAT_DEF */
    d2d, /* DOUBLE_DEF */
    c2c, /* COMPLEX_DEF */
    q2q, /* DCOMPLEX_DEF */ 
    NOP};   /* MULT_DEFS */
  const unsigned int alignment_size[] = 
  {0, /* NO_DEFS */
   sizeof(Int), /* INT_DEF */ 
   sizeof(float), /* FLOAT_DEF */
   sizeof(Double), /* DOUBLE_DEF */
   2*sizeof(Double), /* COMPLEX_DEF */ //TODO:figire sizeof(Complex)
   4*sizeof(Double),/* DCOMPLEX_DEF */ 
   0};/* MULT_DEFS */

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
  Boolean VectorSet_Full(VectorSet vs);
  Boolean VectorSet_Empty(VectorSet vs);
  void AddEdgeExtensionNode(Edge*, LiveRange*, LiveUnit*,SpillType);

  bool LiveUnit_CanMoveLoad(LiveRange* lr, LiveUnit* lu);
  int LiveUnit_LoadLoopDepth(LiveRange*  lr, LiveUnit* lu);
  Priority LiveUnit_ComputePriority(LiveRange* lr, LiveUnit* lu);

  LiveUnit* LiveRange_AddLiveUnit(LiveRange*, LiveUnit*);
  LiveUnit* LiveRange_AddLiveUnitBlock(LiveRange*, Block*);
  void LiveRange_RemoveLiveUnit(LiveRange* , LiveUnit* );
  void LiveRange_RemoveLiveUnitBlock(LiveRange* lr, Block* b);
  void LiveRange_TransferLiveUnit(LiveRange*, LiveRange*, LiveUnit*);
  LiveUnit* LiveRange_ChooseSplitPoint(LiveRange*);
  LiveUnit* LiveRange_IncludeInSplit(LiveRange*, LiveRange*, Block*);
  void LiveRange_AddBlock(LiveRange* lr, Block* b);
  void LiveRange_UpdateAfterSplit(LiveRange*,LiveRange*);
  Boolean LiveRange_EntryPoint(LiveRange* lr, LiveUnit* unit);
  void LiveRange_MarkLoads(LiveRange* lr);
  void LiveRange_MarkStores(LiveRange* lr);
  void LiveRange_InsertLoad(LiveRange* lr, LiveUnit* unit);
  void LiveRange_InsertStore(LiveRange*lr, LiveUnit* unit);
  LiveRange* LiveRange_SplitFrom(LiveRange* origlr);
  Priority LiveRange_OrigComputePriority(LiveRange* lr);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
/*
 *============================
 * LiveRange::Init()
 *============================
 * Initialize class variables
 ***/
Arena LiveRange::arena = NULL;
VectorSet LiveRange::tmpbbset = NULL;
const float LiveRange::UNDEFINED_PRIORITY = 666;
unsigned int LiveRange::counter = 0;
void LiveRange::Init(Arena arena, unsigned int counter_start)
{
  LiveRange::arena = arena;
  LiveRange::tmpbbset = VectorSet_Create(arena, block_count+1);
  LiveRange::counter = counter_start;
}


/*
 *============================
 * LiveRange Constructor
 *============================
 * Allocates and initializes a live range 
 ***/
LiveRange::LiveRange(RegisterClass::RC reg_class, LRID lrid)
{
  orig_lrid = lrid;
  id = lrid;
  rc = reg_class;
  priority = UNDEFINED_PRIORITY;
  color = Coloring::NO_COLOR;
  bb_list = VectorSet_Create(LiveRange::arena, block_count+1);
  fear_list = new std::set<LiveRange*, LRcmp>;
  units = new std::list<LiveUnit*>;
  forbidden = 
    VectorSet_Create(LiveRange::arena, RegisterClass::NumMachineReg(rc));
  is_candidate  = TRUE;
  type = NO_DEFS; //type will be set later
}

/*
 *============================
 * LiveRange::begin()
 *============================
 * returns an iterator to the beginning of the live unit sequence
 ***/
LiveRange::iterator LiveRange::begin() const
{
  return units->begin();
}

/*
 *============================
 * LiveRange::end()
 *============================
 * returns an iterator to the end of the live unit sequence
 */
LiveRange::iterator LiveRange::end() const
{
  return units->end();
}

/*
 *=============================
 * LiveRange::AddInterference()
 *=============================
 *
 ***/
void LiveRange::AddInterference(LiveRange* lr2)
{
  //lr2 --interfer--> lr1
  fear_list->insert(lr2);

  //lr1 --interfer--> lr2
  lr2->fear_list->insert(this);
}

/*
 *============================
 * LiveRange::IsConstrained()
 *============================
 *
 ***/
bool LiveRange::IsConstrained() const
{
  return 
    (fear_list->size() >= RegisterClass::NumMachineReg(rc));
}

/*
 *=======================================
 * LiveRange::MarkNonCandidateAndDelete()
 *=======================================
 * mark the live range so that we know it is not a candidate for a
 * register and delete it from the interference graph.
 *
 ***/
void LiveRange::MarkNonCandidateAndDelete()
{
  color = Coloring::NO_COLOR;
  is_candidate = FALSE;
  Stats::chowstats.cSpills++;

  debug("deleting LR: %d from interference graph", this->id);
  //remove me from all neighbors fear list
  for(LRSet::iterator it = fear_list->begin(); it != fear_list->end(); it++)
  {
    (*it)->fear_list->erase(this);
  }
  //delete all live ranges in my fear list
  fear_list->clear();

  //clear the bb_list so that this live range will no longer interfere
  //with any other live ranges
  VectorSet_Clear(bb_list);
}

/*
 *=======================================
 * LiveRange::AssignColor()
 *=======================================
 * assign an available color to a live range
 *
 ***/
void LiveRange::AssignColor()
{
  //TODO: pick a better color, i.e. one that is used by neighbors
  //neighbors
  //for now just pick the first available color
  VectorSet vsT = RegisterClass::TmpVectorSet(rc);
  VectorSet_Complement(vsT, forbidden);
  Color chosen_color = VectorSet_ChooseMember(vsT);
  color = chosen_color;
  assert(color < RegisterClass::NumMachineReg(rc));
  is_candidate = FALSE; //no longer need a color
  Stats::chowstats.clrColored++;
  debug("assigning color: %d to lr: %d", color, this->id);

  //update the interfering live ranges forbidden set
  for(LRSet::iterator it = fear_list->begin(); it != fear_list->end(); it++)
  {
    LiveRange* intf_lr = *it;
    VectorSet_Insert(intf_lr->forbidden, color);
    debug("adding color: %d to forbid list for LR: %d", color, intf_lr->id)
  }

  //update the basic block taken set and add loads and stores
  for(LiveRange::iterator it = begin(); it != end(); it++)
  {
    LiveUnit* unit = *it;
    VectorSet vs = Coloring::UsedColors(this->rc, unit->block);
    assert(!VectorSet_Member(vs, color));
    VectorSet_Insert(vs, color);
    Coloring::SetColor(unit->block, orig_lrid, color);

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
          if(this->ContainsBlock(e->pred)) lrPreds++;
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
            if(!this->ContainsBlock(e->pred))
            {
              debug("moving load for lr: %d_%d to edge %s --> %s",
                orig_lrid, this->id, bname(e->pred), bname(e->succ));

              //add this spill to list of spills on this edge
              AddEdgeExtensionNode(e, this, unit, LOAD_SPILL);
            }
          }
        }
        else
        {
          debug("load for lr: %d_%d will not be moved. Inserting now",
                 orig_lrid, this->id);
          LiveRange_InsertLoad(this, unit);
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
          if(this->ContainsBlock(e->succ)) lrSuccs++;
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
            if(!this->ContainsBlock(e->succ))
            {
              //add this spill to list of spills on this edge
              debug("moving store for lr: %d_%d to edge %s --> %s",
                orig_lrid, this->id, bname(e->pred), bname(e->succ));
              AddEdgeExtensionNode(e, this, unit, STORE_SPILL);
              moved = TRUE;
            }
          }
        }
        else
        {
          debug("store for lr: %d_%d will not be moved. Inserting now",
                 orig_lrid, this->id);
          LiveRange_InsertStore(this, unit);
        }
      }
    }
    // -------------- NO LOAD STORE OPTIMIZATION ----------------
    else
    {
      if(unit->need_load)
        LiveRange_InsertLoad(this, unit);
      if(unit->need_store)
        LiveRange_InsertStore(this, unit);
    }
  }
}

/*
 *=============================
 * LiveRange::LiveUnitForBlock()
 *=============================
 *
 * Returns the LiveUnit associated with the given block. If no such
 * unit exists NULL is returned.
 ***/
LiveUnit* LiveRange::LiveUnitForBlock(Block* b) const
{
  LiveUnit* unit;
  for(iterator i = begin(); i != end(); i++)
  {
    unit = *i;
    if(id(unit->block) == id(b))
    {
      return unit;
    }
  }

  return NULL;
}

/*
 *=================================
 * LiveRange::ContainsBlock
 *=================================
 * return true if the live range contains this block
 */
bool LiveRange::ContainsBlock(Block* blk) const
{
  return VectorSet_Member(bb_list, id(blk));
}

/*
 *=================================
 * LiveRange::MarkLoadsAndStores
 *=================================
 * Calculates where to place the needed loads and stores in a live
 * range.
 */ 
void LiveRange::MarkLoadsAndStores()
{
  LiveRange_MarkLoads(this);
  LiveRange_MarkStores(this);
}


/*
 *================================
 * LiveRange::LoadOpcode()
 *================================
 *
 ***/
Opcode_Names LiveRange::LoadOpcode() const
{
  assert(type != NO_DEFS && type != MULT_DEFS);
  return load_opcodes[type];
}

/*
 *================================
 * LiveRange::StoreOpcode()
 *================================
 *
 ***/
Opcode_Names LiveRange::StoreOpcode() const
{
  assert(type != NO_DEFS && type != MULT_DEFS);
  return store_opcodes[type];
}


/*
 *================================
 * LiveRange::CopyOpcode()
 *================================
 *
 ***/
Opcode_Names LiveRange::CopyOpcode() const
{
  assert(type != NO_DEFS && type != MULT_DEFS);
  return copy_opcodes[type];
}



/*
 *================================
 * LiveRange::Alignemnt()
 *================================
 *
 ***/
unsigned int LiveRange::Alignment() const
{
  assert(type != NO_DEFS && type != MULT_DEFS);
  return alignment_size[type];
}


/*
 *=============================
 * LiveRange::InterferesWith()
 *=============================
 *
 * Used to determine if two live ranges interfere
 * true - if the live ranges interfere
 ***/
Boolean LiveRange::InterferesWith(LiveRange* lr2) const
{
  if(rc != lr2->rc)
  {
    return FALSE;
  }
  VectorSet_Intersect(LiveRange::tmpbbset, bb_list, lr2->bb_list);
  return (!VectorSet_Empty(LiveRange::tmpbbset));
}

/*
 *===============================
 * LiveRange::HasColorAvailable()
 *===============================
 * return true if there is at least one register that can be assigned
 *  to this live range in all of its live units
 */ 
Boolean LiveRange::HasColorAvailable() const
{
  return (!VectorSet_Full(forbidden));
}

/*
 *=======================================
 * LiveRange::IsEntirelyUnColorable()
 *=======================================
 * returns true if the live range is uncolorable
 *
 ***/
Boolean LiveRange::IsEntirelyUnColorable() const
{
  //a live range is uncolorable when all registers have been used
  //throughout the entire length of the live range (i.e. each live
  //unit has no available registers). we need to have a mapping of
  //basic blocks to used registers for that block in order to see
  //which registers are available.
  for(LiveRange::iterator it = begin(); it != end(); it++)
  {
    LiveUnit* unit = *it;
    //must have a free register where we have a def or a use
    if(unit->defs > 0 || unit->uses > 0)
    {
      VectorSet vsUsedColors = Coloring::UsedColors(rc, unit->block);
      if(!VectorSet_Full(vsUsedColors))
      {
        return false;
      }
    }
  }

  debug("LR: %d is UNCOLORABLE", id);
  return true;
}

/*
 *============================
 * LiveRange::Split()
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
LiveRange* LiveRange::Split()
{
  Stats::chowstats.cSplits++;

  //create a new live range and initialize values
  LiveRange* newlr = LiveRange_SplitFrom(this);

  //chose the live unit that will start the new live range
  LiveUnit* startunit;
  LiveUnit* unit;
  startunit = LiveRange_ChooseSplitPoint(this);
  assert(startunit != NULL);

  debug("adding block: %s to  lr'", bname(startunit->block));
  LiveRange_TransferLiveUnit(newlr, this, startunit);

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
      if((unit = LiveRange_IncludeInSplit(newlr, this, succ)) != NULL)
      {
        debug("adding block: %s to  lr'", bname(succ));
        LiveRange_TransferLiveUnit(newlr, this, unit);
        succ_list.push_back(succ); //explore the succs of this node
      }
    }
  }

  LiveRange_UpdateAfterSplit(newlr, this);
  
  if(Debug::dot_dump_lr && Debug::dot_dump_lr == newlr->orig_lrid)
  {
    Debug::DotDumpLR(newlr, "split");
    Debug::dot_dumped_lrs.push_back(newlr);
  }
  return newlr;
}

/*
 *==================================
 * LiveRange::AddLiveUnitForBlock()
 *==================================
 *
 ***/
LiveUnit* 
LiveRange::AddLiveUnitForBlock(Block* b, 
                               Variable orig_name, 
                               const Stats::BBStats& stat)
{
  LiveUnit* unit = LiveUnit_Alloc(LiveRange::arena);

  //assign initial values
  unit->block = b;
  unit->uses = stat.uses;
  unit->defs = stat.defs;
  unit->start_with_def = stat.start_with_def;
  unit->internal_store = FALSE;
  unit->orig_name = orig_name;
  
  LiveRange_AddLiveUnit(this, unit);
  return unit;
}

/*
 *=======================================
 * LiveRange::ComputePriority()
 *=======================================
 *
 ***/
Priority LiveRange::ComputePriority()
{
  Priority pr = 0.0;
  Unsigned_Int clu = 0; //count of live units
  for(LiveRange::iterator luIT = begin(); luIT != end(); luIT++)
  {
    pr += LiveUnit_ComputePriority(this, *luIT);
    clu++;
  }
  //TODO: set priority in this function
  priority = pr/clu;
  return priority;
}


/*------------------INTERNAL MODULE FUNCTIONS--------------------*/
namespace {

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
  remove_unit = lr->LiveUnitForBlock(b);

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
 * LiveUnit_CanMoveLoad()
 *=======================================
 * used by LiveUnit to compute priority taking into account the final
 * resting place of the load
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
    if(lr->ContainsBlock(e->pred)) lrPreds++;
  }
  return (lrPreds > 0 && Params::Algorithm::move_loads_and_stores);
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



//kept for prosperity in case we want to compare orig priority to the
//priority used when moving loads and stores
Priority LiveRange_OrigComputePriority(LiveRange* lr)
{
  using Params::Machine::load_save_weight;
  using Params::Machine::store_save_weight;
  using Params::Machine::move_cost_weight;
  using Params::Algorithm::loop_depth_weight;

  int clu = 0; //count of live units
  Priority pr = 0.0;
  for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
  {
    LiveUnit* lu = *it;
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
      Arena_GetMemClear(LiveRange::arena, sizeof(Edge_Extension));
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
  using Spill::InsertLoad;
  using Spill::frame;

  Block* b = unit->block;
  debug("INSERTING LOAD: lrid: %d, block: %s, to: %d",
        lr->id, bname(unit->block), unit->orig_name);
  InsertLoad(lr, Block_FirstInst(b), unit->orig_name, frame.ssa_name);
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
  using Spill::InsertStore;
  using Spill::frame;

  Block* b = unit->block;
  debug("INSERTING STORE: lrid: %d, block: %s, to: %d",
        lr->id, bname(unit->block), unit->orig_name);
  (void)
   InsertStore(lr, Block_LastInst(b), unit->orig_name,
                      frame.ssa_name, BEFORE_INST);
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
  LiveRange* newlr = new LiveRange(origlr->rc, NO_LRID);
  newlr->orig_lrid = origlr->orig_lrid;
  newlr->id = LiveRange::counter++;
  newlr->is_candidate = TRUE;
  newlr->type = origlr->type;

  //some sanity checks
  assert(origlr->color == Coloring::NO_COLOR);
  assert(origlr->is_candidate == TRUE);

  return newlr;
}


/*
 *================================
 * LiveRange_UpdateAfterSplit()
 *================================
 *
 */
void LiveRange_UpdateAfterSplit(LiveRange* newlr, LiveRange* origlr)
{

  //rebuild interferences of those live ranges that interfere with 
  //the original live range. they may now interfere with the new live
  //range and/or no longer interefer with the original live range
  for(LRSet::iterator it = origlr->fear_list->begin(); 
      it != origlr->fear_list->end();)
  {
    LiveRange* fearlr = *it;
    //update newlr interference
    if(newlr->InterferesWith(fearlr))
    {
      newlr->AddInterference(fearlr);
    }

    //update origlr interference
    if(!origlr->InterferesWith(fearlr))
    {
      //increment iterator before delete
      LRSet::iterator del = it++; 
      fearlr->fear_list->erase(origlr);
      origlr->fear_list->erase(del);
    }
    else //does not interfere increment iterator normally
    {
      it++; 
    }
  }

  //the need_load and need_store flags actually depend on the
  //boundries of the live range so we must recompute them
  newlr->MarkLoadsAndStores();
  origlr->MarkLoadsAndStores();


  //removing live units means we need to update the forbidden list
  //only do this for the orig live range since the new live range
  //keeps track as it goes
  VectorSet_Clear(origlr->forbidden);
  for(LiveRange::iterator it = origlr->begin(); it != origlr->end(); it++)
  {
    LiveUnit* unit = *it;
    VectorSet vsUsed = Coloring::UsedColors(origlr->rc, unit->block);
    VectorSet_Union(origlr->forbidden, 
                    origlr->forbidden,
                    vsUsed);
  }

  //reset the priorites on the split live ranges since they are no
  //longer current. they will be recomputed if needed
  newlr->priority  = LiveRange::UNDEFINED_PRIORITY;
  origlr->priority = LiveRange::UNDEFINED_PRIORITY;
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
  VectorSet vsUsed = Coloring::UsedColors(lr->rc, b);
  VectorSet_Union(lr->forbidden, lr->forbidden, vsUsed);
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
  LiveUnit* startunit = NULL;
  LiveUnit* first = NULL;
  LiveUnit* startdefunit = NULL;

  for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
  {
    LiveUnit* unit = *it;
    VectorSet vsUsed = Coloring::UsedColors(lr->rc, unit->block);
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
  VectorSet vsUsed = Coloring::UsedColors(origlr->rc, b);
  VectorSet vsT =
    RegisterClass::TmpVectorSet(origlr->rc);
  VectorSet_Union(vsT,
                  newlr->forbidden, 
                  vsUsed);
  if(!VectorSet_Full(vsT))
  {
    unit = origlr->LiveUnitForBlock(b);
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
  for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
  {
    LiveUnit* unit = *it;
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
  Boolean fStore = FALSE; //lr contains a store
  std::list<Variable> def_list;

  for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
  {
    LiveUnit* unit = *it;
    if(unit->defs > 0)
    {
      fStore = TRUE; //TODO: can optimize by caching this in LR
      Def_CollectUniqueUseNames(unit->orig_name, def_list);
    }//if(nstores > 1)
  }

  if(fStore)
  {
    for(LiveRange::iterator it = lr->begin(); it != lr->end(); it++)
    {
      LiveUnit* unit = *it;
      //check if any successor blocks in the live range need a load
      Edge* edg;
      Block* blkSucc;
      LiveUnit* luSucc;
      Boolean only_internal_store = TRUE; //keep track of why store needed
      unit->internal_store = FALSE; //reset to false, make true later
      Block_ForAllSuccs(edg, unit->block)
      {
        blkSucc = edg->succ;
        if(lr->ContainsBlock(blkSucc))
        { 
          //debug("checking lr: %d successor block %s(%d)",
          //      lr->id, bname(blkSucc), id(blkSucc));
          luSucc = lr->LiveUnitForBlock(blkSucc);

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
    if(!lr->ContainsBlock(pred))
    {
      debug("LR: %d is entry at block %s(%d) from block %s(%d)",
            lr->id, bname(unit->block), id(unit->block),
                    bname(pred), id(pred));
      return TRUE;
    }
  }

  return FALSE;
}


//this breaks all encapsulation for VectorSets but i need this 
Boolean VectorSet_Full(VectorSet vs)
{
  return (VectorSet_Size(vs) == vs->universe_size);
}
Boolean VectorSet_Empty(VectorSet vs)
{
  return (VectorSet_Size(vs) == 0);
}

}//end anonymous namespace


