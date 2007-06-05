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
     * STRATEGY NAMES
     */
    enum WhenToSplit {
      NO_COLOR_AVAILABLE, 
      NUM_NEIGHBORS_TOO_GREAT
    };
    enum IncludeInSplit {
      WHEN_NOT_FULL, 
      WHEN_ENOUGH_COLORS, 
      WHEN_NOT_TOO_MANY_NEIGHBORS
    };
    enum ColorChoice {
      CHOOSE_FIRST_COLOR, 
      CHOOSE_FROM_MOST_CONSTRAINED
    };

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

    /* A */
    struct IncludeWhenNotFull : public IncludeInSplitStrategy
    {
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    /* B */
    struct IncludeWhenNotTooManyNeighbors : public IncludeInSplitStrategy
    {
      double max_ratio;
      std::set<LiveRange*> neighbors;
      IncludeWhenNotTooManyNeighbors(double maxR=2.0) : max_ratio(maxR) {};
      void Reset(LiveUnit*); 
      void AddNeighbors(LiveRange*,Block* blk);
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    /* C */
    struct IncludeWhenEnoughColors : public IncludeInSplitStrategy
    {
      int fixed_min;
      int min_colors;
      IncludeWhenEnoughColors(int min=-1) : fixed_min(min){};
      void Reset(LiveUnit*); 
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    /*
     * HOW TO COLOR STRATEGIES
     */
    struct ColorChoiceStrategy
    {
      virtual Color operator()
        (const LiveRange*, const std::vector<Color>&) = 0;
      virtual ~ColorChoiceStrategy(){};
    };

    struct ChooseFirstColor : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    struct ChooseColorFromMostConstrainedNeighbor : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    struct ChooseColorInMostNeighborsForbidden : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    struct ChooseColorFromSplit : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };


    /*
     * STRATEGY VARIABLES
     */
    extern ColorChoiceStrategy* color_choice_strategy;
    extern IncludeInSplitStrategy* include_in_split_strategy;
    extern WhenToSplitStrategy* when_to_split_strategy;
    void SetColorChoiceStrategy(ColorChoice cs);
    void SetIncludeInSplitStrategy(IncludeInSplit is);
    void SetWhenToSplitStrategy(WhenToSplit ws);
  }//Heuristics
}//Chow

#endif
