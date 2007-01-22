#ifndef __GUARD_ASSIGN_H
#define __GUARD_ASSIGN_H
#include "types.h"

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
  //structure which we may remove things and add things to on a
  //regular basis and we want that to be efficent
  std::list< std::pair<LRID,Register> >* evicted;
  std::vector<ReservedReg*>* all;
  ReservedRegMap* regMap;
  std::vector<ReservedReg*>* reserved; 
} RegisterContents;


/* exported functions */
void reset_free_tmp_regs(Inst* last_inst);
void ensure_reg_assignment(Register* reg, 
                           LRID lrid, 
                           Block* blk,
                           Inst*  origInst, 
                           Inst** updatedInst, 
                           Operation* op,
                           RegPurpose purpose,
                           const RegisterList& instUses,
                           const RegisterList& instDefs);

typedef const std::vector<ReservedReg*>::iterator ReservedIterator;
typedef std::map<LRID, ReservedReg*>::iterator RegMapIterator;
typedef std::list<std::pair<LRID, Register> > EvictedList;
typedef std::vector<ReservedReg*> ReservedList;

#endif
