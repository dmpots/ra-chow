

#include <list>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <utility>


#include "debug.h"
#include "chow.h"
#include "rc.h"
#include "assign.h"
#include "types.h"

static Register bullshitReg = 1000;

/* local functions */
static std::pair<Register,bool>
get_free_tmp_reg(LRID lrid, 
                 Block* blk,
                 Inst* inst, 
                 Operation* op,
                 RegPurpose purpose,
                 const RegisterList& instUses,
                 const RegisterList& instDefs);

static ReservedReg* find_usable_reg(
                            const std::vector<ReservedReg*>* reserved,
                            Inst* inst,
                            RegPurpose purpose,
                            const RegisterList& instUses);
static Register mark_register_used(ReservedReg* tmpReg, 
                        Inst* inst, 
                        RegPurpose purpose,
                        LRID lrid,
                        ReservedRegMap& regMap);

void remove_unusable_reg(std::list<ReservedReg*>& potentials, 
                         Register ssaName, Block* blk );

Boolean has_been_evicted(LRID lrid, Register reg);
/* --- end functions --- */

/* debug routines */
void dump_map_contents(ReservedRegMap& regMap)
{
  debug("-- map contents --");
  ReservedRegMap::iterator rmIt;
  for(rmIt = regMap.begin(); rmIt != regMap.end(); rmIt++)
    debug("  %d --> %d", (*rmIt).first, ((*rmIt).second)->machineReg);
  debug("-- end map contents --");
}

void dump_reglist_contents(RegisterList& regList)
{
  debug("-- reglist contents --");
  RegisterList::iterator rmIt;
  for(rmIt = regList.begin(); rmIt != regList.end(); rmIt++)
    debug("  %d", (*rmIt));
  debug("-- end reglist contents --");
}

/*
 *===================
 * get_free_tmp_reg()
 *===================
 * gets the next available free tmp register from the pool of
 * available free temp registers
 **/

Boolean isFree(const ReservedReg* reserved){return reserved->free;}
class ReservedReg_Usable : public std::unary_function<ReservedReg*, bool>
{
  const Inst* inst;
  RegPurpose purpose;
  const RegisterList& instUses;

  public:
  ReservedReg_Usable(const Inst* inst_, RegPurpose purpose_,
                     const RegisterList& instUses_) :
    inst(inst_), purpose(purpose_), instUses(instUses_) {}
  /* we can use a previously used reserved reg if it is for a
   * different instruction or it was used in the instruction to hold
   * a USE and we now need it for a DEF. we also check the list of
   * uses in the inst to make sure we don't use this temp register if
   * the lrid it stores is needed for this instruction (even if it was
   * put in for the previous instruction for example) */
  bool operator() (const ReservedReg* reserved) const 
  {
    if(reserved->forInst != inst)
    {
      //make sure this reg is not used in the instruction under
      //question since it has to occupy a register in that case
      //and is not available to be evicted
      if(purpose == FOR_USE)
      {
        if(find(instUses.begin(), instUses.end(), reserved->forLRID)
           != instUses.end())
        {
          return false;
        }
      }
      return true;
    }
    else //needed in the same inst
    {
      return reserved->forPurpose != purpose;
    }
    /*if(reserved->forInst != inst || reserved->forPurpose != purpose)
    {
      if(purpose == FOR_USE)
      {
        return 
          (find(instUses.begin(), instUses.end(), reserved->forLRID)
          ==
          instUses.end());
      }
      return true;
    }
    return false;
    */
  }
};

class ReservedReg_MRegEq : public std::unary_function<ReservedReg*, bool>
{
  Register mReg; 

  public:
  ReservedReg_MRegEq(Register mReg_) : mReg(mReg_) {};
  bool operator() (const ReservedReg* reserved) const 
  {
    return reserved->machineReg == mReg;
  }
};


class ReservedReg_InstEq : public std::unary_function<ReservedReg*, bool>
{
  Inst* inst; 

  public:
  ReservedReg_InstEq(Inst* inst_) : inst(inst_) {};
  bool operator() (const ReservedReg* reserved) const 
  {
    return reserved->forInst == inst;
  }
};


