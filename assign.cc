/* contains code used for assigning machine registers to live ranges.
 * if the live range has not been allocated a register then it is
 * given a temporary register. all of the details of managing
 * temporary registers are in this file.
 *
 * if you want to peek into the bowles of hell then read on...
 */

/*--------------------------INCLUDES---------------------------*/
#include <SSA.h>
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
#include "cfg_tools.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
/* local types */

//this struct is used to keep track of the contents of any temporary
//registers as well as any registers that are evicted in FRAME/JSR
struct AssignedReg
{
  Register machineReg;
  Inst* forInst;
  RegPurpose forPurpose;
  LRID forLRID;
  Boolean free;
  int index;
  bool dirty;
  int next_use;
  bool local;
  std::vector<AssignedReg*>* regpool;
};

//handy typedefs to save typing
typedef std::map<LRID,AssignedReg*> AssignedRegMap;
typedef std::map<LRID, AssignedReg*>::iterator RegMapIterator;
typedef std::vector<std::pair<LRID, AssignedReg*> > EvictedList;
typedef std::vector<AssignedReg*> AssignedRegList;

//this struct is used to keep track of which lrids are currently in
//the temporary registers as well as live ranges that may have been
//evicted. these contents are also used for choosing which registers
//can be evicted
struct RegisterContents
{
  EvictedList* evicted;       //currently evicted registers
  AssignedRegList* assignable;//all non-reserved registers
  AssignedRegList* reserved;  //all reserved registers
  RegisterClass::RC rc;       //register class for these contents
  //iterator for using reserved regs in a round robin fashion
  AssignedRegList::iterator roundRobinIt;  
}; 

/* local variables */
std::vector<RegisterContents> reg_contents; 
std::map<Inst*, int> inst_order;

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

Register MarkRegisterUsed(AssignedReg* tmpReg, 
                        Inst* inst, 
                        RegPurpose purpose,
                        LRID lrid,
                        AssignedRegList* reglist,
                        unsigned int rwidth);

RegisterContents* RegContentsForLRID(LRID lrid);
bool InsertEvictedStore(LRID evictedLRID, 
                        RegisterContents* regContents, 
                        AssignedReg* evictedReg, 
                        Inst* origInst,
                        Inst* updatedInst,
                        Block* blk);
AssignedReg* 
FindSutiableTmpReg(RegisterContents* regContents, 
                   Block* blk, 
                   Inst* origInst, 
                   Inst* updatedInst, 
                   RegPurpose purpose,
                   const RegisterList& instUses,
                   unsigned int reg_width);
AssignedReg* 
Belady(const AssignedRegList& choices, Block* blk, Inst* origInst, uint);
uint ResetForRegWidth(AssignedRegList::const_iterator begin);
uint ResetForRegWidth(AssignedReg* tmpReg);

int 
UpdateDistances(
  std::map<LRID, int>& distances,
  Block* blk, 
  int startdist, 
  Inst* start_inst = NULL
);

void AssignRegister(
  AssignedReg* tmpReg,
  AssignedRegList* reglist, 
  LRID lrid, 
  Inst* origInst, 
  RegPurpose purpose,
  unsigned int rwidth,
  bool is_dirty
);
void StoreIfNeeded(AssignedReg* tmpReg, Inst* origInst, Block* blk);
AssignedReg* FindInRegPool(LRID lrid, AssignedRegList* regpool);
void ResetRegSpan(AssignedReg* startingReg, uint width);
void StoreAndResetRegSpan( AssignedReg* , Inst* , Block* , uint );

//for local allocation
bool recompute_dist_map = true;
std::map<Inst*, std::map<LRID, int> >distance_map;
void BuildInstOrderingMap();
void ComputeDistanceMap(Block* start_blk);
void RecordDistance(
  Register vreg,
  Inst* inst,
  std::map<LRID, int>& next_uses);
void AnnotateBlockWithDistances(Block* blk, std::map<LRID, int>& next_uses);

/* inline functions */
inline unsigned int UB(unsigned int size, unsigned int width)
{
  return size - width + 1;
}
unsigned int RegWidth(LRID lrid);
inline unsigned int RegWidth(AssignedReg* tmpReg)
{
  return RegWidth(tmpReg->forLRID);
}
inline unsigned int RegWidth(LRID lrid)
{
  int rwidth = 1; 
  if(lrid != NO_LRID){rwidth = Chow::live_ranges[lrid]->RegWidth();}
  return rwidth;
}

inline void ResetAssignedReg(AssignedReg* ar)
{
  ar->free = TRUE;
  ar->forLRID = NO_LRID;
  ar->forInst = NULL;
  ar->dirty   = false;
  ar->next_use = -1;
  ar->local    = false;
}

