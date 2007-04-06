/*====================================================================
 * for printing debug info
 *====================================================================
 * $Id: debug.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/debug.h $
 ********************************************************************/


#ifndef __DEBUG_H
#define __DEBUG_H
#include<vector>
#include "types.h"

#ifdef __DEBUG
#define debug(...) \
                 {fprintf(stderr,"DEBUG [%s,%d] ",__FILE__, __LINE__);\
                fprintf (stderr , __VA_ARGS__);\
                 fprintf(stderr, "\n");}
#else
#define debug(...)
#endif

#define error(...)  \
                 {fprintf(stderr,"ERROR [%s,%d] ",__FILE__, __LINE__);\
                fprintf (stderr , __VA_ARGS__);\
                 fprintf(stderr, "\n");}

#define id(b) ((b)->preorder_index)
#define bname(b) (Label_Get_String((b)->labels->label))
#define oname(op) ((opcode_specs[(op)->opcode]).opcode)

//forward defs
struct LiveUnit;
struct LiveRange;
namespace Debug {
  extern std::vector<LiveRange*> dot_dumped_lrs;
  extern LRID dot_dump_lr;

  void LiveRange_DumpAll(LRVec*);
  void LiveRange_DDumpAll(LRVec* lrs);
  void LiveRange_Dump(LiveRange* lr);
  void LiveRange_DDump(LiveRange* lr);
  void LiveUnit_Dump(LiveUnit* );
  void LiveUnit_DDump(LiveUnit* unit);
  void DumpInitialLiveRanges();

  void DotDumpLR(LiveRange* lr, const char* tag);
  void DotDumpFinalLRs();
  const char* StringOfInst(Inst* inst);
}

#endif /* __DEBUG_H */
