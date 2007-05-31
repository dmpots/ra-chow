/* color.cc
 * 
 * contains functions and data used to keep track of coloring info for
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

  //mapping from block * register class * color --> lrid
  //this is maintained for the assingnment module which needs this
  //information to properly implement eviction of registers for FRAME
  //and JSR instructions
  std::map<std::pair<Block*, RegisterClass::RC>, std::map<Color,LRID> >
    inverse_color_map;

  inline bool HasSpace(VectorSet used_colors, Color color, int width)
  {
    bool free = true;
    for(int i = 0; i < width; i++)
    {
      free = free && (!VectorSet_Member(used_colors, color+i));
    }

    return free;
  };
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
}

VectorSet Coloring::UsedColors(RegisterClass::RC rc, Block* blk)
{
  return mRcBlkId_VsUsedColor[rc][id(blk)];
}

void Coloring::SetColor(Block* blk, LRID lrid, Color color)
{
  LiveRange* lr = Chow::live_ranges[lrid];
  inverse_color_map[std::make_pair(blk,lr->rc)][color] = lrid;
}

Color Coloring::GetColor(Block* blk, LRID lrid)
{
  return (*Chow::live_ranges[lrid]->blockmap)[(id(blk))]->color;
}

LRID Coloring::GetLRID(Block* blk, RegisterClass::RC rc, Color color)
{
  LRID lrid = inverse_color_map[std::make_pair(blk,rc)][color];
  if(lrid == 0) lrid = NO_LRID;

  return lrid;
}

bool Coloring::IsColorAvailable(const LiveRange* lr, Block* blk)
{
  return IsColorAvailable(lr, UsedColors(lr->rc, blk));
}

bool Coloring::IsColorAvailable(const LiveRange* lr, VectorSet used_colors)
{
  return (NumColorsAvailable(lr, used_colors) > 0);
}

int Coloring::NumColorsAvailable(const LiveRange* lr)
{
  return NumColorsAvailable(lr, lr->forbidden);
}

int Coloring::NumColorsAvailable(const LiveRange* lr, VectorSet used_colors)
{
  int num_avail = 0;
  /* TODO: this could be quite slow. keep an on on this section to see
   * if we maybe need to speed it up */
  int step = RegisterClass::RegWidth(lr->type);
  unsigned int ub = RegisterClass::NumMachineReg(lr->rc) - step + 1;
  for(Color c = 0; c < ub; c+=step)
  {
    if(HasSpace(used_colors, c, step)) num_avail++;
  }

  return num_avail;
}

Color Coloring::SelectColor(const LiveRange* lr)
{
  //TODO: pick a better color, i.e. one that is used by neighbors
  //neighbors. for now just pick the first available color
  int step = RegisterClass::RegWidth(lr->type);
  unsigned int ub = RegisterClass::NumMachineReg(lr->rc) - step + 1;
  for(Color c = 0; c < ub; c+=step)
  {
    if(HasSpace(lr->forbidden, c, step)) return c;
  }

  assert(false /*should always find a color */);
  return -1U; //for sun compiler
}

/*------------------INTERNAL MODULE FUNCTIONS--------------------*/


