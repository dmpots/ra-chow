/*
 * computes graph reachability
 */

#ifndef __GUARD_REACH_H
#define  __GUARD_REACH_H

#include <Shared.h>
#include "types.h"
#include "debug.h"


namespace Reach {
  void ComputeReachability(Arena);
  VectorSet ReachableBlocks(Block* blk);
}

#endif

