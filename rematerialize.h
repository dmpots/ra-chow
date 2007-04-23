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

namespace Remat {
/*---------------------------TYPES-----------------------------*/
enum LatticeVal {TOP = 0, CONST = -1, BOTTOM = -2};
struct LatticeElem
{
  LatticeVal val;
  Operation* op;
};

/*-------------------------VARIABLES---------------------------*/
extern std::vector<LatticeElem> tags;

/*-------------------------FUNCTIONS---------------------------*/
void ComputeTags();
void DumpTags();
}

#endif

