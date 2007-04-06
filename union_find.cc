/*====================================================================
 * 
 *====================================================================
 * $Id: union_find.c 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/union_find.c $
 ********************************************************************/

#include "union_find.h"
#include "debug.h"

//globals
UFSet** uf_sets;
Unsigned_Int uf_set_count = 0;

//locals
static Arena uf_arena;

/*
 *===================
 * UFSets_Init()
 *===================
 * 
 ***/
void UFSets_Init(Arena arena, Unsigned_Int num_sets)
{
  uf_arena = arena;
  uf_sets = (UFSet**) 
    Arena_GetMemClear(uf_arena, sizeof(UFSet*) * num_sets);

  Unsigned_Int i;
  for(i = 0; i < num_sets; i++)
    uf_sets[i] = UFSet_Make(i); 

  uf_set_count = num_sets;
}


/*
 *===================
 * UFSet_Alloc()
 *===================
 * 
 ***/
UFSet* UFSet_Alloc()
{
  return (UFSet*)Arena_GetMemClear(uf_arena, sizeof(UFSet));
}


/*
 *===================
 * UFSet_Make()
 *===================
 * 
 ***/
UFSet* UFSet_Make(Variable v)
{
  UFSet* set = UFSet_Alloc();
  set->id = v;
  set->parent = NULL;
  set->rank = 0;

  return set;
}


/*
 *===================
 * UFSet_Find()
 *===================
 * 
 ***/
UFSet* UFSet_Find(UFSet* elem)
{
  UFSet* root;
  UFSet* runner;

  assert(elem != NULL);//trouble
  for(root = elem; root->parent != NULL; root = root->parent);
  if(elem != root)
    for(runner = elem->parent; runner != root; 
        elem = runner, runner = runner->parent)
    {
      elem->parent = root;
    }

  return root;
}

/*
 *===================
 * UFSet_Union()
 *===================
 * 
 ***/
UFSet* UFSet_Union(UFSet* set1, UFSet* set2)
{
  UFSet* s1 = UFSet_Find(set1);
  UFSet* s2 = UFSet_Find(set2);

  if(s1 == s2) return s1; //noop

  UFSet* top = NULL;
  //s1 rank greater
  if(s1->rank > s2->rank)
  {
    s2->parent = s1;
    top = s1;
  }
  else
  {
    //s2 greater or equal
    s1->parent = s2;
    top = s2;
    if(s1->rank == s2->rank)
    {
      s2->rank++;
    }
  }

  debug("union: %d v %d = %d", s1->id, s2->id, top->id);
  --uf_set_count; //union removes a set
  return top;
}

/*
 *===================
 * Find_Set()
 *===================
 * Returns pointer to the UFSet structure for the given variable
 **/
UFSet* Find_Set(Variable v)
{
  return UFSet_Find(uf_sets[v]);
}


