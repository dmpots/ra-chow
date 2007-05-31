/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */
#ifndef __GUARD_COLOR_H
#define __GUARD_COLOR_H

#include <Shared.h>
#include "types.h"
#include "debug.h"
#include "rc.h"

/* forward def */
struct LiveRange;

namespace Coloring {
  /* constants */
  extern const Color NO_COLOR;

  /* functions */
  void Init(Arena, unsigned int num_live_ranges);
  VectorSet UsedColors(RegisterClass::RC rc, Block* b);
  void SetColor(Block* blk, LRID lrid, Color color);
  Color GetColor(Block* blk, LRID lrid);
  LRID GetLRID(Block* blk, RegisterClass::RC rc, Color color);

  bool IsColorAvailable(const LiveRange* lr, Block* blk);
  bool IsColorAvailable(const LiveRange* lr, VectorSet used_colors);
  int  NumColorsAvailable(const LiveRange* lr);
  int  NumColorsAvailable(const LiveRange* lr, VectorSet used_colors);
  Color SelectColor(const LiveRange* lr);
}


#endif

