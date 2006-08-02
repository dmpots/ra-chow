/*====================================================================
 * 
 *====================================================================
 * $Id: live.c 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/live.c $
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <assert.h>
#include "live.h"
#include "debug.h"

//array of VectorSets, one per block
static VectorSet* _live_in  = NULL;
static VectorSet* _live_out = NULL;

static void initialize_sets(void);
//static void initialize_live_out(VectorSet*);
static void alloc_extensions(Arena, Unsigned_Int);
static void live_iter(Arena, Unsigned_Int);

/* define the struct that we will hang off of each block for our analysis */
struct block_extension
{
  VectorSet live_in;
  VectorSet live_out;
  VectorSet ueVars;
  VectorSet varKill;
};

liveness_result compute_liveness(Arena live_arena, Unsigned_Int name_count)
{
  
  //compute the liveness

  alloc_live_sets(live_arena, block_count, name_count);
  alloc_extensions(live_arena, name_count);
  
  /* init live out to empty, and compute UEVars and VARKill */
  initialize_sets();
  
  /* do the iterative algorithm to compute the sets */
  live_iter(live_arena, name_count);
  
  
  /* set the return value */
  Block* block;
  liveness_result res;
  res.live_in = _live_in;
  res.live_out = _live_out;

  /* fill the live sets from the block extensions */
  ForAllBlocks(block)
    {
      _live_out[block->preorder_index] = block->block_extension->live_out;
      _live_in[block->preorder_index] = block->block_extension->live_in;
    }
  return res;
} /* compute_liveness */

/*****************************************************************************
 *  live_iter()
 * perform the iterative data flow algorithm
 * to compute the live sets.
 ***/
void live_iter(Arena arena, Unsigned_Int nRegs)
{
  Block* b = NULL;
  Edge* edge = NULL;
  Boolean changed = TRUE;

  /* used to test when a live_out set has changed so we can stop iteraing */
  VectorSet new_live_out = VectorSet_Create(arena, nRegs);

  while(changed)
    {
      changed = FALSE;
      ForAllBlocks_rPreorder(b)
        {
          VectorSet_Clear(new_live_out);
          
          /* take union of live in over all the successors */
          Block_ForAllSuccs(edge, b)
            {
              Block* succ = edge->succ;
              VectorSet_Union(new_live_out,
                              succ->block_extension->live_in,
                              new_live_out);
            }
          
          /* recompute my value of live in for my predecessors */
          /* LiveIn = UEVars v (LiveOut ^ ~Varkill) */
          VectorSet_DifferenceUnion(b->block_extension->live_in,
                                    new_live_out,
                                    b->block_extension->varKill,
                                    b->block_extension->ueVars);
          if(!VectorSet_Equal(b->block_extension->live_out, new_live_out))
            {
              VectorSet_Copy(b->block_extension->live_out, new_live_out);
              changed = TRUE;
            }
        }
    }
}


/*****************************************************************************
 *  initialize_sets()
 * compute the initial values for the live_in, live_out, UEVars, and VARKilled
 * sets.
 ***/
void initialize_sets()
{
  Block* block;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;

  //temp vars to make te access shorter
  VectorSet ueVars = NULL;
  VectorSet varKill = NULL;

  /* iterate through the blocks and initialize all of the sets */
  ForAllBlocks(block)
    {
      VectorSet_Clear(block->block_extension->live_out);
      VectorSet_Clear(block->block_extension->live_in);
      VectorSet_Clear(block->block_extension->ueVars);      
      VectorSet_Clear(block->block_extension->varKill);
      
      /* put in temp var to access below just so the code
       * looks nicer */
      varKill = block->block_extension->varKill;
      ueVars = block->block_extension->ueVars;

      /* compute the correct varKill and ueVars values */
      Block_ForAllInsts(inst, block)
        {
          Inst_ForAllOperations(op, inst)
            {

              /* UPWARD EXPOSED */
              Operation_ForAllUses(reg, *op)
                {
                  if(!VectorSet_Member(varKill,*reg))
                    { 
                      //debug("(%d)insert use: %d",block->preorder_index,*reg);
                      VectorSet_Insert(ueVars,*reg);
                    }
                }

              /* KILLED */
              Operation_ForAllDefs(reg, *op)
                {
                  //debug("(%d)insert def: %d",block->preorder_index,*reg);
                  VectorSet_Insert(varKill,*reg);                  
                }
              

            } 
        }
    }
}

/*****************************************************************************
 *  alloc_live_sets()
 * allocate the memory for the vector set array and create a vector set to go
 * in each location
 **/
void alloc_live_sets(Arena arena,
                     Unsigned_Int nBlocks, 
                     Unsigned_Int nRegs)
{
  Block* block;
  _live_out = (VectorSet* )Arena_GetMem(arena, 
                                        (sizeof(VectorSet) * (nBlocks + 1)));
  _live_in = (VectorSet* )Arena_GetMem(arena, 
                                       (sizeof(VectorSet) * (nBlocks + 1)));
  
  ForAllBlocks(block)
    {
      _live_in[block->preorder_index] = VectorSet_Create(arena, nRegs);
      _live_out[block->preorder_index] = VectorSet_Create(arena, nRegs);
    }
}


void alloc_extensions(Arena arena, Unsigned_Int nRegs)
{
  Block* block;
  ForAllBlocks(block)
    {
      /* hang an extenstion off of each block */
      block->block_extension = 
        (Block_Extension *) 
        Arena_GetMem(arena, (sizeof(struct block_extension)));
      
      /* allocate the extenstions */
      block->block_extension->live_in = VectorSet_Create(arena, nRegs);
      block->block_extension->live_out = VectorSet_Create(arena, nRegs);
      block->block_extension->ueVars = VectorSet_Create(arena, nRegs);
      block->block_extension->varKill = VectorSet_Create(arena, nRegs);
    }
}
