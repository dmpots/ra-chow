/* contains functions and data for implementing rematerialization in
 * the chow allocator.
 */

#ifndef __GUARD_REMATERIALIZE_H
#define __GUARD_REMATERIALIZE_H

/*----------------------------INCLUDES----------------------------*/
#include <Shared.h>
#include <vector>
#include "types.h"
#include "debug.h"
#include "union_find.h"


namespace Remat {
/*---------------------------TYPES-----------------------------*/
enum LatticeVal {TOP = 0, CONST = -1, BOTTOM = -2};
struct LatticeElem
{
  LatticeVal val;
  Operation* op;
};
typedef std::vector<std::pair<Variable,Variable> > SplitList;

/*-------------------------VARIABLES---------------------------*/
extern std::vector<LatticeElem> tags;
extern UFSet** remat_sets;

/*-------------------------FUNCTIONS---------------------------*/
void ComputeTags();
void DumpTags();
void AddSplit(Variable parent_ssa_name, Variable child_ssa_name);
const SplitList& GetSplits(void);
bool TagsAllEqual(Variable v1, Variable v2);
void SplitRematerializableLiveRanges();
}

#endif

