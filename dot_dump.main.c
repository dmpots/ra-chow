/*====================================================================
 * 
 *====================================================================
 * $Id: dot_dump.c 155 2006-07-24 22:15:51Z dmp $
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
 
  Dot::DotDump();
  return 0;
} /* main */


