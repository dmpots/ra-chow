#include <Shared.h>
#include <SSA.h>
#include <stdio.h>
#include <assert.h>
#include "debug.h"

int
main(Int argc, Char **argv)
{

  if (argc == 2)
    Block_Init(argv[1]);
  else
    Block_Init(NULL);

  /* build ssa */
  Unsigned_Int ssa_options = 0;
  ssa_options |= SSA_PRUNED;
  ssa_options |= SSA_BUILD_DEF_USE_CHAINS;
  ssa_options |= SSA_CONSERVE_LIVE_IN_INFO;
  ssa_options |= SSA_CONSERVE_LIVE_OUT_INFO;
  ssa_options |= SSA_IGNORE_TAGS;
  ssa_options |= SSA_PRINT_WARNING_MESSAGES;

  SSA_Build(ssa_options);

  Block_Put_All(stdout);
  return 0;
} /* main */


