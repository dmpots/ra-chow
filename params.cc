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
unsigned int   num_reserved_registers[] = {2,4};
float loop_depth_weight = 10.0;
bool  enhanced_code_motion = false;
bool  move_loads_and_stores = false;
bool  rematerialize = false;
bool  trim_useless_blocks = false;

/* default heuristics */
WhenToSplitStrategy& when_to_split_strategy = 
  Chow::Heuristics::default_when_to_split;
IncludeInSplitStrategy& include_in_split_strategy = 
  Chow::Heuristics::default_include_in_split;
}

/* program parameters */
namespace Program {
bool force_minimum_register_count = false;
bool dump_params_only = false;
}

}
