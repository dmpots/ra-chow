/* contains code used for assigning machine registers to live ranges.
 * if the live range has not been allocated a register then it is
 * given a temporary register. all of the details of managing
 * temporary registers are in this file.
 *
 * if you want to peek into the bowles of hell then read on...
 */

/*--------------------------INCLUDES---------------------------*/
#include <list>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <utility>


#include "assign.h"
#include "chow.h"
#include "live_range.h"
#include "live_unit.h"
#include "rc.h"
#include "spill.h"
#include "stats.h"
#include "color.h"
#include "mapping.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
/* local types */
struct ReservedReg
{
  Register machineReg;
  Inst* forInst;
  RegPurpose forPurpose;
  LRID forLRID;
  Boolean free;
};

typedef std::map<LRID,ReservedReg*> ReservedRegMap;
typedef const std::vector<ReservedReg*>::iterator ReservedIterator;
typedef std::map<LRID, ReservedReg*>::iterator RegMapIterator;
typedef std::list<std::pair<LRID, ReservedReg*> > EvictedList;
typedef std::vector<ReservedReg*> ReservedList;

struct RegisterContents
{
  //why is evicted a list you ask? it is because it is the only
  //structure which we may remove things and add things in the middle
  //of the collection on a regular basis and we want that to be efficent
  EvictedList* evicted;
  ReservedList* all;
  ReservedRegMap* regMap;
  ReservedList* reserved; 
}; 

/* local variables */
std::vector<RegisterContents> assign_infos; 

/* local functions */
std::pair<Register,bool>
GetFreeTmpReg(LRID lrid, 
                 Block* blk,
                 Inst* origInst, 
                 Inst* updatedInst,
                 Operation* op,
                 RegPurpose purpose,
                 const RegisterList& instUses,
                 const RegisterList& instDefs);

ReservedReg* FindUsableReg( const std::vector<ReservedReg*>* reserved,
                            Inst* inst,
                            RegPurpose purpose,
                            const RegisterList& instUses);
Register MarkRegisterUsed(ReservedReg* tmpReg, 
                        Inst* inst, 
                        RegPurpose purpose,
                        LRID lrid,
                        ReservedRegMap& regMap);

void RemoveUnusableReg(std::list<ReservedReg*>& potentials, 
                         Register ssaName, Block* blk );

RegisterContents* AssignInfoForLRID(LRID lrid);
bool InsertEvictedStore(LRID evictedLRID, 
                        RegisterContents* regContents, 
                        ReservedReg* evictedReg, 
                        Inst* origInst,
                        Inst* updatedInst,
                        Block* blk);


/* used as predicates for seaching reg lists */
Boolean isFree(const ReservedReg* reserved);
class ReservedReg_Usable;
class ReservedReg_MRegEq;
class ReservedReg_InstEq;
class EvictedListElem_Eq;

/* for debugging */
void dump_map_contents(ReservedRegMap& regMap);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Assign {
/* variables */
const Register REG_UNALLOCATED = 666;

/*
 *=========
 * Init()
 *=========
 * Initialize structures need for assignment
 **/
void Init(Arena arena)
{
  using RegisterClass::all_classes;

  //make space for the number of register classes we are using
  assign_infos.resize(all_classes.size());

  //allocate register_contents structs per register class. these are
  //used during register assignment to find temporary registers when
  //we need to evict a register if we don't have enough reserved
  //registers to satisfy the needs of the instruction
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    RegisterClass::RC rc = all_classes[i];

    assign_infos[rc].evicted = new EvictedList;
    assign_infos[rc].all= new ReservedList;
    assign_infos[rc].regMap = new ReservedRegMap;
    assign_infos[rc].reserved= new ReservedList;

    //first add the reserved registers
    //actual reserved must be calculated in this way since we reserve
    //an extra register for the frame pointer, but it is not available
    //to be used as a temporary register so we must remember this when
    //building the reserved regs list
    RegisterClass::ReservedRegsInfo rri = RegisterClass::GetReservedRegInfo(rc);
    int num_reserved = 
      (rc == RegisterClass::INT ? rri.cReserved-1 : rri.cReserved);
    for(int i = 0; i < num_reserved; i++)
    {
      ReservedReg* rr = (ReservedReg*)
        Arena_GetMemClear(arena, sizeof(ReservedReg));
      
      rr->machineReg = rri.regs[i];
      rr->free = TRUE; 
      assign_infos[rc].reserved->push_back(rr);
    }

    //now build the list of all remaining machine registers for this
    //register class.
    Register base = FirstRegister(rc) + rri.cReserved;
    unsigned int num_assignable = RegisterClass::NumMachineReg(rc);
    for(unsigned int i = 0; i < num_assignable; i++)
    {
      ReservedReg* rr = (ReservedReg*)
        Arena_GetMemClear(arena, sizeof(ReservedReg));
      
      rr->machineReg = base+i;
      rr->free = TRUE; 
      assign_infos[rc].all->push_back(rr);
    }
  }
}