void ensure_reg_assignment(Register* reg, 
                           LRID lrid, 
                           Block* blk,
                           Inst*  origInst,
                           Inst** updatedInst, 
                           Operation* op,
                           RegPurpose purpose,
                           const RegisterList& instUses, 
                           const RegisterList& instDefs)
{
  //this live range is spilled. find a temporary register
  if(*reg == REG_UNALLOCATED)
  {
    std::pair<Register,bool> regXneedMem;
    Register tmpReg;
    bool needMemAccess;

    //find a temporary register for this value
    regXneedMem = get_free_tmp_reg(lrid, blk, origInst, op, purpose,
                                   instUses, instDefs);

    //get return values
    tmpReg = regXneedMem.first;
    needMemAccess = regXneedMem.second;
    debug("got a free temporary register: %d, needsMemAccess: %c", 
          tmpReg, needMemAccess ? 't' : 'f');

    //generate a load or store if the value was not already in the
    //register
    if(needMemAccess)
    {
      if(purpose == FOR_USE)
      {
        Insert_Load(lrid, *updatedInst, tmpReg, REG_FP);
      }
      else //FOR_DEF
      {
        *updatedInst = 
          Insert_Store(lrid, *updatedInst, tmpReg, REG_FP, AFTER_INST);
      }
    }
    *reg = tmpReg;
  }
  //this live range is allocated. make sure we have not previously
  //evicted it for another purpose and reload it if we have
  else
  {
    //check to see that it was not previously evicted and insert a
    //load if needed
    if(has_been_evicted(lrid, *reg))
    {
      Insert_Load(lrid, *updatedInst, *reg, REG_FP);
    }
  }
}

Boolean has_been_evicted(LRID lrid, Register reg)
{

  //search evicted list for this machine register
  RegisterContents* regContents = 
    RegisterClass_GetRegisterContentsForLRID(lrid);
  EvictedList::iterator it = find(regContents->evicted->begin(),
                                  regContents->evicted->end(),
                                  std::make_pair(lrid,reg));
  if(it != regContents->evicted->end())
  {
    //remove from evicted list
    regContents->evicted->erase(it);

    //find the lrid that is in our register so we can remove 
    //it from the regMap
    ReservedList::iterator rrIt;
    rrIt = find_if(regContents->all->begin(),
                    regContents->all->end(),
                    ReservedReg_MRegEq(reg));
    assert(rrIt != regContents->all->end());
    RegMapIterator rmIt = regContents->regMap->find((*rrIt)->forLRID);
    // ---- DEBUG ----->
    //TODO: is our assertion assumption correct?
    if(rmIt == regContents->regMap->end())
    {
      debug("expected to find lrid: %d in reg: %d, but didn't",
            (*rrIt)->forLRID, reg);
      dump_map_contents(*regContents->regMap);
    }
    // ---- DEBUG -----<
    assert(rmIt != regContents->regMap->end());
    regContents->regMap->erase(rmIt);
    return TRUE;
  }

  //it was not previously evicted
  return FALSE;
}

