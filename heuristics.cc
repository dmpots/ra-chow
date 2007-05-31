/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

/*--------------------------INCLUDES---------------------------*/
#include "heuristics.h"
#include "live_range.h"
#include "color.h"

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
WhenToSplitStrategy& default_when_to_split = no_color_available;
//WhenToSplitStrategy& default_when_to_split = num_neighbors_too_great;

IncludeWhenNotFull when_not_full;
IncludeInSplitStrategy& default_include_in_split = when_not_full;

/*
 * WHEN TO SPLIT STRATEGIES
 */
bool SplitWhenNoColorAvailable::operator()(LiveRange* lr) 
{
  return !lr->HasColorAvailable();
}

bool SplitWhenNumNeighborsTooGreat::operator()(LiveRange* lr)
{
  double uncolored_neighbors = 0;
  for(LRSet::iterator it = lr->fear_list->begin();
      it != lr->fear_list->end();
      it++)
  {
    if((*it)->color == Coloring::NO_COLOR) uncolored_neighbors += 1.0;
  }
  double num_colors = Coloring::NumColorsAvailable(lr);
  char str[64]; LRName(lr, str);
  debug("(%s) uncolored: %.2f, avail: %.2f, ratio: %.2f max: %.2f", str,
    uncolored_neighbors, num_colors, uncolored_neighbors/num_colors,
    max_ratio);
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

}
}//end Chow::Heuristics namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

}//end anonymous namespace

