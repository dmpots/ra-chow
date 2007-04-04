/*====================================================================
 * 
 *====================================================================
 * $Id: cleave.main.c 157 2006-07-26 19:19:34Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/cleave.main.c $
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "debug.h"
#include "cleave.h"



int
main(Int argc, Char **argv)
{
  Arena arnCleave = Arena_Create();

  if (argc == 2)
    Block_Init(argv[1]);
  else
    Block_Init(NULL);
  
  InitCleaver(arnCleave, 5);
  CleaveBlocks();

  /* dump out the results */
  //Block* b;
  //ForAllBlocks(b){
  //  Block_Dump(b, NULL, TRUE);
  // }
  Block_Put_All(stdout);

  return 0;
} /* main */


