/* for computing reachability in the cfg using iterative data flow
 * analysis
 */

/*--------------------------INCLUDES---------------------------*/
#include <queue>
#include "reach.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
VectorSet* mBlk_ReachSet = NULL;

void AllocateReachMap(Arena);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Reach {

/*
 *============================
 * Reach::ComputeReachability()
 *============================
 *
 ***/
void ComputeReachability(Arena arena)
{
  AllocateReachMap(arena);
  std::queue<Block*> worklist;
  Block* blk;
  ForAllBlocks(blk)
  {
    worklist.push(blk);
  }

  VectorSet vs = VectorSet_Create(arena, block_count+1);
  bool changed = true;
  while(changed)
  {
    changed = false;
    ForAllBlocks_Postorder(blk)
    {
      VectorSet_Clear(vs);
      VectorSet_Insert(vs, bid(blk));
      Edge* e;
      Block_ForAllSuccs(e, blk)
      {
        Block* succ = e->succ;
        VectorSet_Union(vs, vs, mBlk_ReachSet[bid(succ)]);
      }
      if(!VectorSet_Equal(vs, mBlk_ReachSet[bid(blk)]))
      {
        changed = true;
        VectorSet_Copy(mBlk_ReachSet[bid(blk)], vs);
      }
    }
  }
  /*
  ForAllBlocks(blk){
    fprintf(stderr, "blk: %s(%d):\t\t", bname(blk), bid(blk));
    VectorSet_Dump(mBlk_ReachSet[bid(blk)]);
  }
  */
}

/*
 *============================
 * Reach::ReachableBlocks()
 *============================
 *
 ***/
VectorSet ReachableBlocks(Block* blk)
{
  return mBlk_ReachSet[bid(blk)];
}

}//end Reach namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {
void AllocateReachMap(Arena arena)
{
  mBlk_ReachSet = (VectorSet*)
    Arena_GetMemClear(arena, sizeof(VectorSet)* block_count+1);

  for(unsigned int i = 1; i < block_count+1; i++)
    mBlk_ReachSet[i] = VectorSet_Create(arena, block_count+1);
}

}//end anonymous namespace

