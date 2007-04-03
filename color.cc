/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include "color.h"
#include "rc.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
  VectorSet** mRcBlkId_VsUsedColor;
  Unsigned_Int** mBlkIdSSAName_Color;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
const Color Coloring::NO_COLOR = -1u;
void Coloring::Init(Arena arena, 
                    unsigned int num_reg_classes,
                    unsigned int num_live_ranges)
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


  //allocate a mapping of block x SSA_name -> register
  mBlkIdSSAName_Color = (Unsigned_Int**)
    Arena_GetMemClear(arena, 
                      sizeof(Unsigned_Int*) * (1+block_count));
  LOOPVAR i;
  for(i = 0; i < block_count+1; i++)
  {
    mBlkIdSSAName_Color[i] = (Unsigned_Int*)
      Arena_GetMemClear(arena, 
                        sizeof(Unsigned_Int) * num_live_ranges );
      LOOPVAR j;
      for(j = 0; j < num_live_ranges; j++)
        mBlkIdSSAName_Color[i][j] = NO_COLOR;
  }
}

VectorSet Coloring::UsedColors(RegisterClass rc, Block* blk)
{
  return mRcBlkId_VsUsedColor[rc][id(blk)];
}

void Coloring::SetColor(Block* blk, LRID lrid, Color color)
{
  mBlkIdSSAName_Color[id(blk)][lrid] = color;
}

Color Coloring::GetColor(Block* blk, LRID lrid)
{
  return mBlkIdSSAName_Color[id(blk)][lrid];
}

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/


