#ifndef __GUARD_ASSIGN_H
#define __GUARD_ASSIGN_H

#include <Shared.h>
#include <list>
#include <map>
#include <vector>

#include "types.h"
#include "debug.h"

typedef struct reserved_reg
{
  Register machineReg;
  Inst* forInst;
  RegPurpose forPurpose;
  LRID forLRID;
  Boolean free;
} ReservedReg;

typedef std::map<LRID,ReservedReg*> ReservedRegMap;
typedef struct register_contents
{
  //why is evicted a list you ask? it is because it is the only
  //structure which we may remove things and add things in the middle
  //of the collection on a regular basis and we want that to be efficent
  std::list< std::pair<LRID,Register> >* evicted;
  std::vector<ReservedReg*>* all;
  ReservedRegMap* regMap;
  std::vector<ReservedReg*>* reserved; 
} RegisterContents;


typedef const std::vector<ReservedReg*>::iterator ReservedIterator;
typedef std::map<LRID, ReservedReg*>::iterator RegMapIterator;
typedef std::list<std::pair<LRID, Register> > EvictedList;
typedef std::vector<ReservedReg*> ReservedList;


namespace Assign {
  /* constants */
  extern const Register REG_UNALLOCATED;

  /* exported functions */
  void Init(Arena);
  Register GetMachineRegAssignment(Block* b, LRID lrid);
  void EnsureReg(Register* reg, 
                 LRID lrid, 
                 Block* blk,
                 Inst*  origInst, 
                 Inst** updatedInst, 
                 Operation* op,
                 RegPurpose purpose,
                 const RegisterList& instUses,
                 const RegisterList& instDefs);
  void ResetFreeTmpRegs(Inst* last_inst);
}
#endif
