/*====================================================================
 * 
 *====================================================================
 * $Id: chow.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/chow.h $
 ********************************************************************/

#ifndef __GUARD_CHOW_H
#define __GUARD_CHOW_H

#include <Shared.h>
#include "types.h"

struct LiveRange;
namespace Chow {
  extern std::vector<LiveRange*> live_ranges;
  void Run();
}


#endif