/*
 *===================
 * EnsureReg()
 *===================
 * Makes sure that the given live range is in a machin register. 
 * If the live range is not allocated to a register then it will be
 * given an available temporary register.
 *
 * reg - this will be updated with the machine reg for the live range
 **/
void EnsureReg(Register* reg, 
               Block* blk,
               Inst*  origInst,
               Inst** updatedInst, 
               Operation* op,
               RegPurpose purpose,
               const RegisterList& instUses, 
               const RegisterList& instDefs)
{
  using Spill::InsertLoad;
  using Spill::InsertStore;
  using Chow::live_ranges;

  debug("ensuring reg: %d for inst: \n   %s",
     *reg, Debug::StringOfInst(origInst));

  LRID lrid = Mapping::SSAName2OrigLRID(*reg);
  *reg = GetMachineRegAssignment(blk, lrid);

  //this live range is spilled. find a temporary register
  if(*reg == REG_UNALLOCATED)
  {
    std::pair<Register,bool> regXneedMem;
    Register tmpReg;
    bool needMemAccess;

    //find a temporary register for this value
    regXneedMem = GetFreeTmpReg(lrid, blk, origInst, *updatedInst, 
                                op, purpose, instUses, instDefs);

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
        InsertLoad(live_ranges[lrid], *updatedInst, tmpReg, Spill::REG_FP);
      }
      else //FOR_DEF
      {
        *updatedInst = 
          InsertStore(live_ranges[lrid], 
                      *updatedInst, tmpReg, Spill::REG_FP, AFTER_INST);
      }
    }
    *reg = tmpReg;
  }
  else
  {
    debug("reg: %d for lrid: %d is allocated", *reg, lrid);
  }
}

/*
 *===================
 * ResetFreeTmpRegs()
 *===================
 * used to reset the count of which temporary registers are in use for
 * an instruction
 **/
void ResetFreeTmpRegs(Inst* last_inst)
{
  using Spill::InsertLoad;
  using Chow::live_ranges;

  debug("resetting free tmp regs");
  //take care of business for each register class
  for(unsigned int i = 0; i < RegisterClass::all_classes.size(); i++)
  {
    RegisterClass::RC rc = RegisterClass::all_classes[i];
    RegisterContents* regContents = &assign_infos[rc];

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


/*
 *==========================
 * GetMachineRegAssignment()
 *==========================
 * Gets the machine register assignment for a lrid in a given block
 **/
Register GetMachineRegAssignment(Block* b, LRID lrid)
{
  using Coloring::GetColor;
  using Coloring::NO_COLOR;

  if(lrid == Spill::frame.lrid)
    return Spill::REG_FP;

   /* return REG_UNALLOCATED; spill everything */

  Register color = GetColor(b, lrid);
  if(color == NO_COLOR)
    return REG_UNALLOCATED;

  RegisterClass::RC rc = Chow::live_ranges[lrid]->rc;
  return RegisterClass::MachineRegForColor(rc, color);
}

/*
 *===================
 * UnEvict()
 *===================
 * restores any allocted live ranges to their registers if they were
 * evicted to make room for an unallocted live range involved in a
 * function call.
 **/
void UnEvict(Inst** updatedInst)
{
  using RegisterClass::all_classes;
  debug("checking for registers needing unevicting");

  //look at each register classes evicted list
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    EvictedList* evicted = assign_infos[all_classes[i]].evicted;
    if(evicted->size() > 0)
    {
      debug("some registers need unevicting");
      ReservedRegMap* regMap = assign_infos[all_classes[i]].regMap;
      //1) anyone that was evicted needs to be loaded back in
      //TODO: this is bullshit!! (says tim).
      //we can do better than just reloading the register because it is
      //the end of the block. we could look down the path that leads
      //from this block and see where the next use is and put the load
      //right before it, but for now we just put the load here
      EvictedList::iterator evIT;
      for(evIT = evicted->begin(); evIT != evicted->end(); evIT++)
      {
        LRID evictedLRID = (*evIT).first;
        ReservedReg* kicked = (*evIT).second;
        debug("unevicting lrid: %d to reg: %d", evictedLRID,
          kicked->machineReg);

        //load the live range back into its register if we kicked one
        //out when commendeering the machine register
        if(evictedLRID != NO_LRID)
        {
        *updatedInst = 
          Spill::InsertLoad(Chow::live_ranges[evictedLRID], 
                            *updatedInst, kicked->machineReg, 
                            Spill::REG_FP, AFTER_INST);
        }

        //find the lrid that is in our register and remove it from map
        RegMapIterator rmIt = regMap->find(kicked->forLRID);
        // ---- DEBUG ----->
        if(rmIt == regMap->end())
        {
          debug("expected to find lrid: %d in reg: %d, but didn't",
                kicked->forLRID, kicked->machineReg);
          dump_map_contents(*regMap);
        }
        // ---- DEBUG -----<
        assert(rmIt != regMap->end());
        regMap->erase(rmIt);

        //reset values on evicted reg. this is needed in case this
        //register gets chosen for eviction again we don't want the old
        //values hanging around in the ReservedReg*
        kicked->free = TRUE;
        kicked->forLRID = NO_LRID;
        kicked->forInst = NULL;
      }

      //2) remove all evicted registers from evicted list
      evicted->clear();
    }
  }
}




}//end Assign namespace


