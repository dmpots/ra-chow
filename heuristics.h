/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

#ifndef __GUARD_CHOW_HEURISTICS_H
#define __GUARD_CHOW_HEURISTICS_H

#include <Shared.h>
#include <list>
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
      CHOOSE_FROM_MOST_CONSTRAINED,
      CHOOSE_FROM_MOST_FORBIDDEN,
      CHOOSE_FROM_SPLIT
    };
    enum HowToSplit {
      CHOW_SPLIT,
      UP_AND_DOWN_SPLIT
    };
    enum PriorityFunction {
      CLASSIC,
      NO_NORMAL,
      SQUARE_NORMAL,
      GNU,
      GNU_SQUARE_NORMAL,
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

    /* 0 */
    struct SplitWhenNoColorAvailable : public WhenToSplitStrategy
    {
      bool operator()(LiveRange*);
    };

    /* 1 */
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

    /* 0 */
    struct IncludeWhenNotFull : public IncludeInSplitStrategy
    {
      bool operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk);
    };

    /* 1 */
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

    /* 2 */
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

    /* 0 */
    struct ChooseFirstColor : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    /* 1 */
    struct ChooseColorFromMostConstrainedNeighbor : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    /* 2 */
    struct ChooseColorInMostNeighborsForbidden : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    /* 3 */
    struct ChooseColorFromSplit : ColorChoiceStrategy
    {
      Color operator()(const LiveRange*, const std::vector<Color>&);
    };

    /*
     * HOW TO SPLIT STRATEGIES
     */
    struct SplitStrategy
    {
      typedef std::list<Block*> FringeList;
      LiveRange* newlr;
      LiveRange* origlr;

      void operator() (LiveRange*, LiveRange*, LiveUnit*);
      void ExpandSuccs(Block*, FringeList&);
      void ExpandPreds(Block*, FringeList&);
      Block* RemoveFront(FringeList& fringe);
      bool IncludeInSplit(Block*);

      virtual Block* RemoveFringeNode(FringeList&) = 0;
      virtual void ExpandFringeNode(Block*, FringeList&) = 0;
      virtual ~SplitStrategy(){};
    };

    /* 0 */
    struct ChowSplit : SplitStrategy
    {
      Block* RemoveFringeNode(FringeList&);
      void   ExpandFringeNode(Block*,FringeList&);
    };

    /* 1 */
    struct UpAndDownSplit : SplitStrategy
    {
      Block* RemoveFringeNode(FringeList&);
      void   ExpandFringeNode(Block*,FringeList&);
    };

    /*
     * PRIORITY FUNCTIONS
     */
    struct PriorityFunctionStrategy
    {
      virtual Priority operator() (LiveRange*) = 0;
      virtual ~PriorityFunctionStrategy(){};
    };

    /* 0 */
    struct PriorityClassic : PriorityFunctionStrategy
    {
      Priority operator()(LiveRange*);
    };

    /* 1 */
    struct PriorityNoNormal : PriorityFunctionStrategy
    {
      Priority operator()(LiveRange*);
    };

    /* 2 */
    struct PriorityGnu : PriorityFunctionStrategy
    {
      Priority operator()(LiveRange*);
    };

    /* 3 */
    struct PrioritySquareNormal : PriorityFunctionStrategy
    {
      Priority operator()(LiveRange*);
    };

    /* 4 */
    struct PriorityGnuSquareNormal : PriorityFunctionStrategy
    {
      Priority operator()(LiveRange*);
    };

    /*
     * STRATEGY VARIABLES
     */
    extern ColorChoiceStrategy* color_choice_strategy;
    extern IncludeInSplitStrategy* include_in_split_strategy;
    extern WhenToSplitStrategy* when_to_split_strategy;
    extern SplitStrategy* how_to_split_strategy;
    extern PriorityFunctionStrategy* priority_strategy;

    void SetColorChoiceStrategy(ColorChoice cs);
    void SetIncludeInSplitStrategy(IncludeInSplit is);
    void SetWhenToSplitStrategy(WhenToSplit ws);
    void SetHowToSplitStrategy(HowToSplit ws);
    void SetPriorityFunctionStrategy(PriorityFunction pf);
  }//Heuristics
}//Chow

#endif
