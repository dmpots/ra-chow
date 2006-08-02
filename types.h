/*====================================================================
 * General types used throughout the chow allocator 
 *====================================================================
 * $Id: types.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/types.h $
 ********************************************************************/

#ifndef __GUARD_TYPES_H
#define __GUARD_TYPES_H

typedef Unsigned_Int Register;
typedef Unsigned_Int Color;
typedef Unsigned_Int Variable;
typedef Unsigned_Int MemoryLocation;
typedef Unsigned_Int LRID;
typedef Unsigned_Int LOOPVAR;

//simple switch to say where to insert an instruction
enum InstInsertLocation {BEFORE_INST, AFTER_INST};

#endif
