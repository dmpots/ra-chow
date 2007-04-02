/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include "color.h"
#include "rc.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
  VectorSet** mRcBlkId_VsUsedColor;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
void Coloring::Init(Arena arena, unsigned int num_reg_classes)
{
  //allocate a mapping from blocks to a set of used colors
  mRcBlkId_VsUsedColor = (VectorSet**)
       Arena_GetMemClear(arena,sizeof(VectorSet*)*(num_reg_classes));

  for(RegisterClass rc = 0; rc < num_reg_classes; rc++)
  {
    mRcBlkId_VsUsedColor[rc] = (VectorSet*)
        Arena_GetMemClear(arena,sizeof(VectorSet)*(block_count+1));
    Block* b;
    ForAllBlocks(b)
    {
      Unsigned_Int bid = id(b);
      Unsigned_Int cReg = RegisterClass_NumMachineReg(rc);
      mRcBlkId_VsUsedColor[rc][bid] = VectorSet_Create(arena, cReg);
      VectorSet_Clear(mRcBlkId_VsUsedColor[rc][bid]);
    } 
  }
}

VectorSet Coloring::UsedColors(RegisterClass rc, Block* blk)
{
  return mRcBlkId_VsUsedColor[rc][id(blk)];
}

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/


