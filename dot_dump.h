#ifndef __GUARD_DOT_DUMP_H
#define __GUARD_DOT_DUMP_H

#include <Shared.h>
#include "live_range.h"


void name(Block*, char*);
void name(Block*, LiveRange*, char*);
void color(Block*, LiveRange*, char*);
void dot_dump();
void dot_dump_lr(LiveRange* lr, FILE* outfile);
void dot_dump_lr(LiveRange* lr);

#endif
