/* contains functions and data for statistics used in chow allocator.
 * this includes statistics about the allocation itself as well as
 * static properties of the program text such as the number of uses
 * and defs of a variable in a given basic block.
 */

#ifndef __GUARD_STATS_H
#define __GUARD_STATS_H

/*----------------------------INCLUDES----------------------------*/
#include <Shared.h>
#include "types.h"
#include "debug.h"

namespace Stats {
/*----------------------------TYPES------------------------------*/
//usage statistics for variables
struct BBStats
{
  Unsigned_Int defs;
  Unsigned_Int uses;
  Boolean start_with_def;
};

//statistics for allocation
struct ChowStats
{
  Unsigned_Int clrInitial;
  Unsigned_Int clrFinal;
  Unsigned_Int clrColored;
  Unsigned_Int cSplits;
  Unsigned_Int cSpills;
  Unsigned_Int cChowStores;
  Unsigned_Int cChowLoads;
  Unsigned_Int cInsertedCopies;
  Unsigned_Int cThwartedCopies;
};

/*-------------------------VARIABLES---------------------------*/
extern ChowStats chowstats;

/*-------------------------FUNCTIONS---------------------------*/
void ComputeBBStats(Arena, Unsigned_Int);
BBStats GetStatsForBlock(Block* blk, LRID lrid);
void DumpAllocationStats();
}

#endif

