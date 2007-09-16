/* definitions of various priority functions used for deciding the
 * order live ranges should be assigned to a register.
 */

/*--------------------------INCLUDES---------------------------*/
#include <cmath>

#include "priority.h"
#include "live_range.h"
#include "live_unit.h"
#include "cfg_tools.h" 
#include "params.h"
#include "shared_globals.h" 

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
  Priority BasePriority(
    LiveRange* lr, 
    bool use_log, 
    bool square_len,
    bool normalize
  );
  Priority LiveUnit_ComputePriority(LiveRange* lr, LiveUnit* lu);
  bool LiveUnit_CanMoveLoad(LiveRange* lr, LiveUnit* lu);
  int LiveUnit_LoadLoopDepth(LiveRange*  lr, LiveUnit* lu);

  inline double log2(double v){
    return log(v)/log(2);
  }
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow{
namespace PriorityFuns {
Priority Classic(LiveRange* lr)
{
  return BasePriority(lr, false, false, true);
}

Priority NoNormal(LiveRange* lr)
{
  return BasePriority(lr, false, false, false);
}

Priority Gnu(LiveRange* lr)
{
  return BasePriority(lr, true, false, true);
}

Priority SquareNormal(LiveRange* lr)
{
  return BasePriority(lr, false, true, true);
}

Priority GnuSquareNormal(LiveRange* lr)
{
  return BasePriority(lr, true, true, true);
}

}}//end Chow::PriorityFuns namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {
/*
 *=======================================
 * BasePriority()
 *=======================================
 *
 ***/
Priority BasePriority(
  LiveRange* lr, 
  bool use_log, 
  bool square_len,
  bool normalize
)
{
  Priority pr = 0.0;
  Unsigned_Int clu = 0; //count of live units
  for(LiveRange::iterator luIT = lr->begin(), luE = lr->end(); 
      luIT != luE; ++luIT)
  {
    pr += LiveUnit_ComputePriority(lr, *luIT);
    clu++;
  }

  if(use_log) pr = log2(pr);
  if(square_len) clu = clu*clu;
  if(normalize) pr = pr/clu;
  return pr;
}


/*
 *=======================================
 * LiveUnit_ComputePriority()
 *=======================================
 *
 ***/
inline bool need_store(LiveUnit* lu){
  if(Params::Algorithm::move_loads_and_stores){
    return lu->need_store && !lu->internal_store;
  }
  return lu->need_store;
}
Priority LiveUnit_ComputePriority(LiveRange* lr, LiveUnit* lu)
{
  using Params::Machine::load_save_weight;
  using Params::Machine::store_save_weight;
  using Params::Machine::move_cost_weight;
  using Params::Algorithm::loop_depth_weight;

  Priority unitPrio = 
      load_save_weight  * lu->uses 
    + store_save_weight * lu->defs 
    - move_cost_weight  * need_store(lu);
  unitPrio *= pow(loop_depth_weight, Globals::depths[bid(lu->block)]);

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
    int depth = depths[bid(lu->block)];
    if(Block_IsLoopHeader(lu->block) && LiveUnit_CanMoveLoad(lr, lu))
    {
      depth -= 1;
    }
    return depth;
}

#if 0
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
          * pow(loop_depth_weight, Globals::depths[bid(lu->block)]);
    clu++;
  }
  return pr/clu;
}
#endif


}//end anonymous namespace

