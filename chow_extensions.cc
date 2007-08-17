/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

/*--------------------------INCLUDES---------------------------*/
#include <list>
#include <utility>
#include <queue>

#include "chow_extensions.h"
#include "live_range.h"
#include "live_unit.h"
#include "assign.h"
#include "spill.h"
#include "cfg_tools.h"
#include "chow.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
  using std::list;
  using std::pair;

  /* for enhanced code motion */
  struct CopyDescription
  {
    LiveRange* src_lr;
    LiveRange* dest_lr;
    Register   src_reg;
    Register   dest_reg;
  };
  typedef list<CopyDescription> CDL;
  typedef list<pair<MovedSpillDescription,MovedSpillDescription> > CopyList; 
  bool OrderCopies(const CopyList&, CDL* ordered_copies);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow {
namespace Extensions {

/*
 *========================================
 * Chow::Extensions::EnhancedCodeMotion()
 *========================================
 * looks for a store to a live range that has a load on the same
 * edge. This will only happen when the live range is split and
 * both parts get a register. if we find this situation then we
 * replace the load store with a register to register copy
 *
 * edg - the edge we are examining
 * blkLD - the block where the load was going to be placed. we place
 *         the copy in this block
*/ 
void EnhancedCodeMotion(Edge* edg, Block* blkLD)
{
  using std::vector;
  using std::list;
  using std::make_pair;

  typedef list<MovedSpillDescription>::iterator LI;

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
        switch(ee->spill_type){
          case STORE_SPILL:
          {
            rr_copies.push_back(make_pair(*ee, *eeT));
            removals.push_back(eeT);
            break;
          }
          case LOAD_SPILL:
          {
            rr_copies.push_back(make_pair(*eeT, *ee));
            removals.push_back(ee);
            break;
          }
          default:
            //ignore if it is not a load or a store that we can change
            //into a copy
            error("lrid: %d, st1: %d, st2: %d", ee->lr->orig_lrid,
                  ee->spill_type, eeT->spill_type);
            error("two mem-ops on edge, but not load store");
            assert(false);
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
        //insert the copy in the newly created block right
        //before the last instruction. this will ensure that the
        //copies are sandwiched between the loads and stores in
        //this block (when they are inserted below) which is 
        //necessary for correct code since with enhanced code motion
        //we always move loads and stores onto edges. if there is
        //only one edge between the blocks then the loads move to the
        //top block and the stores move to the bottom block which will
        //cause problems when you load and store from same register
        //but for different live ranges.
        Spill::InsertCopy(cdIT->src_lr, cdIT->dest_lr,
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
}

/*
 *========================================
 * Chow::Extensions::Trim()
 *========================================
 * Attempts to trim away blocks that serve no purpose in the live
 * range. these blocks come up after splitting a live range. there can
 * be dangling blocks that are not part of a path that reaches a use
 * or a def and thus serve no purpose.
*/ 
enum TrimDirection{UP, DOWN};
void Trim(LiveRange* lr, TrimDirection td);
void Trim(LiveRange* lr)
{
  Trim(lr, UP);
  Trim(lr, DOWN);
}
void Trim(LiveRange* lr, TrimDirection td)
{
  std::queue<LiveUnit*> worklist;
  for(LiveRange::iterator i = lr->begin(); i != lr->end(); i++)
  {
    LiveUnit* lu = *i;
    lu->mark = false;
    if(lu->uses > 0 || lu->defs > 0){worklist.push(lu); lu->mark = true;}
  }

  //mark all useful live units
  while(!worklist.empty())
  {
    LiveUnit* lu = worklist.front(); worklist.pop();
    Edge* e;
    if(td == UP) //look up the graph 
    {
      Block_ForAllPreds(e, lu->block)
      {
        Block* pred = e->pred;
        if(lr->ContainsBlock(pred)) 
        {
          LiveUnit* luPred = lr->LiveUnitForBlock(pred);
          if(!luPred->mark)
          {
            luPred->mark = true;
            worklist.push(luPred);
          }
        }
      }
    }
    else //look down the graph 
    {
      Block_ForAllSuccs(e, lu->block)
      {
        Block* succ = e->succ;
        if(lr->ContainsBlock(succ)) 
        {
          LiveUnit* luSucc = lr->LiveUnitForBlock(succ);
          if(!luSucc->mark)
          {
            luSucc->mark = true;
            worklist.push(luSucc);
          }
        }
      }
    }
  }

  //delete unmarked units from the liverange
  for(LiveRange::iterator i = lr->begin(); i != lr->end();)
  {
    LiveUnit* lu = *i; i++;

    if(!lu->mark)
    {
      lr->RemoveLiveUnit(lu); 
          //better be a list or i++ could be invalid
    }
  }
}


/*
 *========================================
 * Chow::Extensions::AddEdgeExtensionNode()
 *========================================
 * Adds another moved spill description onto the edge
*/ 
Edge_Extension* AddEdgeExtensionNode(Edge* e, MovedSpillDescription msd)
{
  //always add the edge extension to the predecessor version of the edge
  Edge* edgPred = FindEdge(e->pred, e->succ, PRED_OWNS);
  Edge_Extension* ee = edgPred->edge_extension;
  if(ee == NULL)
  {
    //create and add the edge extension
    ee = (Edge_Extension*) 
      Arena_GetMemClear(Chow::arena, sizeof(Edge_Extension));
    ee->spill_list = new std::list<MovedSpillDescription>;
    edgPred->edge_extension = ee;
  }
  ee->spill_list->push_back(msd);
  return ee;
}



}}//end Chow::Enhancments namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

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
    cd.src_reg = msdSrc.mreg == -1 ? 
      Assign::GetMachineRegAssignment(msdSrc.orig_blk, 
                                        msdSrc.lr->orig_lrid)
      : msdSrc.mreg;
    cd.dest_lr = msdDest.lr;
    cd.dest_reg = msdDest.mreg == -1 ?
      Assign::GetMachineRegAssignment(msdDest.orig_blk, 
                                        msdDest.lr->orig_lrid)
      : msdDest.mreg;
    

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


}//end anonymous namespace

