/* params.cc
 *
 * contains parameters used to control allocation. this includes
 * machine paramerters as well as flags for turning on different
 * algorithms in the allocator.
 *
 * the default values for the paramerter are set in this file
 */

#include "params.h"
#include "heuristics.h"

namespace Params {
/* machine parameters */
namespace Machine {
int   num_registers = 32;
bool  enable_register_classes = true;
int   num_register_classes = 2;
float load_save_weight = 1.0;
float store_save_weight = 1.0;
float move_cost_weight = 1.0;
bool  double_takes_two_regs = true;
}

/* algorithm parameters */
namespace Algorithm {
int   bb_max_insts = 0;
int   num_reserved_registers[] = {2,4};
float loop_depth_weight = 10.0;
bool  enhanced_code_motion = false;
bool  move_loads_and_stores = true;
bool  rematerialize = false;
bool  trim_useless_blocks = false;
bool  allocate_locals = false;
bool  spill_instead_of_split = false;
bool  optimistic = false;
bool  allocate_all_unconstrained = false;
bool  enhanced_register_promotion = true;
bool  prefer_clean_locals = false;

/* default heuristics */
ColorChoice color_choice = CHOOSE_FIRST_COLOR;
IncludeInSplit include_in_split = WHEN_NOT_FULL;
WhenToSplit when_to_split = NO_COLOR_AVAILABLE;
HowToSplit how_to_split = CHOW_SPLIT;
}

/* program parameters */
namespace Program {
bool force_minimum_register_count = false;
bool dump_params_only = false;
}

}
