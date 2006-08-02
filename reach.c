/*====================================================================
 * 
 *====================================================================
 * $Id: reach.c 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/reach.c $
 ********************************************************************/

#include <Shared.h>
#include "reach.h"
#include "debug.h"

struct block_extension
{
  VectorSet reach_in;
  VectorSet reach_out;
  VectorSet gen;
};

/* global variables */
static Arena reach_arena = NULL;
static VectorSet* _reach_in  = NULL;
static VectorSet* _reach_out = NULL;
static VectorSet* _refs = NULL;

/* local functions */
void initialize_sets(void);
void alloc_extensions(Arena, Unsigned_Int);
void alloc_reach_sets(Arena, Unsigned_Int, Unsigned_Int);
void reach_iter(void);
void dump_reaching(reaching_result res);


/*
 *======================
 * compute_reaching()
 *======================
 *
 ***/
reaching_result compute_reaching(Arena arena)
{
  //assign to global variables
  reach_arena = arena;
 
  //do analyasis 
  initialize_sets();
  reach_iter();

  /* set the return value */
  reaching_result reaching;
  reaching.reach_in = _reach_in;
  reaching.reach_out = _reach_out;
  reaching.refs = _refs;

  /* fill the live sets from the block extensions */
  Block* block;
  ForAllBlocks(block)
    {
      _reach_out[id(block)] = block->block_extension->reach_out;
      _reach_in[id(block)] = block->block_extension->reach_in;
      _refs[id(block)] = block->block_extension->gen;
    }
  //dump_reaching(reaching);
  return reaching;
}

/*
 *======================
 * reach_iter()
 *======================
 *
 ***/
void reach_iter()
{
  Block* b = NULL;
  Edge* edge = NULL;
  Boolean changed = TRUE;

  /* to test when a reach_out set has changed so we can stop iteraing */
  VectorSet new_reach_in =VectorSet_Create(reach_arena, register_count);

  while(changed)
    {
      changed = FALSE;
      ForAllBlocks_rPostorder(b)
        {
          VectorSet_Clear(new_reach_in);
          
          /* take union of live in over all the predecessors */
          Block_ForAllPreds(edge, b)
            {
              Block* pred = edge->pred;
              VectorSet_Union(new_reach_in,
                              pred->block_extension->reach_out,
                              new_reach_in);
            }
          
          /* ReachOut = ReachIn v Gen */
          VectorSet_Union(b->block_extension->reach_out,
                          b->block_extension->gen,
                          new_reach_in);

          if(!VectorSet_Equal(b->block_extension->reach_in,
                              new_reach_in))
            {
              VectorSet_Copy(b->block_extension->reach_in,
                             new_reach_in);
              changed = TRUE;
            }
        }
    }

}


/*
 *======================
 * initialize_sets()
 *======================
 *
 ***/
void initialize_sets()
{
  Block* block;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;

  //allocate mem for block extension structure
  alloc_extensions(reach_arena, register_count);
  alloc_reach_sets(reach_arena, block_count, register_count);

  //temp vars to make te access shorter
  VectorSet gen       = NULL;

  /* iterate through the blocks and initialize all of the sets */
  ForAllBlocks(block)
    {
      VectorSet_Clear(block->block_extension->reach_out);
      VectorSet_Clear(block->block_extension->reach_in);
      VectorSet_Clear(block->block_extension->gen);      
      
      /* put in temp var to access below just so the code
       * looks nicer */
      gen = block->block_extension->gen;

      /* gen set is any variable that is used or defined */
      Block_ForAllInsts(inst, block)
        {
          Inst_ForAllOperations(op, inst)
            {

              /* DEFS */
              Operation_ForAllUses(reg, *op)
                {
                  VectorSet_Insert(gen,*reg);
                }

              /* USES */
              Operation_ForAllDefs(reg, *op)
                {
                  VectorSet_Insert(gen,*reg);                  
                }
            } 
        }
    }
}

/*
 *======================
 * alloc_reach_sets()
 *======================
 *
 ***/
void alloc_reach_sets(Arena arena, 
                      Unsigned_Int nBlocks, 
                      Unsigned_Int nRegs)
{
  Block* block;
  _reach_out= (VectorSet* )
               Arena_GetMem(arena,(sizeof(VectorSet) * (nBlocks + 1)));
  _reach_in = (VectorSet* )
               Arena_GetMem(arena,(sizeof(VectorSet) * (nBlocks + 1)));
  
  _refs = (VectorSet* )
               Arena_GetMem(arena,(sizeof(VectorSet) * (nBlocks + 1)));
  
  ForAllBlocks(block)
    {
      /*_reach_in[id(block)]  = VectorSet_Create(arena, nRegs);
      _reach_out[id(block)] = VectorSet_Create(arena, nRegs);
      _refs[id(block)] = VectorSet_Create(arena, nRegs);
      */
      _reach_in[id(block)]  = NULL;
      _reach_out[id(block)] = NULL;
      _refs[id(block)] = NULL;
    }
}


/*
 *======================
 * alloc_extensions
 *======================
 *
 ***/
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
      block->block_extension->reach_in = VectorSet_Create(arena, nRegs);
      block->block_extension->reach_out =VectorSet_Create(arena, nRegs);
      block->block_extension->gen = VectorSet_Create(arena, nRegs);
    }
}

void dump_reaching(reaching_result res)
{
  Block* b;
  ForAllBlocks(b){
    fprintf(stderr, "BLOCK %s: \n", bname(b));
    fprintf(stderr, "REACHIN: ");
    VectorSet_Dump(res.reach_in[id(b)]);

    fprintf(stderr, "REACHOUT: ");
    VectorSet_Dump(res.reach_out[id(b)]);
    fprintf(stderr, "\n");
  } 
 
}
