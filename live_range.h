#ifndef __GUARD_LIVE_RANGE_H
#define __GUARD_LIVE_RANGE_H

#include <set>
#include <list>
#include <vector>
#include "types.h"


/* forward definition of a comparison object used by the std::set
 * implementation to compare LiveRanges. implementation of this struct
 * follows below */
struct LRcmp;

/* forward definition of LiveUnit structure */
struct LiveUnit;


/* a live range is the unit of allocation for the register allocator.
 * these will be assigned a color */
typedef float Priority;
struct LiveRange
{
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
  RegisterClass rc;
  Expr tag;
};

/* compares two live ranges for set container based in lrid */
struct LRcmp
{
  inline bool operator()(const LiveRange* lr1, const LiveRange* lr2) const
  {
    return lr1->id < lr2->id;
  }
};


/* convienient typedefs for live range containers */
typedef std::vector<LiveRange*> LRList;
typedef std::set<LiveRange*, LRcmp> LRSet;

/*---------------------LIVE RANGE FUNCTIONS-------------------------*/
void LiveRange_AllocLiveRanges(Arena, LRList&, Unsigned_Int);
void LiveRange_AddInterference(LiveRange*, LiveRange*);
bool LiveRange_Constrained(LiveRange*);
void LiveRange_MarkNonCandidateAndDelete(LiveRange* lr);
void LiveRange_AssignColor(LiveRange* lr);
void LiveRange_SplitNeighbors(LiveRange* lr, LRSet* , LRSet*);
LiveUnit* LiveRange_LiveUnitForBlock(LiveRange* lr, Block* b);
Boolean LiveRange_ContainsBlock(LiveRange* lr, Block* b);
void LiveRange_MarkLoadsAndStores(LiveRange* lr);
LiveUnit* AddLiveUnitOnce(LRID lrid, Block* b, VectorSet lrset, Variable orig_name);
Opcode_Names LiveRange_LoadOpcode(LiveRange* lr);
Opcode_Names LiveRange_StoreOpcode(LiveRange* lr);
Opcode_Names LiveRange_CopyOpcode(const LiveRange* lr);
Expr LiveRange_GetTag(LiveRange* lr);
Unsigned_Int LiveRange_GetAlignment(LiveRange* lr);
MemoryLocation LiveRange_MemLocation(LiveRange* lr);
RegisterClass LiveRange_RegisterClass(LiveRange* lr);
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs);

inline void LRName(const LiveRange* lr, char* buf)
{
  sprintf(buf, "%d_%d", lr->orig_lrid, lr->id);
}

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



/************************** FIXME *******************************/
//FIXME: put this in a LRContainers file or something
//LRList
//HERE <-- moving these in live_range.c
Boolean LRList_Empty(LRList lrs);
void LRList_Add(LRList* lrs, LiveRange* lr);
LiveRange* LRList_Pop(LRList* lrs);
Unsigned_Int LRList_Size(LRList* lrs);
void LRList_Remove(LRList* lrs, LiveRange* lr);
void LRList_AddUnique(LRList* lrs, LiveRange* lr);

//LRSet
void LRSet_Add(LRSet* lrs, LiveRange* lr);
void LRSet_Remove(LRSet* lrs, LiveRange* lr);
void LRSet_UpdateConstrainedLists(LRSet* , LRSet* , LRSet* ); 


#define LRList_ForAll(lrs, lr) \
for (LRList::iterator i = ((lrs)->begin()); \
     i != ((lrs)->end());\
     i++) \
   if(((lr) = *i) || TRUE) 

#define LRList_ForAllCandidates(lrs, lr) \
for (LRList::iterator i = ((lrs)->begin()); \
     i != ((lrs)->end());\
     i++) \
   if(((lr) = *i) && lr->is_candidate) 

#define LRSet_ForAllCandidates(lrs, lr) \
for (LRSet::iterator i = ((lrs)->begin()); \
     i != ((lrs)->end());\
     i++) \
   if(((lr) = *i) && lr->is_candidate) 



//FIXME: put this in debug
void LiveRange_DumpAll(LRList*);
void LiveRange_DDumpAll(LRList* lrs);
void LiveRange_Dump(LiveRange* lr);
void LiveRange_DDump(LiveRange* lr);
void LiveUnit_Dump(LiveUnit* );
void LiveUnit_DDump(LiveUnit* unit);

/* FIXME: put this somewhere good */
/* constants */
const MemoryLocation MEM_UNASSIGNED = (MemoryLocation) -1;



#endif

