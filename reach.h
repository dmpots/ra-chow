/*====================================================================
 * 
 *====================================================================
 * $Id: reach.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/reach.h $
 ********************************************************************/

#ifndef __GUARD_REACH_H
#define  __GUARD_REACH_H

#include <Shared.h>

typedef struct reaching_result {
  VectorSet* reach_in;
  VectorSet* reach_out;
  VectorSet* refs;
} reaching_result;


reaching_result compute_reaching(Arena);

#endif

