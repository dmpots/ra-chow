/*====================================================================
 * 
 *====================================================================
 * $Id: dot_dump.c 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/dot_dump.c $
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"

#define puts(...) \
                 {fprintf(stdout,__VA_ARGS__);\
                 fprintf(stdout, "\n");}

void name(Block*, char*);

int main(Int argc, Char **argv)
{
  Block* b;
  Edge* e;
  char buf[1024] = {'\0'};
  char buf2[1024] = {'\0'};

  if (argc == 2)
    Block_Init(argv[1]);
  else
    Block_Init(NULL);
 
 
  puts("digraph {");
 
  /* nodes */ 
  ForAllBlocks(b){
   name(b, buf);
   puts("%s;", buf);  
  }
  /* edges */
  ForAllBlocks(b){
    Block_ForAllSuccs(e, b)
    {
      name(b, buf);
      name(e->succ, buf2);
      puts("%s -> %s;",buf, buf2);
    } 
  }
  puts("}"); //graph

  return 0;
} /* main */

#define isdot(c) ((c) == '.')
void name(Block* b, char* buf)
{
  unsigned int i,k;
  char c;
  for(i = 0,k = 0; i < strlen(bname(b)); i++)
  {
    c = ((bname(b)))[i];
    if(!isdot(c)) buf[k++] = c;
  }
  buf[i] = '\0';
}
 

