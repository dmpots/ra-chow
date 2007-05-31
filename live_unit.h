#ifndef __GUARD_LIVE_UNIT_H
#define __GUARD_LIVE_UNIT_H

#include <Shared.h>
#include <list>

#include "types.h"
#include "debug.h"

/* forward def */
struct LiveRange;

/* live units hold info about the basic blocks in a live range. there
 * is a 1-1 mapping from live units to basic blocks in a live range */
struct LiveUnit
{
  Boolean need_load;
  Boolean need_store;
  Boolean internal_store; /* is the need_store for an internal store*/
  Boolean dead_on_exit;
  Boolean start_with_def;
  int uses;
  int defs;
  Block* block;
  Variable orig_name;
  std::list<LiveUnit*> *lr_units;
  std::list<LiveUnit*> *bb_units;
  bool mark;
  LiveRange* live_range;
}; 

LiveUnit* LiveUnit_Alloc(Arena);

#endif
