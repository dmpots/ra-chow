/* contains functions and data for mappings between namespaces used in
 * the chow allocator.
 */

#ifndef __GUARD_MAPPING_H
#define __GUARD_MAPPING_H

/*----------------------------INCLUDES----------------------------*/
#include <Shared.h>
#include "types.h"
#include "debug.h"

namespace Mapping {
/*-------------------------FUNCTIONS---------------------------*/
void CreateLiveRangeNameMap(Arena);
LRID SSAName2OrigLRID(Variable v);
void ConvertLiveInNamespaceSSAToLiveRange();
void ConvertLiveOutNamespaceSSAToLiveRange();
void CreateLiveRangeTypeMap(Arena, Unsigned_Int lr_count);
Def_Type LiveRangeDefType(LRID lrid);
void CreateSSANameTypeMap(Arena arena);
Def_Type SSANameDefType(Variable ssa_name);
}

#endif

