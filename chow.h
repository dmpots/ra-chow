/*====================================================================
 * 
 *====================================================================
 ********************************************************************/

#ifndef __GUARD_CHOW_H
#define __GUARD_CHOW_H

#include <Shared.h>
#include "debug.h"
#include "types.h"

struct LiveRange;
namespace Chow {
  extern std::vector<LiveRange*> live_ranges;
  void Run();
}


#endif