std::pair<Register,bool>
get_free_tmp_reg(LRID lrid, Block* blk, Inst* inst, Operation* op,
                 RegPurpose purpose, 
                 const RegisterList& instUses, 
                 const RegisterList& instDefs)
{
  debug("looking for temporary reg for %s of lrid: %d",
    (purpose == FOR_USE ? "FOR_USE" : "FOR_DEF"), lrid);

  std::pair<Register,bool> regXneedMem;
  regXneedMem.second = true; //default is needs memory load/store

  //get the register contents struct for this lrid register class
  RegisterContents* regContents =
    RegisterClass_GetRegisterContentsForLRID(lrid);

  //1) check to see if this lrid is already stored in one of our
  //temporary registers.
  ReservedRegMap* regMap = (regContents->regMap);
  RegMapIterator it = regMap->find(lrid);
  if(it != regMap->end()) //found it
  {
    debug("found the live range already in a temporary register");
    ReservedReg* rReg  = ((*it).second);
    rReg->forInst = inst;
    rReg->forPurpose = purpose;
    regXneedMem.first  = rReg->machineReg;
    if(purpose == FOR_USE) {regXneedMem.second = false;}
    return regXneedMem;
  }

  //2) check to see if we have any more reserved registers available
  ReservedIterator freeReserved = find_if(regContents->reserved->begin(),
                                          regContents->reserved->end(),
                                          isFree);
  if(freeReserved != regContents->reserved->end())
  {
    debug("found a reserved register that is available");
    ReservedReg* tmpReg = *freeReserved;
    regXneedMem.first = 
      mark_register_used(tmpReg, inst, purpose, lrid, *regMap);
    return regXneedMem;
  }

  //3) check to see if we can evict someone from a reserved registers
  //or if they were put in the register for the current instruction.
  //we have to do this since there are no more free reserved
  //registers. if we have to do this it means that either we need the
  //register to hold the def, or we were lazy about releasing the
  //register so now is the time to do it.
  {
    ReservedReg* tmpReg;
    tmpReg = 
      find_usable_reg(regContents->reserved, inst, purpose, instUses);
    if(tmpReg != NULL)
    {
      debug("found a reserved register that we can evict");
      regXneedMem.first = 
        mark_register_used(tmpReg, inst, purpose, lrid, *regMap);
      return regXneedMem;
    }
  }

  //4) at this point we need to evict a register that we determined
  //should really be allocated. This is because we have an operation
  //with too many uses. make sure this only happens for FRAME and JSR
  //operations
  debug("no more reserved regs available must evict an allocted reg");
  assert(op->opcode == FRAME ||
         op->opcode == JSRr  ||
         op->opcode == iJSRr ||
         op->opcode == fJSRr ||
         op->opcode == dJSRr ||
         op->opcode == cJSRr ||
         op->opcode == qJSRr ||
         op->opcode == JSRl  ||
         op->opcode == iJSRl ||
         op->opcode == fJSRl ||
         op->opcode == dJSRl ||
         op->opcode == cJSRl ||
         op->opcode == qJSRl);

  //TODO: just for testing
  regXneedMem.first = bullshitReg++;
  return regXneedMem;

  //evict a live range from a register and record the fact that the
  //register has been commandeered
  //to find a suitable register to evict we start with the list of all
  //machine registers. we go through the uses (or defs) in the op and
  //remove any machine reg that is in use in the current operation
  //from the list of potential register we can commandeer
  
  //go through the list of all registers and remove any register that
  //is allocated for this operation
  std::list<ReservedReg*> potentials(regContents->all->size());
  copy(regContents->all->begin(), 
       regContents->all->end(),
       potentials.begin());
  debug("initial potential size: %d", (int)potentials.size());
  debug("regContents->all size: %d", (int)regContents->all->size());
  if(purpose == FOR_USE)
  {
    LRID lridT;
    RegisterList::const_iterator it;
    for(it = instUses.begin(); it != instUses.end(); it++)
    {
      lridT = *it;
      remove_unusable_reg(potentials, lridT, blk);
    }
  }
  else
  {
    LRID lridT;
    RegisterList::const_iterator it;
    for(it = instDefs.begin(); it != instDefs.end(); it++)
    {
      lridT = *it;
      remove_unusable_reg(potentials, lridT, blk);
    }
  }

  //from the remaining registers remove any register that has already
  //been evicted for another register in this operation
  potentials.erase(
      remove_if(potentials.begin(), potentials.end(), 
                ReservedReg_InstEq(inst)),
      potentials.end()
  );


  //there should be at least one register left to choose from. evict
  //it and use it now.
  assert(potentials.size() > 0);
  ReservedReg* tmpReg = potentials.front();
  //TODO: this is a horrible way to find out which LRID corresponds to
  //the evicted machine register. we could keep a mapping, but that
  //seems kind of like a waste of space. I will try this for now and
  //fix it up if it is too slow. we should not be evicting registers
  //often so hopefully its not too bad
  LRID evictedLRID;
  for(evictedLRID = 0; evictedLRID < chowstats.clrFinal; evictedLRID++)
  {
    if(GetMachineRegAssignment(blk, evictedLRID) == tmpReg->machineReg)
      break;
  }
  assert(GetMachineRegAssignment(blk, evictedLRID) == tmpReg->machineReg);
  regContents->evicted->push_back(
    std::make_pair(evictedLRID,tmpReg->machineReg));
  regXneedMem.first = 
    mark_register_used(tmpReg, inst, purpose, lrid, *regMap);
  return regXneedMem;
}

