/* debug.cc
 *
 * contains functions and variables used only for debugging the
 * allocator 
 */
#include "debug.h"
using std::vector;

//keep track of all the lrids that are dumped throughout the program.
//there can be multiple lrids if the original lrid indicated by
//dot_dump_lr is split during allocation
vector<LRID> Debug::dot_dumped_lrids;

//used to control whether we dot dump a lr throughout the life of the
//allocator. a non-zero value indicates the live range to watch
LRID Debug::dot_dump_lr = 0;

