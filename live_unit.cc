
#include "live_unit.h"

/*
 *======================
 * LiveUnit_Alloc()
 *======================
 *
 ***/
LiveUnit* LiveUnit_Alloc(Arena a)
{
  LiveUnit* unit =  (LiveUnit*)Arena_GetMemClear(a, sizeof(LiveUnit));
  unit->need_load = FALSE;
  unit->need_store = FALSE;
  unit->block = 0;
  return unit;
}


