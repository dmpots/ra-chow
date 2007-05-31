/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

#ifndef __GUARD_CHOW_HEURISTICS_H
#define __GUARD_CHOW_HEURISTICS_H

#include <Shared.h>
#include "types.h"

/* forward def */
struct LiveRange;
struct LiveUnit;
namespace Chow 
{
  namespace Heuristics
  {

    /*
     * WHEN TO SPLIT STRATEGIES
     */
    struct WhenToSplitStrategy
    {
      public:
      virtual bool operator()(LiveRange*) = 0;
      virtual ~WhenToSplitStrategy(){};
    };

    struct SplitWhenNoColorAvailable : public WhenToSplitStrategy
    {
      bool operator()(LiveRange*);
    };

    struct SplitWhenNumNeighborsTooGreat : public WhenToSplitStrategy
    {
      double max_ratio; //num_neighbors/num_colors
      SplitWhenNumNeighborsTooGreat(double maxR=2.0) : max_ratio(maxR){};
      bool operator()(LiveRange*);
    };


    /*
     * WHEN TO INCLUDE IN SPLIT STRATEGIES
     */
    struct IncludeInSplitStrategy
    {
      public:
      virtual bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk) = 0;
      virtual void Reset(LiveUnit*){}; //default, do nothing
      virtual ~IncludeInSplitStrategy(){};
    };

    struct IncludeWhenNotFull : public IncludeInSplitStrategy
    {
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    struct IncludeWhenNotTooManyNeighbors : public IncludeInSplitStrategy
    {
      double max_ratio;
      std::set<LiveRange*> neighbors;
      IncludeWhenNotTooManyNeighbors(double maxR=2.0) : max_ratio(maxR) {};
      void Reset(LiveUnit*); 
      void AddNeighbors(Block* blk);
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    /*
     * DEFAULT STRATEGY VARIABLES
     */
    extern WhenToSplitStrategy& default_when_to_split;
    extern IncludeInSplitStrategy& default_include_in_split;

  }//Heuristics
}//Chow

#endif
