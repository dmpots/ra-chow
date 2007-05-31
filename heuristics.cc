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

}
}//end Chow::Heuristics namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

}//end anonymous namespace

