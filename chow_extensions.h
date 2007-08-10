/* contains various enhancements to the chow algorithm that do not
 * appear in the original paper.
 */

#ifndef __GUARD_CHOW_EXTENSIONS_H
#define __GUARD_CHOW_EXTENSIONS_H

#include <Shared.h>
#include "types.h"
#include "live_range.h"

/* forward def */
struct LiveRange;
namespace Chow 
{
  namespace Extensions
  {
    void EnhancedCodeMotion(Edge*, Block*);
    void Trim(LiveRange*);
    Edge_Extension* AddEdgeExtensionNode(Edge*, MovedSpillDescription);
  }
}

#endif