/* used as predicates for seaching reg lists */
template<class Predicate> 
const AssignedRegList&
FindCandidateRegs(const AssignedRegList* possibles, 
                  unsigned int reg_width, 
                  Predicate pred); 
class AssignedReg_Usable;

/* for debugging */
void dump_assignedlist_contents(const AssignedRegList& arList);
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
  reg_contents.resize(all_classes.size());

  //allocate register_contents structs per register class. these are
  //used during register assignment to find temporary registers when
  //we need to evict a register if we don't have enough reserved
  //registers to satisfy the needs of the instruction
  for(unsigned int i = 0; i < all_classes.size(); i++)
  {
    RegisterClass::RC rc = all_classes[i];

    reg_contents[rc].evicted = new EvictedList;
    reg_contents[rc].assignable= new AssignedRegList;
    reg_contents[rc].reserved= new AssignedRegList;
    reg_contents[rc].rc = rc;

    //first add the reserved registers
    //actual reserved must be calculated in this way since we reserve
    //an extra register for the frame pointer, but it is not available
    //to be used as a temporary register so we must remember this when
    //building the reserved regs list
    RegisterClass::ReservedRegsInfo rri = RegisterClass::GetReservedRegInfo(rc);
    for(int i = 0; i < rri.cReserved; i++)
    {
      AssignedReg* rr = (AssignedReg*)
        Arena_GetMemClear(arena, sizeof(AssignedReg));
      
      rr->machineReg = rri.regs[i];
      rr->free = TRUE; 
      rr->index = i;
      rr->regpool = reg_contents[rc].reserved;
      rr->regpool->push_back(rr);
    }
    //set starting point for round robin usage
    reg_contents[rc].roundRobinIt = reg_contents[rc].reserved->begin();

    //now build the list of all remaining machine registers for this
    //register class.
    Register base = FirstRegister(rc) + rri.cHidden;
    int num_assignable = RegisterClass::NumMachineReg(rc);
    for(int i = 0; i < num_assignable; i++)
    {
      AssignedReg* rr = (AssignedReg*)
        Arena_GetMemClear(arena, sizeof(AssignedReg));
      
      rr->machineReg = base+i;
      rr->free = TRUE; 
      rr->index = i;
      rr->regpool = reg_contents[rc].assignable;
      rr->regpool->push_back(rr);
    }
  }

  //get an ordering of instructions for local allocation decisions
  BuildInstOrderingMap();
}

/*
 *===================
 * InitLocalAllocation()
 *===================
 * initializes the data structures needed to do local allocation. this
 * is only done for blocks that are the head of a SingleSuccessorPath,
 * otherwise the block is part of such a path and the information does
 * not need to be recomputed.
 **/
