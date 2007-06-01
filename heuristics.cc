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
template<class T>
inline T max(T a, T b){return a > b ? a : b;}

unsigned int ColorsLeftAfterBlock(LiveRange* lr, Block* blk);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Chow {
namespace Heuristics {
/*
 * DEFAULT STRATEGIES
 */
SplitWhenNoColorAvailable no_color_available;
SplitWhenNumNeighborsTooGreat num_neighbors_too_great;

IncludeWhenNotFull when_not_full;
IncludeWhenEnoughColors when_enough_colors;
IncludeWhenNotTooManyNeighbors when_not_too_many_neighbors;


//default heuristics
WhenToSplitStrategy& default_when_to_split = no_color_available;
IncludeInSplitStrategy& default_include_in_split = when_not_full;

//chow's heuristics
//WhenToSplitStrategy& default_when_to_split = num_neighbors_too_great;
//IncludeInSplitStrategy& default_include_in_split=when_not_too_many_neighbors;

//dave's heuristics
//WhenToSplitStrategy& default_when_to_split = no_color_available;
//IncludeInSplitStrategy& default_include_in_split = when_enough_colors;
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
  debug("live unit size: %d", (int)Chow::live_units[id(blk)].size());
  /*
  for(unsigned int i = 0; i < Chow::live_units[id(blk)].size(); i++)
  {
    LiveUnit* lu = Chow::live_units[id(blk)][i];
    if(lu->live_range->is_candidate || 
       lu->live_range->color != Coloring::NO_COLOR)
    {
      neighbors.insert(lu->live_range);
    }
  }
  */
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
      bname(blk), id(blk));
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
      bname(blk), id(blk));
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

}//end anonymous namespace

