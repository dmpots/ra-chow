/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

#ifndef __GUARD_CHOW_HEURISTICS_H
#define __GUARD_CHOW_HEURISTICS_H

#include <Shared.h>
#include "types.h"

/* forward def */
struct LiveRange;
namespace Chow 
{
  namespace Heuristics
  {
    struct WhenToSplitStrategy
    {
      public:
      virtual bool operator()(LiveRange*) = 0;
      virtual ~WhenToSplitStrategy(){};
    };

    struct SplitWhenNoColorAvailable : public WhenToSplitStrategy
    {
      virtual bool operator()(LiveRange*);
    };

    struct SplitWhenNumNeighborsTooGreat : public WhenToSplitStrategy
    {
      SplitWhenNumNeighborsTooGreat(double maxR=2.0) : max_ratio(maxR){};
      virtual bool operator()(LiveRange*);
      double max_ratio; //num_neighbors/num_colors
    };


    extern WhenToSplitStrategy& default_when_to_split;
    //class SplitWhenTooManyNeighbors;

    /*
    template<class IncludeStrategy>
    bool IncludeInSplit(LiveRange* lr_orig, LiveRange* lr_new,
                        Block* blk, IncludeStrategy strategy)
    {
      return strategy(lr_orig, lr_new, blk);
    }
    */

   
  }
}

#endif
