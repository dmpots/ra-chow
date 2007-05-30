/* params.cc
 *
 * contains parameters used to control allocation. this includes
 * machine paramerters as well as flags for turning on different
 * algorithms in the allocator.
 *
 * the default values for the paramerter are set in this file
 */

#include "params.h"

/* machine parameters */
int   Params::Machine::num_registers = 32;
bool  Params::Machine::enable_register_classes = true;
float Params::Machine::load_save_weight = 1.0;
float Params::Machine::store_save_weight = 1.0;
float Params::Machine::move_cost_weight = 1.0;
bool  Params::Machine::double_takes_two_regs = true;

/* algorithm parameters */
int   Params::Algorithm::bb_max_insts = 0;
unsigned int   Params::Algorithm::num_reserved_registers[] = {2,4};
float Params::Algorithm::loop_depth_weight = 10.0;
bool  Params::Algorithm::enhanced_code_motion = false;
bool  Params::Algorithm::move_loads_and_stores = false;
bool  Params::Algorithm::rematerialize = false;
bool  Params::Algorithm::trim_useless_blocks = false;

/* program parameters */
bool Params::Program::force_minimum_register_count = false;
bool Params::Program::dump_params_only = false;

