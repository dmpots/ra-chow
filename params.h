/* params.h
 *
 * contains parameters used to control allocation. this includes
 * machine paramerters as well as flags for turning on different
 * algorithms in the allocator.
 */
#ifndef __GUARD_PARAMS_H
#define __GUARD_PARAMS_H

/* forward def */
namespace Chow{
namespace Heuristics {
  class WhenToSplitStrategy;
}
}

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
    extern unsigned int num_reserved_registers[];
    extern float loop_depth_weight;
    extern bool enhanced_code_motion;
    extern bool move_loads_and_stores;
    extern bool rematerialize;
    extern bool trim_useless_blocks;
    extern Chow::Heuristics::WhenToSplitStrategy& when_to_split_strategy;
  }
  namespace Program {
    extern bool force_minimum_register_count;
    extern bool dump_params_only;
  }
}

#endif