Register mark_register_used(ReservedReg* tmpReg, 
                        Inst* inst, 
                        RegPurpose purpose,
                        LRID lrid,
                        ReservedRegMap& regMap)
{
  //remove any previous mapping that may exist for the live range in
  //this temporary register
  debug("marking reg: %d used for lrid: %d",tmpReg->machineReg, lrid);
  LRID prevLRID = tmpReg->forLRID;
  RegMapIterator rmIt = regMap.find(prevLRID);
  if(rmIt != regMap.end()){regMap.erase(rmIt);}

  //mark the temporary register as used
  tmpReg->free = FALSE;
  tmpReg->forInst = inst;
  tmpReg->forPurpose = purpose;
  tmpReg->forLRID = lrid;
  regMap[lrid] = tmpReg;

  dump_map_contents(regMap);
  return tmpReg->machineReg;
}

void remove_unusable_reg(std::list<ReservedReg*>& potentials, 
                    LRID lridT, Block* blk )
{
  //LRID lridT = SSAName2LRID(ssaName);
  Register mReg = GetMachineRegAssignment(blk, lridT);
  if(mReg != REG_UNALLOCATED)
  {
    debug("removing machine reg %d from potentials (lrid: %d)",
        mReg, lridT);
    potentials.erase(
      remove_if(potentials.begin(), potentials.end(),
                ReservedReg_MRegEq(mReg)),
      potentials.end()
    );
  }
}


ReservedReg* find_usable_reg(const std::vector<ReservedReg*>* reserved,
                            Inst* inst,
                            RegPurpose purpose,
                            const RegisterList& instUses)
{
  ReservedReg* usable = NULL;
 // ReservedIterator reservedIt;
  std::vector<ReservedReg*>::const_iterator reservedIt;
  reservedIt = find_if(reserved->begin(),
                       reserved->end(),
                       ReservedReg_Usable(inst, purpose, instUses));
  if(reservedIt != reserved->end())
  {
    usable = *reservedIt;
  }

  return usable;
}





/*
 *===================
 * reset_free_tmp_regs()
 *===================
 * used to reset the count of which temporary registers are in use for
 * an instruction
 **/
void reset_free_tmp_regs(Inst* last_inst)
{
  debug("resetting free tmp regs");
  //take care of business for each register class
  for(RegisterClass rc = 0; rc < cRegisterClass; rc++)
  {
    RegisterContents* regContents =
      RegisterClass_GetRegisterContentsForRegisterClass(rc);

    //1) anyone that was evicted needs to be loaded back in
    //TODO: this is bullshit!! (says tim).
    //we can do better than just reloading the register because it is
    //the end of the block. we could look down the path that leads
    //from this block and see where the next use is and put the load
    //right before it, but for now we just put the load here
    EvictedList* evicted = regContents->evicted;
    EvictedList::iterator evIT;
    for(evIT = evicted->begin(); evIT != evicted->end(); evIT++)
    {
      LRID evictedLRID = (*evIT).first;
      Insert_Load(evictedLRID, last_inst, (*evIT).second, REG_FP);
    }

    //2) remove all evicted registers from evicted list
    evicted->clear();

    //3) set all reserved registers to be FREE so they can be used in
    //the next block
    ReservedList* reserved = regContents->reserved;
    ReservedList::iterator resIT;
    for(resIT = reserved->begin(); resIT != reserved->end(); resIT++)
    {
      (*resIT)->free = TRUE;
      (*resIT)->forLRID = NO_LRID;
      (*resIT)->forInst = NULL;
    }

    //4) clear the regMap
    regContents->regMap->clear();
  }
}


