/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

/*--------------------------INCLUDES---------------------------*/
#include <functional>
#include <algorithm>
#include "heuristics.h"
#include "live_range.h"
#include "live_unit.h"
#include "color.h"
#include "chow.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
using namespace Chow::Heuristics;
/*
 * STRATEGY VARIABLES
 */
//splitting
SplitWhenNoColorAvailable no_color_available;
SplitWhenNumNeighborsTooGreat num_neighbors_too_great;
ChowSplit chow_split;
UpAndDownSplit up_and_down_split;

//including
IncludeWhenNotFull when_not_full;
IncludeWhenEnoughColors when_enough_colors;
IncludeWhenNotTooManyNeighbors when_not_too_many_neighbors;

//coloring
ChooseFirstColor choose_first_color;
ChooseColorFromMostConstrainedNeighbor choose_from_most_constrained;
ChooseColorInMostNeighborsForbidden choose_most_forbidden;
ChooseColorFromSplit choose_from_split;

template<class T>
inline T max(T a, T b){return a > b ? a : b;}

unsigned int ColorsLeftAfterBlock(LiveRange* lr, Block* blk);
Color FindMaxOrDefault(
  const std::map<Color,int>& color_count_map,
  const std::vector<Color>& choices
);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow {
namespace Heuristics {
//strategy variables
ColorChoiceStrategy* color_choice_strategy = NULL;
IncludeInSplitStrategy* include_in_split_strategy = NULL;
WhenToSplitStrategy* when_to_split_strategy = NULL;
SplitStrategy* how_to_split_strategy = NULL;

//heuristic setters
void SetColorChoiceStrategy(ColorChoice cs)
{
  switch(cs)
  {
    case CHOOSE_FIRST_COLOR:
      color_choice_strategy = &choose_first_color;
      break;
    case CHOOSE_FROM_MOST_CONSTRAINED:
      color_choice_strategy = &choose_from_most_constrained;
      break;
    case CHOOSE_FROM_MOST_FORBIDDEN:
      color_choice_strategy = &choose_most_forbidden;
      break;
    case CHOOSE_FROM_SPLIT:
      color_choice_strategy = &choose_from_split;
      break;
    default:
      error("unknown color strategy: %d", cs);
      abort();
  }
}

void SetIncludeInSplitStrategy(IncludeInSplit is)
{
  switch(is)
  {
    case WHEN_NOT_FULL:
      include_in_split_strategy = &when_not_full;
      break;
    case WHEN_ENOUGH_COLORS: 
      include_in_split_strategy = &when_enough_colors;
      break;
    case WHEN_NOT_TOO_MANY_NEIGHBORS:
      include_in_split_strategy = &when_not_too_many_neighbors;
      break;
   default:
      error("unknown include in split strategy: %d", is);
      abort();
  }
}

void SetWhenToSplitStrategy(WhenToSplit ws)
{
  switch(ws)
  {
    case NO_COLOR_AVAILABLE:
      when_to_split_strategy = &no_color_available;
      break;
    case NUM_NEIGHBORS_TOO_GREAT:
      when_to_split_strategy = &num_neighbors_too_great;
      break;
    default:
      error("unknown when to split strategy: %d", ws);
      abort();
  }
}

void SetHowToSplitStrategy(HowToSplit hs)
{
  switch(hs)
  {
    case CHOW_SPLIT:
      how_to_split_strategy = &chow_split;
      break;
    case UP_AND_DOWN_SPLIT:
      how_to_split_strategy = &up_and_down_split;
      break;
    default:
      error("unknown how to split strategy: %d", hs);
      abort();
  }
}

/*
 * WHEN TO SPLIT STRATEGIES
 */
bool SplitWhenNoColorAvailable::operator()(LiveRange* lr) 
{
  return !lr->HasColorAvailable();
}

bool SplitWhenNumNeighborsTooGreat::operator()(LiveRange* lr)
{
  double uncolored_neighbors = 
    lr->fear_list->size() - lr->num_colored_neighbors;
  double num_colors = Coloring::NumColorsAvailable(lr);
  char str[64]; LRName(lr, str);
  debug("(%s) uncolored: %.2f, avail: %.2f, ratio: %.2f max: %.2f", str,
    uncolored_neighbors, num_colors, uncolored_neighbors/num_colors,
    max_ratio);
  if(num_colors == 0) return true;
  return ((uncolored_neighbors/num_colors) > max_ratio);
}

/*
 * WHEN TO INCLUDE IN SPLIT STRATEGIES
 */
bool IncludeWhenNotFull::operator()
      (LiveRange* lrnew, LiveRange* lrorig, Block* blk)
{
  bool include = false;
  //we can include this block in the split if it does not max out the
  //forbidden set
  if(ColorsLeftAfterBlock(lrnew, blk) > 0)
  {
    include = true;
  }
  return include;

}

void IncludeWhenNotTooManyNeighbors::Reset(LiveUnit* lu)
{
  debug("resetting");
  neighbors.clear();
  AddNeighbors(lu->live_range, lu->block);
}

struct AddIfBlockEq : public std::unary_function<LiveRange*, void>
{
  Block* blk; std::set<LiveRange*>& lrset;
  AddIfBlockEq(Block* _blk, std::set<LiveRange*>& _lrset) 
    : blk(_blk), lrset(_lrset){};
  void operator()(LiveRange* lr)
  {
    if(lr->ContainsBlock(blk) && lr->color == Coloring::NO_COLOR) 
      lrset.insert(lr);
  }
};
void IncludeWhenNotTooManyNeighbors::AddNeighbors(LiveRange* lr,Block* blk)
{
  //go through all the live units that include this block and add them
  //as neighbors if they are a candidate or have been assigned a color
  debug("live unit size: %d", (int)Chow::live_units[bid(blk)].size());
  std::for_each(
    lr->fear_list->begin(), lr->fear_list->end(), 
    AddIfBlockEq(blk, neighbors));
}

bool IncludeWhenNotTooManyNeighbors::operator()
      (LiveRange* lrnew, LiveRange* lrorig, Block* blk)
{
  debug("Checking to see if blk should be included for lr: %d_%d",
    lrnew->orig_lrid, lrnew->id);
  /* check that we do not add more neighbors than available colors */
  double num_colors_before = Coloring::NumColorsAvailable(lrnew);
  int num_neighbors_before = neighbors.size();
  AddNeighbors(lrorig, blk);
  int num_neighbors_after = neighbors.size();
  debug("colors before: %.0f, nebs before %d, nebs after %d",
    num_colors_before, num_neighbors_before, num_neighbors_after);
  if((num_neighbors_after - num_neighbors_before) > num_colors_before)
  {
    debug("do not include %s(%d) it adds too many neighbors",
      bname(blk), bid(blk));
    return false;
  }

  // check that the total number of neighbors/colors does not exceed
  // the ratio 
  double num_colors_after  = ColorsLeftAfterBlock(lrnew, blk);

  debug("nebs: %d, colors: %.0f, ratio: %.2f", num_neighbors_after,
    num_colors_after, ((double)num_neighbors_after/num_colors_after));

  if(num_colors_after == 0) 
    return false;

  if(((double)num_neighbors_after)/num_colors_after > max_ratio)
  {
    debug("do not include %s(%d) its neb/color ratio is too big",
      bname(blk), bid(blk));
    return false;
  }

  return true;
}

void IncludeWhenEnoughColors::Reset(LiveUnit* lu)
{
  min_colors = fixed_min > 0 ? 
    fixed_min : max(Coloring::NumColorsAvailable(lu->live_range)/2, 1);
}

bool IncludeWhenEnoughColors::operator()
        (LiveRange* lrnew, LiveRange* lrorig, Block* blk)
{
  int left = ColorsLeftAfterBlock(lrnew, blk);
  return left >= min_colors;
}

/*
 * HOW TO COLOR STRATEGIES
 */
Color
ChooseFirstColor::operator()(
  const LiveRange* lr,
  const std::vector<Color>& choices
)
{
  return choices.front();
}

Color
ChooseColorFromMostConstrainedNeighbor::operator()(
  const LiveRange* lr,
  const std::vector<Color>& choices
)
{
  using Coloring::NO_COLOR;
  typedef LazySet::iterator SI;
  typedef std::vector<LiveRange*>::iterator LI;
  typedef std::vector<Color>::const_iterator CI;

  //look at all the live ranges that the live range interferes with
  //and see which ones have a forbidden color the same as one of the
  //choices for the given live range
  std::vector<LiveRange*> lr_choices;
  for(SI si = lr->fear_list->begin(); si != lr->fear_list->end(); si++)
  {
    for(CI ci = choices.begin(); ci != choices.end(); ci++)
    {
      if(VectorSet_Member((*si)->forbidden, *ci))
      {
        lr_choices.push_back(*si);
        break;
      }
    }
  }
  //if none of the colors to pick from is already in the forbidden
  //list of another live range then just pick the first available
  if(lr_choices.empty())
  {
    debug("no neighbors with forbidden colors, picking any");
    return choices.front();
  }

  //otherwise find the live range that has the most forbidden colors
  int max_forbidden = -1;
  LiveRange* max_lr = NULL;
  for(LI li = lr_choices.begin(); li != lr_choices.end(); li++)
  {
    int size = VectorSet_Size((*li)->forbidden);
    if(size > max_forbidden)
    {
      max_forbidden = size;
      max_lr = (*li);
    }
  }
  assert(max_lr != NULL);

  //now choose the color that is available as a choice for this live
  //range and also in the forbidden set of the live range with the
  //most forbidden colors
  Color color = NO_COLOR;
  for(CI ci = choices.begin(); ci != choices.end(); ci++)
  {
    if(VectorSet_Member(max_lr->forbidden, *ci))
    {
      color = *ci; break;
    }
  }
  debug("choosing color: %d from lr: %d_%d with %d forbidden",
    color, max_lr->orig_lrid, max_lr->id, max_forbidden);

  assert(color != NO_COLOR);
  return color;
}

Color 
ChooseColorInMostNeighborsForbidden::operator()(
  const LiveRange* lr, 
  const std::vector<Color>& choices
)
{
  typedef LazySet::iterator SI;
  typedef std::vector<Color>::const_iterator CI;
  typedef std::map<Color,int>::const_iterator CCI;

  std::map<Color, int> color_count_map;
  for(CI ci = choices.begin(); ci != choices.end(); ci++)
  {
    for(SI si = lr->fear_list->begin(); si != lr->fear_list->end(); si++)
    {
      if(VectorSet_Member((*si)->forbidden, *ci))
      {
        color_count_map[*ci]++;
      }
    }
  }
  return FindMaxOrDefault(color_count_map, choices);
}

Color 
ChooseColorFromSplit::operator()(
  const LiveRange* lr, 
  const std::vector<Color>& choices
)
{
  typedef std::vector<Color>::const_iterator CI;
  typedef std::vector<LiveRange*>::const_iterator LI;

  std::map<Color, int> color_count_map;
  for(CI ci = choices.begin(); ci != choices.end(); ci++)
  {
    for(LI li = lr->splits->begin(); li != lr->splits->end(); li++)
    {
      if((*li)->color == *ci) color_count_map[*ci]++;
    }
  }

  //find max
  typedef std::map<Color,int>::const_iterator CCI;
  return FindMaxOrDefault(color_count_map, choices);

}

/*
 * HOW TO SPLIT STRATEGIES
 */
void 
SplitStrategy::operator()(
  LiveRange* _newlr,
  LiveRange* _origlr,
  LiveUnit* startunit
)
{
  //set instance variables used in splitting functions
  newlr = _newlr;
  origlr = _origlr;
  include_in_split_strategy->Reset(startunit);

  //keep a queue of successors that we may add to the new live range
  std::list<Block*> fringe_list;
  fringe_list.push_back(startunit->block);
  while(!fringe_list.empty())
  {
    Block* blk = RemoveFringeNode(fringe_list);
    ExpandFringeNode(blk, fringe_list);
  }

}

void 
SplitStrategy::ExpandSuccs(Block* blk, FringeList& fringe)
{
  Edge* e;
  Block_ForAllSuccs(e, blk)
  {
    Block* succ = e->succ;
    if(origlr->ContainsBlock(succ) && IncludeInSplit(succ))
    {
      LiveUnit* unit = origlr->LiveUnitForBlock(succ);
      debug("adding block: %s to  lr'", bname(succ));
      origlr->TransferLiveUnitTo(newlr, unit);
      fringe.push_back(succ); //explore the succs of this node
    }
  }
}

void 
SplitStrategy::ExpandPreds(Block* blk, FringeList& fringe)
{
  Edge* e;
  Block_ForAllPreds(e, blk)
  {
    Block* pred = e->pred;
    if(origlr->ContainsBlock(pred) && IncludeInSplit(pred))
    {
      LiveUnit* unit = origlr->LiveUnitForBlock(pred);
      debug("adding block: %s to  lr'", bname(pred));
      origlr->TransferLiveUnitTo(newlr, unit);
      fringe.push_back(pred); //explore the succs of this node
    }
  }
}

bool 
SplitStrategy::IncludeInSplit(Block* blk)
{
  return (*include_in_split_strategy)(newlr,origlr,blk);
}

Block* 
SplitStrategy::RemoveFront(FringeList& fringe)
{
  Block* blk = fringe.front(); fringe.pop_front();
  return blk;
}

/* 0 */
Block* 
ChowSplit::RemoveFringeNode(FringeList& fringe)
{
  return RemoveFront(fringe);
}

void 
ChowSplit::ExpandFringeNode(Block* blk, FringeList& fringe)
{
  ExpandSuccs(blk, fringe);
}

/* 1 */
Block* 
UpAndDownSplit::RemoveFringeNode(FringeList& fringe)
{
  return RemoveFront(fringe);
}

void 
UpAndDownSplit::ExpandFringeNode(Block* blk, FringeList& fringe)
{
  ExpandSuccs(blk, fringe);
  ExpandPreds(blk, fringe);
}


}
}//end Chow::Heuristics namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

unsigned int ColorsLeftAfterBlock(LiveRange* lr, Block* blk)
{
  VectorSet used_colors = RegisterClass::TmpVectorSet(lr->rc);
  VectorSet vsUsed = Coloring::UsedColors(lr->rc, blk);
  VectorSet_Union(used_colors, lr->forbidden, vsUsed);

  return Coloring::NumColorsAvailable(lr,used_colors);
}

Color FindMaxOrDefault(
  const std::map<Color,int>& color_count_map,
  const std::vector<Color>& choices
)
{
  using Coloring::NO_COLOR;
  typedef std::map<Color,int>::const_iterator CCI;

  Color max_color = NO_COLOR;
  int max_count = -1;
  for(CCI cci = color_count_map.begin(); cci != color_count_map.end(); cci++)
  {
    if((*cci).second > max_count)
    {
      max_count = (*cci).second;
      max_color = (*cci).first;
    }
  }

  Color color = NO_COLOR;
  if(max_color == NO_COLOR)
  {
    debug("no max color found, taking first available");
    color = choices.front();
  }
  else
  {
    debug("max color found: %d at count: %d", max_color, max_count);
    color = max_color;
  }

  return color;
}

}//end anonymous namespace

