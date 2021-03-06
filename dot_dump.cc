/*====================================================================
 * 
 *====================================================================
 ********************************************************************/

#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dot_dump.h"
#include "live_range.h"
#include "live_unit.h"

/* local functions */
namespace {
  void name(Block*, char*);
  void name(Block*, LiveRange*, char*);
  void color(Block*, LiveRange*, char*);
}

#define puts(...) \
                 {fprintf(stdout,__VA_ARGS__);\
                 fprintf(stdout, "\n");}
#define putso(...) \
                 {fprintf(outfile,__VA_ARGS__);\
                 fprintf(outfile, "\n");}
#define isdot(c) ((c) == '.')

namespace Dot {
void Dump(LiveRange* lr){Dump(lr, stdout);}
void Dump(LiveRange* lr, const char* fname )
{
  FILE* fp = fopen(fname, "w");
  if(fp == NULL) {error("unable to open file: %s", fname); abort();}
  Dump(lr, fp);
  fclose(fp);
}
void Dump(LiveRange* lr, FILE* outfile)
{
  Block* b;
  Edge* e;
  char buf[512] = {'\0'};
  char buf2[512] = {'\0'};
  char buf3[1024] = {'\0'};

  putso("digraph {");
  /* nodes */ 
  ForAllBlocks(b){
   name(b, lr, buf);
   color(b, lr, buf2);
   sprintf(buf3, "\"%s\" %s", buf, buf2);
   putso("%s;", buf3);  
  }
  /* edges */
  ForAllBlocks(b){
    Block_ForAllSuccs(e, b)
    {
      name(b, lr, buf);
      name(e->succ, lr, buf2);
      putso("\"%s\" -> \"%s\";",buf, buf2);
    } 
  }
  putso("}"); //graph
}


void Dump()
{
  Block* b;
  Edge* e;
  char buf[1024] = {'\0'};
  char buf2[1024] = {'\0'};
 
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
} /* main */
}/* end namespace Dot */


namespace {
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
 

void name(Block* b, LiveRange* lr, char* buf)
{
  char bufT[100] = {'\0'};
  char bufT2[100] = {'\0'};
  char bufT3[100] = {'\0'};
  char bufTD[100] = {'\0'};

  name(b, bufT);
  if(lr->ContainsBlock(b))
  {
    LiveUnit* lu = lr->LiveUnitForBlock(b);
    if(lu->start_with_def && lu->defs > 0)
    {
      sprintf(bufTD, "\\n(def %d_%d)", lr->id, lr->orig_lrid);
    }
    if(lu->uses > 0)
    {
      sprintf(bufT2, "\\n(use %d_%d)", lr->id, lr->orig_lrid);
    }
    if(lu->defs > 0 && !(lu->start_with_def))
    {
      sprintf(bufT3, "\\n(def %d_%d)", lr->id, lr->orig_lrid);
    }
  }

  sprintf(buf,"%s%s%s%s", bufT, bufTD, bufT2, bufT3);
}


void color(Block* b, LiveRange* lr, char* buf)
{
  if(lr->ContainsBlock(b))
  {
    LiveUnit* lu = lr->LiveUnitForBlock(b);
    if(lu->uses > 0 && lu->defs > 0)
      sprintf(buf, "[style=filled, fillcolor=yellow]");
    else if(lu->uses > 0)
      sprintf(buf, "[style=filled, fillcolor=green]");
    else if(lu->defs > 0)
      sprintf(buf, "[style=filled, fillcolor=red]");
    else
      sprintf(buf, "[style=filled, fillcolor=blue]");
  }
  else
    buf[0] = '\0';
}

}