/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/
namespace {
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
 * ReservedReg_Usable()
 *===================
 * says whether we can use the temporary register or not
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

class EvictedListElem_Eq 
: public std::unary_function<std::pair<LRID, ReservedReg*>, bool>
{
  Register mReg; 
  LRID lrid; 

  public:
  EvictedListElem_Eq(Register mReg_, LRID lrid_) 
    : mReg(mReg_), lrid(lrid_) {};
  bool operator() (const std::pair<LRID,ReservedReg*> p) const 
  {
    return p.first == lrid && p.second->machineReg == mReg;
  }
};

/*
 *===================
 * GetFreeTmpReg()
 *===================
 * gets the next available free tmp register from the pool of
 * available free temp registers
 **/
std::pair<Register,bool>
GetFreeTmpReg(LRID lrid, 
              Block* blk, 
              Inst* origInst, 
              Inst* updatedInst,
              Operation* op,
              RegPurpose purpose, 
              const RegisterList& instUses, 
              const RegisterList& instDefs)
{
  using Assign::GetMachineRegAssignment;

  debug("looking for temporary reg for %s of lrid: %d",
    (purpose == FOR_USE ? "FOR_USE" : "FOR_DEF"), lrid);

  std::pair<Register,bool> regXneedMem;
  regXneedMem.second = true; //default is needs memory load/store

  //get the register contents struct for this lrid register class
  RegisterContents* regContents = AssignInfoForLRID(lrid);

  //1) check to see if this lrid is already stored in one of our
  //temporary registers.
  ReservedRegMap* regMap = (regContents->regMap);
  RegMapIterator it = regMap->find(lrid);
  if(it != regMap->end()) //found it
  {
    debug("found the live range already in a temporary register");
    ReservedReg* rReg  = ((*it).second);
    rReg->forInst = origInst;
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
      MarkRegisterUsed(tmpReg, origInst, purpose, lrid, *regMap);
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
      FindUsableReg(regContents->reserved, origInst, purpose, instUses);
    if(tmpReg != NULL)
    {
      debug("found a reserved register that we can evict");
      regXneedMem.first = 
        MarkRegisterUsed(tmpReg, origInst, purpose, lrid, *regMap);
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
//DROP >>>>
// static Register bullshitReg = 1000;
//  regXneedMem.first = bullshitReg++;
//  return regXneedMem;
//DROP <<<<

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
  debug("trimming registers needed in this inst");
  if(purpose == FOR_USE)
  {
    LRID lridT;
    RegisterList::const_iterator it;
    for(it = instUses.begin(); it != instUses.end(); it++)
    {
      lridT = *it;
      RemoveUnusableReg(potentials, lridT, blk);
    }
  }
  else
  {
    LRID lridT;
    RegisterList::const_iterator it;
    for(it = instDefs.begin(); it != instDefs.end(); it++)
    {
      lridT = *it;
      RemoveUnusableReg(potentials, lridT, blk);
    }
  }

  //from the remaining registers remove any register that has already
  //been evicted for another register in this operation
  debug("trimming registers already evicted");
  potentials.erase(
      remove_if(potentials.begin(), potentials.end(), 
                ReservedReg_InstEq(origInst)),
      potentials.end()
  );


  //there should be at least one register left to choose from. evict
  //it and use it now.
  assert(potentials.size() > 0);
  ReservedReg* tmpReg = potentials.front();
  debug("trimmed potential size: %d", (int)potentials.size());
  debug("evicting machine register: %d", tmpReg->machineReg);

  //find the lrid assigned to this  machine register so that we know
  //which live range is about to be evicted
  RegisterClass::RC rc = Chow::live_ranges[lrid]->rc;
  Color color = RegisterClass::ColorForMachineReg(rc, tmpReg->machineReg);
  LRID evictedLRID = Coloring::GetLRID(blk, rc, color);

  // see if we found a live range that is going to be evicted. it may
  // be the case that there is no live range to evict from the register
  // if no live range was assigned to the tmpReg we are looking at.
  // this can happen because it may not be worth it to keep a live
  // range in this block if there is only a def (for example in a
  // frame statement it is defined and you get no savings if there is
  // no use in the block).
  if(evictedLRID != NO_LRID)
  {
    debug("lrid: %d kicked out of reg: %d", evictedLRID,tmpReg->machineReg);
    assert(GetMachineRegAssignment(blk, evictedLRID) == tmpReg->machineReg);
    
    //store the register if this eviction is not for a FRAME statement
    if(op->opcode != FRAME)
    {
      //if we did not need a store then note that no live range was
      //evicted so that we don't insert a load when we unevict
      if(!InsertEvictedStore(evictedLRID, regContents, tmpReg, 
                            origInst, updatedInst, blk))
      {
        evictedLRID = NO_LRID;
      }
    }
  }
  regContents->evicted->push_back(std::make_pair(evictedLRID,tmpReg));

  regXneedMem.first = 
    MarkRegisterUsed(tmpReg, origInst, purpose, lrid, *regMap);
  return regXneedMem;
}

/*
 *=====================
 * InsertEvictedStore()
 *=====================
 * Inserts a store for an evicted live range if needed.
 * 
 * A store is needed if the live range is not killed before it is used
 * in this block or if it reaches the end of the block with no uses or
 * defs. This is to prevet storing a live range when its register is
 * allocated for a def in the given block. If we always stored then we
 * could possibly store incorrect values into memory so instead we
 * must make sure that either there is no def in this block so the
 * current value must be correct, or that the def has already
 * happened.
 **/
bool InsertEvictedStore(LRID evictedLRID, 
                        RegisterContents* regContents, 
                        ReservedReg* evictedReg, 
                        Inst* origInst,
                        Inst* updatedInst,
                        Block* blk)
{
  debug("checking if we need a store for evicted: %d", evictedLRID);
  using Mapping::SSAName2OrigLRID;

  LiveRange* evictedLR = Chow::live_ranges[evictedLRID]; 
  LiveUnit* lu = evictedLR->LiveUnitForBlock(blk);
  bool need_store = true;

  //Block_Dump(blk, NULL, TRUE);
  //Debug::LiveRange_Dump(evictedLR);
  assert(lu != NULL);

  //if this live unit contains a def then the evicted live range may
  //have been allocated the register just for this def. 
  if(lu->defs > 0)
  {
    //look forward
    //through the block to see if this live range is killed before
    //it is used or the end of the block is reached. if it is killed
    //then no store is needed and in fact it is incorrect to insert a
    //store
    for(Inst* runner = updatedInst->next_inst; 
        runner != blk->inst; 
        runner = runner->next_inst)
    {
      Operation** op;
      Inst_ForAllOperations(op, runner)
      {
        Register* reg;
        Operation_ForAllUses(reg, *op)
        { 
          //upward exposed use
          if(SSAName2OrigLRID(*reg) == evictedLR->orig_lrid) 
          {
            goto stop_looking;
          }
        }

        Operation_ForAllDefs(reg, *op)
        {
          //kill
          if(SSAName2OrigLRID(*reg) == evictedLR->orig_lrid) 
          {
            debug("lr %d does not need store because it is killed "
                  "before used", evictedLRID);
            need_store = false;
            goto stop_looking;
          }
        }
      } 
    }
    stop_looking: ; //targe for gotos
  }


  if(need_store)
  {
    debug("lr %d needs store because it is evicted", evictedLRID);
    Spill::InsertStore(evictedLR, origInst, evictedReg->machineReg, 
                       Spill::REG_FP, BEFORE_INST);
  }
  return need_store;
}


Register MarkRegisterUsed(ReservedReg* tmpReg, 
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
  if(rmIt != regMap.end()){
    debug("removing previous mapping reg: %d for lrid: %d",
          (*rmIt).second->machineReg, prevLRID);
    regMap.erase(rmIt);
  }

  //mark the temporary register as used
  tmpReg->free = FALSE;
  tmpReg->forInst = inst;
  tmpReg->forPurpose = purpose;
  tmpReg->forLRID = lrid;
  regMap[lrid] = tmpReg;

  dump_map_contents(regMap);
  return tmpReg->machineReg;
}

void RemoveUnusableReg(std::list<ReservedReg*>& potentials, 
                    LRID lridT, Block* blk )
{
  Register mReg = Assign::GetMachineRegAssignment(blk, lridT);
  if(mReg != Assign::REG_UNALLOCATED)
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


ReservedReg* FindUsableReg(const std::vector<ReservedReg*>* reserved,
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


RegisterContents* AssignInfoForLRID(LRID lrid)
{
  return &assign_infos[Chow::live_ranges[lrid]->rc];
}

}//end anonymous namespace




