/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

/*--------------------------INCLUDES---------------------------*/
#include "heuristics.h"
#include "live_range.h"
#include "live_unit.h"
#include "color.h"
#include "chow.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
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
IncludeWhenNotTooManyNeighbors when_not_too_many_neighbors;


//default heuristics
//WhenToSplitStrategy& default_when_to_split = no_color_available;
//IncludeInSplitStrategy& default_include_in_split = when_not_full;

//chow's heuristics
WhenToSplitStrategy& default_when_to_split = num_neighbors_too_great;
IncludeInSplitStrategy& default_include_in_split=when_not_too_many_neighbors;
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
  VectorSet vsUsed = Coloring::UsedColors(lrorig->rc, blk);
  VectorSet used_colors = RegisterClass::TmpVectorSet(lrorig->rc);
  VectorSet_Union(used_colors, lrnew->forbidden, vsUsed);
  if(Coloring::IsColorAvailable(lrnew, used_colors))
  {
    include = true;
  }
  return include;

}

void IncludeWhenNotTooManyNeighbors::Reset(LiveUnit* lu)
{
  neighbors.clear();
  AddNeighbors(lu->block);
}

void IncludeWhenNotTooManyNeighbors::AddNeighbors(Block* blk)
{
  //go through all the live units that include this block and add them
  //as neighbors if they are a candidate or have been assigned a color
  debug("live unit size: %d", (int)Chow::live_units[id(blk)].size());
  for(unsigned int i = 0; i < Chow::live_units[id(blk)].size(); i++)
  {
    LiveUnit* lu = Chow::live_units[id(blk)][i];
    if(lu->live_range->is_candidate || 
       lu->live_range->color != Coloring::NO_COLOR)
    {
      neighbors.insert(lu->live_range);
    }
  }
}

bool IncludeWhenNotTooManyNeighbors::operator()
      (LiveRange* lrnew, LiveRange* lrorig, Block* blk)
{

  /* check that we do not add more neighbors than available colors */
  double num_colors_before = Coloring::NumColorsAvailable(lrnew);
  int num_neighbors_before = neighbors.size();
  AddNeighbors(blk);
  int num_neighbors_after = neighbors.size();
  debug("colors before: %.0f, nebs before %d, nebs after %d",
    num_colors_before, num_neighbors_before, num_neighbors_after);
  if((num_neighbors_before - num_neighbors_after) > num_colors_before)
  {
    debug("do not include %s(%d) it adds too many neighbors",
      bname(blk), id(blk));
    return false;
  }

  /* check that the total number of neighbors/colors does not exceed
   * the ratio */
  VectorSet used_colors = RegisterClass::TmpVectorSet(lrorig->rc);
  VectorSet vsUsed = Coloring::UsedColors(lrorig->rc, blk);
  VectorSet_Union(used_colors, lrnew->forbidden, vsUsed);
  double num_colors_after  = Coloring::NumColorsAvailable(lrnew,used_colors);

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

}
}//end Chow::Heuristics namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

}//end anonymous namespace

