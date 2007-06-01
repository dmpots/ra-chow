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
  Unsigned_Int clrRemat;
  Unsigned_Int clrFinal;
  Unsigned_Int clrColored;
  Unsigned_Int cSplits;
  Unsigned_Int cSpills;
  Unsigned_Int cZeroOccurrence;
  Unsigned_Int cChowStores;
  Unsigned_Int cChowLoads;
  Unsigned_Int cInsertedCopies;
  Unsigned_Int cThwartedCopies;
};

class Timer
{
public:
  typedef std::vector<std::pair<const char*, double> > SavedTimes;

private:
  double elapsed_time;
  const char* section;
  time_t tstart;
  SavedTimes saved_times;

public:
  void Start(const char* = "");
  double Stop();
  const char*  ElapsedStr(); 
  inline double Elapsed() {return elapsed_time;}
  const SavedTimes GetSavedTimes(){return saved_times;}

};


/*-------------------------VARIABLES---------------------------*/
extern ChowStats chowstats;
extern Timer program_timer;
extern Timer section_timer;

/*-------------------------FUNCTIONS---------------------------*/
void ComputeBBStats(Arena, Unsigned_Int);
BBStats GetStatsForBlock(Block* blk, LRID lrid);
void DumpAllocationStats();
void Start(const char*); //timing functions
void Stop();  //timing functions
}

#endif

