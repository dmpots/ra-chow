/*====================================================================
 * 
 *====================================================================
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <utility>
#include <list>
#include "debug.h"
#include "util.h"
#include "cfg_tools.h"



int
main(Int argc, Char **argv)
{
  Arena arena = Arena_Create();

  if (argc == 2)
    Block_Init(argv[1]);
  else
    Block_Init(NULL);
  
  InitCFGTools(arena);

  std::list<std::pair<Block*,Block*> > worklist;
  Block* b;
  ForAllBlocks(b){
    Edge* e;
    Block_ForAllSuccs(e, b)
    {
      if(b != end_block && b != start_block && e->succ != end_block)
      {
        worklist.push_back(std::make_pair(e->pred, e->succ));
      }
    }
  }
  for(std::list<std::pair<Block*,Block*> >::iterator 
      it = worklist.begin();
      it != worklist.end();
      it++)
  {
    SplitEdge((*it).first, (*it).second);
  }
  Block_Order();

  /* dump out the results */
  //ForAllBlocks(b){
  //  Block_Dump(b, NULL, TRUE);
  //}
  Block_Put_All(stdout);

  return 0;
} /* main */


