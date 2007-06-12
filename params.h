/* params.h
 *
 * contains parameters used to control allocation. this includes
 * machine paramerters as well as flags for turning on different
 * algorithms in the allocator.
 */
#ifndef __GUARD_PARAMS_H
#define __GUARD_PARAMS_H
#include "heuristics.h"

namespace Params {
  namespace Machine {
    extern int num_registers;
    extern bool enable_register_classes;
    extern int num_register_classes;
    extern float load_save_weight;
    extern float store_save_weight;
    extern float move_cost_weight;
    extern bool  double_takes_two_regs;
  }
  namespace Algorithm {
    extern int bb_max_insts;
    extern int num_reserved_registers[];
    extern float loop_depth_weight;
    extern bool enhanced_code_motion;
    extern bool move_loads_and_stores;
    extern bool rematerialize;
    extern bool trim_useless_blocks;
    extern bool  allocate_locals;

    using namespace Chow::Heuristics;
    extern WhenToSplit when_to_split;
    extern ColorChoice color_choice;
    extern IncludeInSplit include_in_split;
    extern HowToSplit how_to_split;
  }
  namespace Program {
    extern bool force_minimum_register_count;
    extern bool dump_params_only;
  }
}

#endif
