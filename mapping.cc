/* contains functions and data for mappings between namespaces used in
 * the chow allocator.
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include <SSA.h>

#include "mapping.h"
#include "union_find.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
LRID* ssa_name_to_lrid = NULL;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
/*
 *========================
 * CreateLiveRangeNameMap
 *========================
 * Makes a mapping from SSA name to live range id and stores the
 * result in the variable ssa_name_to_lrid
 ***/
void Mapping::CreateLiveRangeNameMap(Arena arena)
{
  ssa_name_to_lrid = (LRID*)
        Arena_GetMemClear(arena,sizeof(LRID) * (SSA_def_count));
  Unsigned_Int idcnt = 0; 
  Unsigned_Int setid; //a setid may be 0
  LOOPVAR i;

  //initialize to known value
  for(i = 0; i < SSA_def_count; i++)
    ssa_name_to_lrid[i] = NO_LRID;
  
  for(i = 1; i < SSA_def_count; i++)
  {
    setid = Find_Set(i)->id;
    //debug("name: %d setid: %d lrid: %d", i, setid, ssa_name_to_lrid[setid]);
    if(ssa_name_to_lrid[setid] != NO_LRID)
    {
      ssa_name_to_lrid[i] = ssa_name_to_lrid[setid]; //already seen this lr
    } 
    else
    {
      //assign next available lr id
      ssa_name_to_lrid[i] = ssa_name_to_lrid[setid] = idcnt++;
    }

    debug("SSAName: %4d ==> LRID: %4d", i, ssa_name_to_lrid[i]);
  }

  assert(idcnt == (uf_set_count -1)); //no lr for SSA name 0
}

/*
 *===================
 * SSAName2LRID()
 *===================
 * Maps a variable to the initial live range id to which that variable
 * belongs. Once the splitting process starts this mapping may not be
 * valid and should not be used.
 **/
LRID Mapping::SSAName2LRID(Variable v)
{
  assert(v < SSA_def_count);
  assert((ssa_name_to_lrid[v] != NO_LRID) || v == 0);
  return ssa_name_to_lrid[v];
}


/*
 *========================================
 * ConvertLiveInNamespaceSSAToLiveRange()
 *========================================
 * Changes live range name space to use live range ids rather than the
 * ssa namespace.
 *
 * NOTE: Make sure you call this after building initial live ranges
 * because the live units need to know which SSA name they contain and
 * that information is taken from the ssa liveness info.
 ***/
void Mapping::ConvertLiveInNamespaceSSAToLiveRange() 
{

  Liveness_Info info;
  Block* blk;
  LOOPVAR j;
  LRID lrid;
  ForAllBlocks(blk)
  {
    //TODO: do we need to convert live out too?
    info = SSA_live_in[id(blk)];
    for(j = 0; j < info.size; j++)
    {
      Variable vLive = info.names[j];
      if(!(vLive < SSA_def_count))
      {
        error("invalid live in name %d", vLive);
        continue;
      }
      //debug("Converting LIVE: %d to LRID: %d",vLive,
      //       SSAName2LRID(vLive));
      lrid = SSAName2LRID(vLive);
      info.names[j] = lrid;
    }
  }
}


