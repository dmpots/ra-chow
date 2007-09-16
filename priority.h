/* contains various priority functions for deciding which live ranges
 * should be allocated a register.
 */

#ifndef __GUARD_PRIORITY_H
#define __GUARD_PRIORITY_H

#include <Shared.h>
#include "types.h"

/* forward def */
struct LiveRange;
struct LiveUnit;
namespace Chow{
namespace PriorityFuns
{
  Priority Classic(LiveRange* lr);
  Priority NoNormal(LiveRange* lr);
  Priority SquareNormal(LiveRange* lr);
  Priority Gnu(LiveRange* lr);
  Priority GnuSquareNormal(LiveRange* lr);
}
}

#endif
