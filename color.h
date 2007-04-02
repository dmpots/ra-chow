/* contains functions and data used to keep track of coloring info for
 * live ranges and blocks. 
 */
#include <Shared.h>
#include "types.h"
#include "debug.h"


namespace Coloring {
  void Init(Arena, unsigned int num_reg_classes);
  VectorSet UsedColors(RegisterClass rc, Block* b);
}



