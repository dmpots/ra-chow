/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include <utility>
#include <map>

#include "color.h"
#include "chow.h"
#include "rc.h"
#include "live_range.h"
#include "assign.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
namespace {
  VectorSet** mRcBlkId_VsUsedColor;
  Unsigned_Int** mBlkIdSSAName_Color;

  //mapping from block * register class * color --> lrid
  //this is maintained for the assingnment module which needs this
  //information to properly implement eviction of registers for FRAME
  //and JSR instructions
  std::map<std::pair<Block*, RegisterClass::RC>, std::map<Color,LRID> >
    inverse_color_map;
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
const Color Coloring::NO_COLOR = -1u;
void Coloring::Init(Arena arena, unsigned int num_live_ranges)
{
  //allocate a mapping from blocks to a set of used colors
  int num_reg_classes = RegisterClass::all_classes.size();
  mRcBlkId_VsUsedColor = (VectorSet**)
       Arena_GetMemClear(arena,sizeof(VectorSet*)*(num_reg_classes));

  for(unsigned int i = 0; i < RegisterClass::all_classes.size(); i++)
  {
    RegisterClass::RC rc = RegisterClass::all_classes[i];
    mRcBlkId_VsUsedColor[rc] = (VectorSet*)
        Arena_GetMemClear(arena,sizeof(VectorSet)*(block_count+1));
    Block* b;
    ForAllBlocks(b)
    {
      Unsigned_Int bid = id(b);
      Unsigned_Int cReg = RegisterClass::NumMachineReg(rc);
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

VectorSet Coloring::UsedColors(RegisterClass::RC rc, Block* blk)
{
  return mRcBlkId_VsUsedColor[rc][id(blk)];
}

void Coloring::SetColor(Block* blk, LRID lrid, Color color)
{
  LiveRange* lr = Chow::live_ranges[lrid];
  mBlkIdSSAName_Color[id(blk)][lr->orig_lrid] = color;
  inverse_color_map[std::make_pair(blk,lr->rc)][color] = lrid;
}

Color Coloring::GetColor(Block* blk, LRID lrid)
{
  LRID orig_lrid = Chow::live_ranges[lrid]->orig_lrid;
  return mBlkIdSSAName_Color[id(blk)][orig_lrid];
}

LRID Coloring::GetLRID(Block* blk, RegisterClass::RC rc, Color color)
{
  LRID lrid = inverse_color_map[std::make_pair(blk,rc)][color];
  if(lrid == 0) lrid = NO_LRID;

  return lrid;
}

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/


