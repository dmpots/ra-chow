/* contains functions and data for statistics used in chow allocator.
 * this includes statistics about the allocation itself as well as
 * static properties of the program text such as the number of uses
 * and defs of a variable in a given basic block.
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include "stats.h"
#include "mapping.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
Stats::BBStats** bb_stats = NULL;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Stats {
ChowStats chowstats = {0};

/*
 *===================
 * Stats::ComputeBBStats()
 *===================
 * Gathers statistics about variables on a per-live range basis.
 * Collects:
 *  1) number of uses in the block
 *  2) number of defs in the block
 *  3) if the first occurance is a def
 ***/
void ComputeBBStats(Arena arena, Unsigned_Int variable_count)
{
  using Mapping::SSAName2LRID;

  Block* b;
  Inst* inst;
  Operation** op;
  Variable* reg;
  BBStats* bstats;


  //allocate space to hold the stats
  bb_stats = (BBStats**)
    Arena_GetMemClear(arena, sizeof(BBStats*) * (block_count + 1));
  ForAllBlocks(b)
  {
    bb_stats[id(b)] = (BBStats*) 
      Arena_GetMemClear(arena, sizeof(BBStats) * variable_count);
  }

  LRID lrid;
  ForAllBlocks(b)
  {
    bstats = bb_stats[id(b)];
    Block_ForAllInsts(inst, b)
    {
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].uses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].defs++;
          if(bstats[lrid].uses == 0)
            bstats[lrid].start_with_def = TRUE;
        }
      } 
    }
  }
}

/*
 *==========================
 * Stats::GetStatsForBlock()
 *==========================
 * Get statistics about a liverange in the given basic block
 ***/
BBStats GetStatsForBlock(Block* blk, LRID lrid)
{
  return bb_stats[id(blk)][lrid];
}

}
/*------------------INTERNAL MODULE FUNCTIONS--------------------*/

