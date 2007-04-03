/*====================================================================
 * 
 *====================================================================
 * $Id: union_find.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/union_find.h $
 ********************************************************************/

#ifndef __GUARD_UNION_FIND_H
#define __GUARD_UNION_FIND_H

#include <Shared.h>
#include "types.h"

/* types */
struct uf_set
{
  struct uf_set* parent;
  Unsigned_Int id;
  Unsigned_Int rank;
};
typedef struct uf_set UFSet;

/* globals */
extern UFSet** uf_sets;
extern Unsigned_Int uf_set_count;

/* funcitons */
UFSet* UFSet_Make(Variable v);
UFSet* UFSet_Find(UFSet* elem);  
UFSet* UFSet_Union(UFSet* s1, UFSet* s2);
UFSet* UFSet_Alloc();
void UFSets_Init(Arena, Unsigned_Int);

//helper function to allow easy lookup of variables
UFSet* Find_Set(Variable v);

#endif


