/*====================================================================
 * dot_dump.c
 *====================================================================
 * main wrapper for running dot dump which dumps out the the call
 * graph in a file for display by dot
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include "dot_dump.h"

void name(Block*, char*);

int main(Int argc, Char **argv)
{
  if (argc == 2)
    Block_Init(argv[1]);
  else
    Block_Init(NULL);
 
  Dot::Dump();
  return 0;
} /* main */


