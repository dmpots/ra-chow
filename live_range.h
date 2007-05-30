#ifndef __GUARD_LIVE_RANGE_H
#define __GUARD_LIVE_RANGE_H

#include <Shared.h>
#include <set>
#include <list>
#include <vector>
#include <map>
#include "types.h"
#include "debug.h"
#include "stats.h"
#include "rc.h"

/*--------------------------FORWARD DEFS--------------------------*/
/* forward definition of a comparison object used by the std::set
 * implementation to compare LiveRanges. implementation of this struct
 * follows below */
struct LRcmp;

/* forward definition of LiveUnit structure */
struct LiveUnit;

/*-------------------LIVE RANGE DATA STRUCTURE--------------------*/
/* a live range is the unit of allocation for the register allocator.
 * these will be assigned a color */
typedef float Priority;
struct LiveRange
{
  /* class varaibles */
  static void Init(Arena,unsigned int); /* class initialization function */
  static Arena arena; /* for memory allocation needs */
  static VectorSet tmpbbset; /* for memory allocation needs */
  static const float UNDEFINED_PRIORITY;
  static unsigned int counter;

  /* constructor */
  LiveRange(RegisterClass::RC rc, LRID lrid, Def_Type);

  /* fields */
  VectorSet bb_list;  /* basic blocks making up this LR */ 
                      /* set of live range interferences */
  std::set<LiveRange*, LRcmp> *fear_list;
  VectorSet forbidden; /* forbidden colors for this LR */
  std::list<LiveUnit*> *units;  /* live units making up this LR */ 
  Color color;  /* color assigned to this LR */
  Variable orig_lrid;  /* original variable for this live range */
  Variable id;  /* unique id for this live range */
  Priority priority; /* priority for this to be in a live range */
  Boolean is_candidate; /* is possible to store this in a register */
  Def_Type type;
  RegisterClass::RC rc;
  bool rematerializable;
  Operation* remat_op;

  /* maps from block id --> live range, for keeping track of splits */
  std::map<unsigned int, LiveRange*> *blockmap; 
  /* keeps track of all the live range split from this one */
  std::vector<LiveRange*> *splits; 

  /* methods */
  void AddInterference(LiveRange* other);
  bool IsConstrained() const;
  void MarkNonCandidateAndDelete();
  void AssignColor();
  LiveUnit* LiveUnitForBlock(Block* b) const;
  bool ContainsBlock(Block* b) const;
  void MarkLoadsAndStores();
  Opcode_Names LoadOpcode() const;
  Opcode_Names StoreOpcode() const;
  Opcode_Names CopyOpcode() const;
  unsigned int Alignment() const;
  LiveRange* Split();
  Boolean IsEntirelyUnColorable() const;
  Boolean HasColorAvailable() const;
  Boolean InterferesWith(LiveRange* lr2) const;
  LiveUnit* AddLiveUnitForBlock(Block*, Variable, const Stats::BBStats& );
  Priority LiveRange::ComputePriority();
  LiveRange* Mitosis();
  void TransferLiveUnitTo(LiveRange* to, LiveUnit* unit);
  void RebuildForbiddenList();
  void RemoveLiveUnit(LiveUnit*);
  unsigned int RegWidth() const;

  /* iterators */
  /* for live units in this live range */
  typedef std::list<LiveUnit*>::iterator iterator;
  iterator begin() const;
  iterator end() const;
};

/* compares two live ranges for set container based in lrid */
struct LRcmp
{
  inline bool operator()(const LiveRange* lr1, const LiveRange* lr2) const
  {
    return lr1->id < lr2->id;
  }
};


/*---------------------HELPER FUNCTIONS-------------------------*/
inline void LRName(const LiveRange* lr, char* buf)
{
  sprintf(buf, "%d_%d", lr->orig_lrid, lr->id);
}


/*-------------------SPILLING DATA STRUCTURES-----------------------*/
/* for moving loads and stores onto edges so that we can do code
 * motion as described in chow */
enum SpillType  {STORE_SPILL, LOAD_SPILL};
struct MovedSpillDescription
{
  LiveRange* lr;
  SpillType spill_type;
  Block* orig_blk;
};

/* edge extension must be defined to attach this on edges so that we
 * can do code motion */
struct edge_extension
{
  std::list<MovedSpillDescription>* spill_list;
};

#endif