void InitLocalAllocation(Block* blk)
{
  if(recompute_dist_map)
  {
    ComputeDistanceMap(blk);
    recompute_dist_map = false;
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

  debug("ensuring r%d for inst %d (0x%p): \n   %s",
     *reg, inst_order[origInst], origInst, Debug::StringOfInst(origInst));

  LRID orig_lrid = Mapping::SSAName2OrigLRID(*reg);
  *reg = GetMachineRegAssignment(blk, orig_lrid);

  //this live range is spilled. find a temporary register
  if(*reg == REG_UNALLOCATED)
  {
    std::pair<Register,bool> regXneedMem;
    Register tmpReg;
    bool needMemAccess;

    //find a temporary register for this value
    regXneedMem = GetFreeTmpReg(orig_lrid, blk, origInst, *updatedInst, 
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
      /* must grab the actual live range here to make sure that loads
       * and stores respect the rematerialization settings */
      LiveRange* lr = (*live_ranges[orig_lrid]->blockmap)[bid(blk)];
      if(purpose == FOR_USE)
      {
        InsertLoad(lr, *updatedInst, tmpReg, Spill::REG_FP);
      }
      else //FOR_DEF
      {
        *updatedInst = 
          InsertStore(lr,*updatedInst, tmpReg, Spill::REG_FP, AFTER_INST);
      }
    }
    *reg = tmpReg;
  }
  else
  {
    debug("reg: %d for orig_lrid: %d is allocated", *reg, orig_lrid);
  }
}

/*
 *===================
 * HandleCopy()
 *===================
 * Assigns registers for a copy instruction.
 *
 * Copies are handled separately because we want to catch the case
 * where the source of the copy is not allocated a register and would
 * require a load. 
 **/
void HandleCopy(Block* blk,
               Inst*  origInst,
               Inst** updatedInst, 
               Operation** op,
               const RegisterList& instUses, 
               const RegisterList& instDefs)
{
  debug("handling copy for inst %d (0x%p): \n   %s",
     inst_order[origInst], origInst, Debug::StringOfInst(origInst));

  bool delete_copy = false;
  Register* srcp =  &((*op)->arguments[0]);
  Register* destp = &((*op)->arguments[1]);
  
  LRID src_lrid = Mapping::SSAName2OrigLRID(*srcp);
  *srcp = GetMachineRegAssignment(blk, src_lrid);
  if(*srcp == REG_UNALLOCATED)
  {
    //check to see if its already in a tmp reg
    if(FindInRegPool(src_lrid, RegContentsForLRID(src_lrid)->reserved))
    {
      //and call GetFreeTmpReg so that the data structures get updated
      //as necessary for belady
      std::pair<Register,bool> regXneedMem =
        GetFreeTmpReg(src_lrid, blk, origInst, *updatedInst, 
                      *op, FOR_USE, instUses, instDefs);

      assert(regXneedMem.second == false);
      *srcp = regXneedMem.first;
    }
    else
    {
      delete_copy = true;
    }
  }
  //we always need a register for the dest of the copy
  EnsureReg(destp, blk, origInst, updatedInst,
            *op, FOR_DEF, instUses, instDefs);

  if(delete_copy)
  {
    //insert load from src to dest reg
    LiveRange* lr = (*Chow::live_ranges[src_lrid]->blockmap)[bid(blk)];
    Spill::InsertLoad(lr, *updatedInst, *destp, Spill::REG_FP);
    
    //delete copy
    *op = NULL;
  }
}

/*
 *===================
 * ResetFreeTmpRegs()
 *===================
 * used to reset the count of which temporary registers are in use for
 * an instruction
 **/
void ResetFreeTmpRegs(Block* blk)
{
  //need to reset all tmps if there are multiple paths from this block
  //or multiple paths to the successor
  bool reset_all = !SingleSuccessorPath(blk);

  //this if/else looks like a big copy and paste job, which it is but
  //it was easier to get it to work this way and probably easier to
  //understand later as the conditionals in the loop get a little
  //messy if you try to merge the cases.
  if(reset_all)
  {
    //make sure that we update our distance map for the next block
    recompute_dist_map = true;

    //take care of business for each register class
    debug("resetting all tmp regs to be free");
    for(unsigned int i = 0; i < reg_contents.size(); i++)
    {
      RegisterContents* regContents = &reg_contents[i];
      //3) set all reserved registers to be FREE so they can be used in
      //the next block
      AssignedRegList* reserved = regContents->reserved;
      StoreAndResetRegSpan(
        reserved->front(), Block_LastInst(blk), blk, reserved->size()
      );
    }
  }
  else
  {
    //take care of business for each register class
    debug("resetting tmp regs allocated in succesor to be free");
    for(unsigned int i = 0; i < reg_contents.size(); i++)
    {
      RegisterContents* regContents = &reg_contents[i];
      AssignedRegList* reserved = regContents->reserved;
      AssignedRegList::const_iterator resIT;
      for(resIT = reserved->begin(); resIT != reserved->end(); resIT++)
      {
        //reset the reg if this tmp reg holds a live range that has a
        //real register in the next block
        if(IsAllocated((*resIT)->forLRID, blk->succ->succ))
        {
          debug("resetting tmp reg: r%d for lrid: %d", 
            (*resIT)->machineReg, (*resIT)->forLRID);
          StoreAndResetRegSpan(
            *resIT, Block_LastInst(blk), blk, RegWidth(*resIT)
          );
          //could increment the iterator here to skip next width regs
        }
      }
    }
  }
}

/*
 *==========================
 * IsAllocated()
 *==========================
 * returns true if the live range is allocated a register in the
 * block. the live range does not have to have originally contained
 * the block.
 **/
inline bool IsAllocated(LRID lrid, Block* blk)
{
  if(lrid == NO_LRID) return false;
  //have to check to see if the block was ever part of the live range
  if((*Chow::live_ranges[lrid]->blockmap)[bid(blk)] == NULL) return false;
  return (GetMachineRegAssignment(blk, lrid) != REG_UNALLOCATED);
}


/*
 *==========================
 * GetMachineRegAssignment()
 *==========================
 * Gets the machine register assignment for a lrid in a given block
 * the block must have originally been part of the live range.
 **/
Register GetMachineRegAssignment(Block* b, LRID lrid)
{
  using Coloring::GetColor;
  using Coloring::NO_COLOR;

  if(lrid == Spill::frame.lrid)
    return Spill::REG_FP;

   /* return REG_UNALLOCATED; to spill everything */

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
  debug("checking for registers needing unevicting");

  //look at each register classes evicted list
  for(unsigned int i = 0; i < reg_contents.size(); i++)
  {
    EvictedList* evicted = reg_contents[i].evicted;
    if(evicted->size() > 0)
    {
      debug("some registers need unevicting");
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
        AssignedReg* kicked = (*evIT).second;

        //load the live range back into its register if we kicked one
        //out when commendeering the machine register
        if(evictedLRID != NO_LRID)
        {
          debug("unevicting lrid: %d to reg: %d", evictedLRID,
            kicked->machineReg);
          *updatedInst = 
            Spill::InsertLoad(Chow::live_ranges[evictedLRID], 
                              *updatedInst, kicked->machineReg, 
                              Spill::REG_FP, AFTER_INST);
        }
      }

      //reset values on assigned regs. this is needed in case a
      //register gets chosen for eviction again we don't want the old
      //values hanging around in the AssignedReg*
      for(AssignedRegList::iterator it=reg_contents[i].assignable->begin();
          it != reg_contents[i].assignable->end();
          it++)
      {
        ResetAssignedReg(*it);
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
void dump_reglist_contents(const RegisterList& regList, Block* blk)
{
  debug("-- reglist contents --");
  RegisterList::const_iterator rmIt;
  for(rmIt = regList.begin(); rmIt != regList.end(); rmIt++)
    debug("  %d --> r%d", (*rmIt), 
      Assign::GetMachineRegAssignment(blk, *rmIt));
  debug("-- end reglist contents --");
}

void dump_assignedlist_contents(const AssignedRegList& arList)
{
  debug("-- assigned reglist contents --");
  AssignedRegList::const_iterator rmIt;
  for(rmIt = arList.begin(); rmIt != arList.end(); rmIt++)
    debug("  r%d (%d lrid %s for inst %p(%d) nxU: %d)", 
      (*rmIt)->machineReg, (*rmIt)->forLRID, 
      ((*rmIt)->dirty ? "dirty" : "clean"),
      (*rmIt)->forInst, inst_order[(*rmIt)->forInst],(*rmIt)->next_use);
  debug("-- end assigned reglist contents --");
}

/*
 *=======================
 * AssignedReg_Usable()
 *=======================
 * says whether we can use the temporary register or not
 **/
class AssignedReg_Usable : public std::unary_function<AssignedReg*, bool>
{
  const Inst* inst;
  RegPurpose purpose;
  const RegisterList& instUses;

  public:
  AssignedReg_Usable(const Inst* inst_, RegPurpose purpose_,
                     const RegisterList& instUses_) :
    inst(inst_), purpose(purpose_), instUses(instUses_) {}
  /* we can use a previously used reserved reg if it is for a
   * different instruction or it was used in the instruction to hold
   * a USE and we now need it for a DEF. we also check the list of
   * uses in the inst to make sure we don't use this temp register if
   * the lrid it stores is needed for this instruction (even if it was
   * put in for the previous instruction for example) */
  bool operator() (const AssignedReg* reserved) const 
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

/*
 *=======================
 * AssignedReg_IsFree()
 *=======================
 * predicate for whether a temporary register is free or not
 **/
class AssignedReg_IsFree : public std::unary_function<AssignedReg*, bool>
{
  public:
  bool operator() (const AssignedReg* rReg) const 
  {
    return rReg->free;
  }
};

/*
 *=======================
 * AssignedReg_LridEq()
 *=======================
 * predicate for whether a temporary register has a matching LRID
 **/
class AssignedReg_LridEq : public std::unary_function<AssignedReg*, bool>
{
  LRID lrid;
  public:
  AssignedReg_LridEq(LRID _lrid) : lrid(_lrid){};
  bool operator() (const AssignedReg* rReg) const 
  {
    return rReg->forLRID == lrid;
  }
};

/*
 *=======================
 * AssignedReg_Evictable()
 *=======================
 * predicate for whether a register can be evicted if needed in a
 * JSR/FRAME instruction
 **/
class AssignedReg_Evictable :public std::unary_function<AssignedReg*, bool>
{
  Inst* inst; 
  const RegisterList& forbidden_regs;
  Block* blk;

  public:
  AssignedReg_Evictable(Inst* inst_, const RegisterList& rList, Block* b): 
    inst(inst_), forbidden_regs(rList), blk(b) {};
  bool operator() (const AssignedReg* aReg) const 
  {
    //can not use if already assigned for the same inst
    if(aReg->forInst == inst) return false;

    //can not use if will be assigned in the future
    for(RegisterList::const_iterator it = forbidden_regs.begin();
        it != forbidden_regs.end();
        it++)
    {
      Register mReg = Assign::GetMachineRegAssignment(blk, *it);
      if(mReg != Assign::REG_UNALLOCATED && mReg == aReg->machineReg) 
        return false;
    }

    //othwise...
    return true;
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

  //a memory access is only needed if we are bringing a value from
  //memory into a register, or we are evicting a register that is used
  //for a def
  regXneedMem.second = false;

  //get the register contents struct for this lrid register class
  RegisterContents* regContents = RegContentsForLRID(lrid);
  unsigned int rwidth = RegWidth(lrid);
  dump_assignedlist_contents(*regContents->reserved);

  //1) check to see if this lrid is already stored in one of our
  //temporary registers.
  {
    AssignedReg* tmpReg = FindInRegPool(lrid, regContents->reserved);
    if(tmpReg != NULL)
    {
      debug("found the live range already in a temporary register");
      AssignRegister(tmpReg, tmpReg->regpool, lrid, origInst, purpose, 
                     rwidth, tmpReg->dirty || (purpose == FOR_DEF));
      regXneedMem.first  = tmpReg->machineReg;
      return regXneedMem;
    }
  }

  //find a sutiable temporary register to use, either
  //2) a reserved register that is still available, or
  //3) kick someone out of an occupied reserved register not needed
  //for this instruction
  {//private scope for tmpReg
    AssignedReg* tmpReg = 
      FindSutiableTmpReg(regContents, blk, origInst, updatedInst,
                         purpose, instUses, rwidth);
    if(tmpReg != NULL)
    {
      regXneedMem.first = 
        MarkRegisterUsed(tmpReg, origInst, purpose, lrid,
                         regContents->reserved, rwidth);
      if(purpose == FOR_USE) regXneedMem.second = true;
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
//  static Register bullshitReg = 1000;
//  regXneedMem.first = bullshitReg++;
//  return regXneedMem;
//DROP <<<<

  //first we have to seach through the assignable registers since it
  //may be the case that we have already evicted a register for the
  //lrid and it appears twice in the JSR call
  {
    AssignedReg* tmpReg = FindInRegPool(lrid, regContents->assignable);
    if(tmpReg != NULL)
    {
      regXneedMem.first  = tmpReg->machineReg;
      return regXneedMem;
    }
  }

  //evict a live range from a register and record the fact that the
  //register has been commandeered
  //to find a suitable register to evict we start with the list of all
  //machine registers. we go through the uses (or defs) in the op and
  //remove any machine reg that is in use in the current operation
  //from the list of potential register we can commandeer
  const AssignedRegList& potentials = 
    purpose == FOR_USE ?
      FindCandidateRegs(regContents->assignable, 
                        rwidth, 
                        AssignedReg_Evictable(origInst,instUses,blk))
      :
      FindCandidateRegs(regContents->assignable, 
                        rwidth, 
                        AssignedReg_Evictable(origInst,instDefs,blk));

  //there should be at least one register left to choose from. evict
  //it and use it now.
  //dump_reglist_contents(instUses, blk);
  //dump_assignedlist_contents(*regContents->assignable);
  assert(potentials.size() > 0);
  AssignedReg* tmpReg = potentials.front();
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
    bool inserted_store = false;
    
    //store the register if this eviction is not for a FRAME statement
    if(op->opcode != FRAME)
    {
      inserted_store = 
        InsertEvictedStore(evictedLRID, regContents, tmpReg, 
                            origInst, updatedInst, blk);
    }
    //if we did not need a store then note that no live range was
    //evicted so that we don't insert a load when we unevict
    if(!inserted_store || op->opcode == FRAME)
    {
      evictedLRID = NO_LRID;
    }
  }
  regContents->evicted->push_back(std::make_pair(evictedLRID,tmpReg));

  regXneedMem.first = 
    MarkRegisterUsed(tmpReg, origInst, purpose, lrid, 
                     regContents->assignable, rwidth);
  regXneedMem.second = true; //always load uses and always store defs
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
                        AssignedReg* evictedReg, 
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


/*
 *=====================
 * MarkRegisterUsed()
 *=====================
 * Updates structures so that the register is known to be in use. If
 * it is replacing another register that was previously in the
 * register then the appropriate lists are updated so that we know
 * those registers are no longer used by the previous live range.
 */
Register MarkRegisterUsed(AssignedReg* tmpReg, 
                          Inst* inst, 
                          RegPurpose purpose,
                          LRID lrid,
                          AssignedRegList* reglist,
                          unsigned int rwidth)
{
  debug("marking reg: %d used for lrid: %d",tmpReg->machineReg, lrid);

  //remove any previous mappings that may exist for the live range in
  //the span of registers that will be assigned starting with the head 
  //temporary register
  ResetRegSpan(tmpReg, rwidth);
  
  //update info in AssignedReg structs
  AssignRegister(tmpReg, reglist, lrid, inst, purpose, rwidth, 
                (purpose == FOR_DEF));
  return tmpReg->machineReg;
}

void AssignRegister(
  AssignedReg* tmpReg,
  AssignedRegList* reglist, 
  LRID lrid, 
  Inst* origInst, 
  RegPurpose purpose,
  unsigned int rwidth,
  bool is_dirty
)
{
  for(unsigned int i = tmpReg->index; i< tmpReg->index + rwidth; i++)
  {
    //mark the temporary register as used
    (*reglist)[i]->free = FALSE;
    (*reglist)[i]->forInst = origInst;
    (*reglist)[i]->forPurpose = purpose;
    (*reglist)[i]->forLRID = lrid;
    (*reglist)[i]->dirty = is_dirty;
    (*reglist)[i]->next_use = distance_map[origInst][lrid];
    (*reglist)[i]->local = Chow::live_ranges[lrid]->is_local;
  }
  debug("assigned (%s) reg: %d for lrid: %d(%s), next use: %d",
    tmpReg->dirty ? "dirty" : "clean",
    tmpReg->machineReg, lrid, tmpReg->local ? "local" : "global",
    tmpReg->next_use);
}

/*
 *=====================
 * RegContentsForLRID
 *=====================
 */
RegisterContents* RegContentsForLRID(LRID lrid)
{
  return &reg_contents[Chow::live_ranges[lrid]->rc];
}

/*
 *=====================
 * FindSuitableTmpReg
 *=====================
 * returns a temporary register that can be used to hold an
 * unallocated live range or NULL if no such register exists.
 */
AssignedReg* 
FindSutiableTmpReg(RegisterContents* regContents, 
                   Block* blk,
                   Inst* origInst, 
                   Inst* updatedInst, 
                   RegPurpose purpose,
                   const RegisterList& instUses,
                   unsigned int reg_width)
{
  AssignedReg* tmpReg = NULL;

  //2) check to see if we have any more reserved registers available
  AssignedRegList reserved = *(regContents->reserved);
  {
    debug("searching for a free temporary register");
    const AssignedRegList& free = 
      FindCandidateRegs(regContents->reserved, 
                        reg_width, 
                        AssignedReg_IsFree());
    if(!free.empty())
    {
      debug("found a reserved register that is free");
      tmpReg = free.front();
    }
  }

  //3) check to see if we can evict someone from a reserved registers
  //or if they were put in the register for the current instruction.
  //we have to do this since there are no more free reserved
  //registers. if we have to do this it means that either we need the
  //register to hold the def, or we were lazy about releasing the
  //register so now is the time to do it.
  if(tmpReg == NULL)
  {
    debug("searching for an evictable temporary register");
    const AssignedRegList& kickable = 
      FindCandidateRegs(regContents->reserved, 
                        reg_width, 
                        AssignedReg_Usable(origInst, purpose, instUses));
    if(!kickable.empty())
    {
      debug("found a reserved register that we can evict");
      //tmpReg = kickable.front(); StoreIfNeeded(tmpReg,origInst,blk);
      tmpReg = Belady(kickable, blk, origInst, reg_width);
    }
  }

  return tmpReg;
}

/*
 *=====================
 * FindCandidateRegs()
 *=====================
 * Searches for +reg_width+ registers in a row that satisfy the
 * predicate. These registers are returned in a candidates list.
 */
template<class Predicate>
const AssignedRegList&
FindCandidateRegs(const AssignedRegList* possibles, 
                  unsigned int reg_width, 
                  Predicate pred) 
{
  static AssignedRegList candidates;
  candidates.clear();

  unsigned int ub =  UB(possibles->size(), reg_width);
  for(unsigned int r = 0; r < ub; r += reg_width)
  {
    bool ok = true;
    for(unsigned int w = 0; w < reg_width; w++)
    {
      ok = ok && pred((*possibles)[r+w]);
    }
    if(ok)
    {
      debug("found a candidate: %d", (*possibles)[r]->machineReg);
      candidates.push_back((*possibles)[r]);
    }
  }

  return candidates;
}

/*
 *=====================
 * Belady()
 *=====================
 * Run the belady local register allocation algorithm to decied which
 * temporary register should be used next. The algorithm looks for the
 * register which is not used for the longes amount of time and
 * chooses it for eviction.
 */
AssignedReg* 
Belady(
  const AssignedRegList& choices, 
  Block* blk, 
  Inst* origInst, 
  uint rwidth
)
{
  debug("running belady to choose a temporary register to evict");
  typedef AssignedRegList::const_iterator LI;
  AssignedReg* tmpReg = NULL;

  //look for regs with no further use from the current instruction
  //if there is no further use for this tmpReg then set it free. of
  //course there may still be a store needed if it holds a global lr
  //we do this first as a garbage collection step to free up any regs
  //that no longer should be holding a value
  int cur_inst_num = inst_order[origInst];
  for(LI i = choices.begin(); i != choices.end(); i++)
  {
    int next_use = (*i)->next_use;
    if(next_use == -1)
    {
      StoreAndResetRegSpan(*i, origInst, blk, rwidth); 
      tmpReg = tmpReg ? tmpReg : *i; //take the first available
      debug("gc dead register: %d (+ %d)", (*i)->machineReg, rwidth);
    }
    else{assert(next_use > cur_inst_num);}
  }

  //choose tmp reg with furthest next use if GC did not give any
  if(tmpReg == NULL)
  {
    debug("searching for furthest next use");
    int max_next_use = -2;
    for(LI i = choices.begin(); i != choices.end(); i++)
    {
      int next_use = (*i)->next_use;
      if(next_use > max_next_use) {tmpReg = *i; max_next_use = next_use;}
    }

    assert(tmpReg != NULL);
    StoreAndResetRegSpan(tmpReg, origInst, blk, rwidth); 
  }
  debug("finished belady");
  return tmpReg;
} 

/*
 *=====================
 * ComputeDistanceMap()
 *=====================
 * used to fill in the distance_map data structure which maps each
 * instruction and the live ranges used in that instruction to an inst
 * id of its next use or -1 if it is not used after this instruction.
 * 
 * the distances are computed for the +start_blk+ and any successor
 * blocks which are SingleSuccessorPath blocks
*/
void ComputeDistanceMap(Block* start_blk)
{
  debug("computing distance map for block: %s", bname(start_blk));
  distance_map.clear();

  //find the end block
  Block* end_blk = start_blk;
  while(SingleSuccessorPath(end_blk)) end_blk = end_blk->succ->succ;

  //begin with the end block and move backwards up the graph
  //until you get to the original start block
  std::map<LRID, int> next_uses;
  for(Block* blk = end_blk; blk != start_blk; blk = blk->pred->pred)
  {
    //record distances for the block
    AnnotateBlockWithDistances(blk, next_uses);
  }
  //and finally for the start block as well
  AnnotateBlockWithDistances(start_blk, next_uses);
}

/*
 *=============================
 * AnnotateBlockWithDistances()
 *=============================
 * handles details of filling in the distance for each live range and
 * instruction. the +next_uses+ structure is updated with local
 * information in the block.
*/
void AnnotateBlockWithDistances(Block* blk, std::map<LRID, int>& next_uses)
{
  using Mapping::SSAName2OrigLRID;

  Inst* inst;
  Block_ForAllInstsReverse(inst, blk)
  {
    Operation** op;
    Inst_ForAllOperations(op, inst)
    {
      Register* vreg;
      Operation_ForAllUses(vreg, *op)
      {
        RecordDistance(*vreg, inst, next_uses);
      }
      Operation_ForAllDefs(vreg, *op)
      {
        RecordDistance(*vreg, inst, next_uses);
      }
    }
    //update next_use map to be this inst
    Inst_ForAllOperations(op, inst)
    {
      Register* vreg;
      Operation_ForAllUses(vreg, *op)
      {
        next_uses[SSAName2OrigLRID(*vreg)] = inst_order[inst];
      }
    }
  }
}

/*
 *=================
 * RecordDistance()
 *=================
 * fills in the +distance_map+ structure based on the +next_uses+
 * variable.
*/
void RecordDistance(
  Register vreg,
  Inst* inst,
  std::map<LRID, int>& next_uses) 
{
  int next_use_from_here =  -1; //means it is not used again
  LRID orig_lrid = Mapping::SSAName2OrigLRID(vreg);
  if(next_uses.find(orig_lrid) != next_uses.end())
    next_use_from_here = next_uses[orig_lrid]; //there is a use from here
  distance_map[inst][orig_lrid] = next_use_from_here;
}


/*
 *=====================
 * BuildInstOrderingMap()
 *=====================
 * build a mapping from inst --> int that is used for local allocation
 * decisions. the mapping must have the invariant that if instA and
 * instB are in the same blocks, or are in blocks connected by a
 * SingleSuccesorPath and instA occurs textually before instB, then
 * bid(instA) < bid(instB)
*/
void BuildInstOrderingMap()
{
  int id = 0;
  Block* blk;
  ForAllBlocks_rPostorder(blk)
  {
    Inst* inst;
    Block_ForAllInsts(inst, blk)
    {
      inst_order[inst] = id++;
    }
  }
} 

bool NeedStore(AssignedReg* tmpReg, Inst* inst, Block* blk);
bool LiveOut(LRID lrid, Block* blk);
/*
 *=====================
 * StoreIfNeeded()
 *=====================
 * Inserts a store for the live range in the AssignedReg only if one
 * is necessary because it has a next use or is a global live range
 * and the AssignedReg contains a def of the live range.
 */
void StoreIfNeeded(AssignedReg* tmpReg, Inst* origInst, Block* blk)
{
  using Spill::InsertStore;
  if(NeedStore(tmpReg, origInst, blk))
  {
    LiveRange* lr = Chow::live_ranges[tmpReg->forLRID];
    InsertStore(lr, origInst, tmpReg->machineReg, 
                  Spill::REG_FP, BEFORE_INST);
  }
}

/*
 *=====================
 * NeedStore()
 *=====================
 * Predicate used to indicate when an AssingedReg contains a value
 * that needs to be stored.
 */
bool NeedStore(AssignedReg* tmpReg, Inst* inst, Block* blk)
{
  if(tmpReg->forLRID == NO_LRID)
  {
    debug("no store needed for tmpReg: %d, it is not allocated",
          tmpReg->machineReg);
    return false;
  }

  bool need_store = false;
  if(tmpReg->dirty)
  {
    if(tmpReg->next_use > inst_order[inst])
    {
      debug("store needed for replacing tmpReg: r%d, "
            "because next use is at: %d", tmpReg->machineReg,
            tmpReg->next_use);
      need_store = true;
    }
    else //may still need a store if it is a non-local variable
    {
      if(!tmpReg->local)
      {
        if(LiveOut(tmpReg->forLRID, blk))
        {
          debug("store needed for replacing tmpReg: r%d, "
                "because its lr(%d) is  non-local and live out",
                tmpReg->machineReg, tmpReg->forLRID);
          need_store = true;
        }
        else
        {
          debug("no store needed for replacing tmpReg: r%d, "
                "because its lr(%d) is  not live out",
                tmpReg->machineReg, tmpReg->forLRID);
        }
      }
      else
      {
          debug("no store needed for replacing tmpReg: r%d, "
                "because its lr(%d) is local and has no next use",
                tmpReg->machineReg, tmpReg->forLRID);
      }
    }
  }
  else 
  {
    debug("no store needed for replacing tmpReg: r%d, it is clean",
          tmpReg->machineReg);
  }


  return need_store;
}

bool LiveOut(LRID lrid, Block* blk)
{
  using Mapping::SSAName2OrigLRID;
  Liveness_Info info = SSA_live_out[bid(blk)];
  for(uint i = 0; i < info.size; i++)
  {
    if((info.names[i] < SSA_def_count))//avoid junk in live_out set
    {
      if(SSAName2OrigLRID(info.names[i]) == lrid)
      {
        return true;
      }
    }
  }
  return false;
}

/*
 *=====================
 * ResetForRegWidth()
 *=====================
 * Resets the AssignedReg values for all regs in the list starting at
 * the passed iterator and ending with the width of the live range
 * stored in the AssignedReg pointed to by the iterator.
 */
uint ResetForRegWidth(AssignedReg* tmpReg)
{
  return ResetForRegWidth(tmpReg->regpool->begin() + tmpReg->index);
}

uint ResetForRegWidth(AssignedRegList::const_iterator begin)
{
  uint rwidth = RegWidth((*begin)->forLRID);
  typedef AssignedRegList::const_iterator CI;
  for(CI runner = begin; runner != begin + rwidth; runner++)
  {
    ResetAssignedReg(*runner);
  }

  return rwidth;
}

/*
 *=======================
 * StoreAndResetRegSpan()
 *=======================
 * Works with a span of registers from the regpool. Beginning with
 * +startingReg+ the pool is examined in order up to the length
 * specified. Each tmp reg is stored if needed and then its values are
 * reset and the next tmp reg is processed skipping the width of the
 * previous tmp reg that was just reset.
 *
 */
void 
StoreAndResetRegSpan(
  AssignedReg* startingReg, 
  Inst* origInst, 
  Block* blk,
  uint width
)
{
  for(uint i = startingReg->index; i < startingReg->index + width;)
  {
    AssignedReg* tmpReg = (*startingReg->regpool)[i];
    uint tmpRegWidth = RegWidth(tmpReg);
    StoreIfNeeded(tmpReg, origInst, blk); 
    ResetForRegWidth(tmpReg);
    i += tmpRegWidth;
  }
}
/*
 *=======================
 * ResetRegSpan()
 *=======================
 * see StoreAndRestRegSpan()
 */
void ResetRegSpan(AssignedReg* startingReg, uint width)
{
  for(uint i = startingReg->index; i < startingReg->index + width;)
  {
    AssignedReg* tmpReg = (*startingReg->regpool)[i];
    uint tmpRegWidth = RegWidth(tmpReg);
    ResetForRegWidth(tmpReg);
    i += tmpRegWidth;
  }
}

/*
 *=======================
 * FindInRegPool()
 *=======================
 * Searches the +regpool+ for a register that holds the given +lrid+ 
 */
AssignedReg* FindInRegPool(LRID lrid, AssignedRegList* regpool)
{
  AssignedReg* tmpReg = NULL;

  AssignedRegList::iterator it = 
    find_if(regpool->begin(), regpool->end(), AssignedReg_LridEq(lrid));
  if(it != regpool->end()) tmpReg = *it;

  return tmpReg;
}
}//end anonymous namespace 


