/*====================================================================
 * General types used throughout the chow allocator 
 *====================================================================
 * $Id: types.h 155 2006-07-24 22:15:51Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/types.h $
 ********************************************************************/

#ifndef __GUARD_TYPES_H
#define __GUARD_TYPES_H
#include <vector>
#include <set>

typedef unsigned int Register;
typedef unsigned int Color;
typedef unsigned int Variable;
typedef unsigned int MemoryLocation;
typedef unsigned int LRID;
typedef unsigned int LOOPVAR;
typedef unsigned int RegisterClass;
typedef float Priority;

//simple switch to say where to insert an instruction
enum InstInsertLocation {BEFORE_INST, AFTER_INST};
typedef enum {FOR_USE, FOR_DEF} RegPurpose;

//list of registers
typedef std::vector<Register> RegisterList;

/* convienient typedefs for live range containers */
struct LiveRange;
struct LRcmp;
typedef std::vector<LiveRange*> LRVec;
typedef std::set<LiveRange*, LRcmp> LRSet;

const LRID NO_LRID = (LRID) -1; //not a valid LRID

#endif
