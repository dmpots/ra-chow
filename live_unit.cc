
/*-----------------------MODULE INCLUDES-----------------------*/
#include <cmath>

#include "live_unit.h"
#include "shared_globals.h"
#include "params.h"
#include "cfg_tools.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
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

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/
namespace {
}

