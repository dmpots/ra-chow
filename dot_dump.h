#ifndef __GUARD_DOT_DUMP_H
#define __GUARD_DOT_DUMP_H

#include <Shared.h>


struct LiveRange;
namespace Dot {
  void Dump(void);
  void Dump(LiveRange* lr, FILE* outfile);
  void Dump(LiveRange* lr, const char* fname);
  void Dump(LiveRange* lr);
}

#endif
