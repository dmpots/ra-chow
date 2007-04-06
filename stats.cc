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
  using Mapping::SSAName2OrigLRID;

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
          lrid = SSAName2OrigLRID(*reg);
          bstats[lrid].uses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2OrigLRID(*reg);
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

/*
 *======================
 * DumpAllocationStats()
 *======================
 *
 ***/
void DumpAllocationStats()
{
  using Stats::chowstats;
  fprintf(stderr, "***** ALLOCATION STATISTICS *****\n");
  fprintf(stderr, " Inital  LiveRange Count: %d\n",
                                           chowstats.clrInitial);
  fprintf(stderr, " Final   LiveRange Count: %d\n",
                                           chowstats.clrFinal);
  fprintf(stderr, " Colored LiveRange Count: %d\n",
                                           chowstats.clrColored+1);
  fprintf(stderr, " Spilled LiveRange Count: %d\n", 
                                           chowstats.cSpills-1);
  fprintf(stderr, " Number of Splits: %d\n", chowstats.cSplits);
  fprintf(stderr, " Inserted Copies : %d\n", chowstats.cInsertedCopies);
  fprintf(stderr, " Thwarted Copies : %d\n", chowstats.cThwartedCopies);
  fprintf(stderr, "***** ALLOCATION STATISTICS *****\n");
  //note: +/- 1 colored/spill count is for frame pointer live range
}



}
/*------------------INTERNAL MODULE FUNCTIONS--------------------*/

