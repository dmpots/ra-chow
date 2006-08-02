/*====================================================================
 * 
 *====================================================================
 * $Id: live_range.h 160 2006-07-27 19:24:52Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/live_range.h $
 ********************************************************************/

#ifndef __GUARD_LIVE_RANGE_H
#define __GUARD_LIVE_RANGE_H

#include <list>
#include <set>
#include <vector>
#include "types.h"

/* types */
typedef float Priority;

/* constants */
const MemoryLocation MEM_UNASSIGNED = (MemoryLocation) -1;
const Expr TAG_UNASSIGNED = (Expr) -1;

/* live units hold info about the basic blocks in a live range. there
 * is a 1-1 mapping from live units to basic blocks in a live range */
typedef 
struct live_unit
{
  Boolean need_load;
  Boolean need_store;
  Boolean dead_on_exit;
  Boolean start_with_def;
  Unsigned_Int uses;
  Unsigned_Int defs;
  Block* block;
  Variable orig_name;
  std::list<struct live_unit*> *lr_units;
  std::list<struct live_unit*> *bb_units;
} LiveUnit;

/* forward definition of a comparison object used by the std::set
 * implementation to compare LiveRanges. implementation of this struct
 * follows below */
struct LRcmp;

/* a live range is the unit of allocation for the register allocator.
 * these will be assigned a color */
typedef
struct live_range
{
  VectorSet bb_list;  /* basic blocks making up this LR */ 
  /* set of live range interferences */
  std::set<struct live_range*, struct LRcmp> *fear_list;
  VectorSet forbidden; /* forbidden colors for this LR */
  std::list<LiveUnit*> *units;  /* live units making up this LR */ 
  Color color;  /* color assigned to this LR */
  Variable orig_lrid;  /* original variable for this live range */
  Variable id;  /* unique id for this live range */
  Priority priority; /* priority for this to be in a live range */
  Boolean is_candidate; /* is possible to store this in a register */
  Def_Type type;
  Expr tag;
} LiveRange;

/* compares two live ranges for set container based in lrid */
struct LRcmp
{
  bool operator()(const LiveRange* lr1, const LiveRange* lr2) const
  {
    return lr1->id < lr2->id;
  }
};

/* defined here so that we can DumpAll() live ranges */
typedef std::vector<LiveRange*> LRList;
typedef std::set<LiveRange*, LRcmp> LRSet;
extern LRList live_ranges;
extern Unsigned_Int liverange_count;

/* hold pairs of live range */
typedef
struct lr_tuple
{
  LiveRange* fst;
  LiveRange* snd;
} LRTuple;

/* functions */
//LiveRange
void LiveRange_AllocLiveRanges(Arena, LRList&, Unsigned_Int);
LiveRange* LiveRange_Create(Arena);
void LiveRange_Alloc(Arena);
LiveUnit* LiveRange_AddLiveUnit(LiveRange*, LiveUnit*);
LiveUnit* LiveRange_AddLiveUnitBlock(LiveRange*, Block*);
void LiveRange_RemoveLiveUnit(LiveRange* , LiveUnit* );
void LiveRange_RemoveLiveUnitBlock(LiveRange* lr, Block* b);
void LiveRange_TransferLiveUnit(LiveRange*, LiveRange*, LiveUnit*);
void LiveRange_AddInterference(LiveRange*, LiveRange*);
Boolean LiveRange_Constrained(LiveRange*);
void LiveRange_MarkNonCandidateAndDelete(LiveRange* lr);
Boolean LiveRange_UnColorable(LiveRange* lr);
void LiveRange_RemoveInterference(LiveRange* from, LiveRange* with);
void LiveRange_AssignColor(LiveRange* lr);
void LiveRange_SplitNeighbors(LiveRange* lr, LRSet* , LRSet*);
LRTuple LiveRange_Split(LiveRange* lr, LRSet* , LRSet* );
Boolean LiveRange_RegistersAvailable(LiveRange* lr);
LiveUnit* LiveRange_ChooseSplitPoint(LiveRange*);
LiveUnit* LiveRange_IncludeInSplit(LiveRange*, LiveRange*, Block*);
LiveUnit* LiveRange_LiveUnitForBlock(LiveRange* lr, Block* b);
void LiveRange_AddBlock(LiveRange* lr, Block* b);
void LiveRange_RebuildInterference(LiveRange* lr);
void LiveRange_UpdateAfterSplit(LiveRange*,LiveRange*,LRSet*,LRSet*);
Boolean LiveRange_ContainsBlock(LiveRange* lr, Block* b);
Boolean LiveRange_EntryPoint(LiveRange* lr, LiveUnit* unit);
void LiveRange_MarkLoadsAndStores(LiveRange* lr);
void LiveRange_MarkLoads(LiveRange* lr);
void LiveRange_MarkStores(LiveRange* lr);
LiveUnit* AddLiveUnitOnce(LRID lrid, Block* b, VectorSet lrset, Variable orig_name);
Opcode_Names LiveRange_LoadOpcode(LiveRange* lr);
Opcode_Names LiveRange_StoreOpcode(LiveRange* lr);
Expr LiveRange_GetTag(LiveRange* lr);
Unsigned_Int LiveRange_GetAlignment(LiveRange* lr);
void LiveRange_InsertLoad(LiveRange* lr, LiveUnit* unit);
void LiveRange_InsertStore(LiveRange*lr, LiveUnit* unit);
LiveRange* LiveRange_SplitFrom(LiveRange* origlr);
MemoryLocation LiveRange_MemLocation(LiveRange* lr);



//temporary while resolving VectorSet_Equal problem
Boolean RegisterSet_Full(VectorSet set);
Boolean VectorSet_Empty(VectorSet set);

//Dumps
void LiveRange_DumpAll(LRList*);
void LiveRange_DDumpAll(LRList* lrs);
void LiveRange_Dump(LiveRange* lr);
void LiveRange_DDump(LiveRange* lr);
void LiveUnit_Dump(LiveUnit* );
void LiveUnit_DDump(LiveUnit* unit);

//LiveUnit
LiveUnit* LiveUnit_Alloc(Arena);

//LRList
Boolean LRList_Empty(LRList lrs);
void LRList_Add(LRList* lrs, LiveRange* lr);
LiveRange* LRList_Pop(LRList* lrs);
LiveRange* ComputePriorityAndChooseTop(LRSet* lrs);
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




#endif
