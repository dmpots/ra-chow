/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */
#ifndef __GUARD_COLOR_H
#define __GUARD_COLOR_H

#include <Shared.h>
#include "types.h"
#include "debug.h"


namespace Coloring {
  /* constants */
  extern const Color NO_COLOR;

  /* functions */
  void Init(Arena, unsigned int num_reg_classes, unsigned int num_live_ranges);
  VectorSet UsedColors(RegisterClass rc, Block* b);
  void SetColor(Block* blk, LRID lrid, Color color);
  Color GetColor(Block* blk, LRID lrid);
}


#endif

