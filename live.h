/*====================================================================
 * 
 *====================================================================
 * $Id: live.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/live.h $
 ********************************************************************/

#ifndef __GUARD_LIVE_H
#define  __GUARD_LIVE_H

#include <Shared.h>

typedef struct liveness_result {
  VectorSet* live_in;
  VectorSet* live_out;
} liveness_result;


liveness_result compute_liveness(Arena, Unsigned_Int);

void alloc_live_sets(Arena arena,
                     Unsigned_Int nBlocks, 
                     Unsigned_Int nRegs);

#endif
