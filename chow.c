/*====================================================================
 * 
 *====================================================================
 * $Id: chow.c 157 2006-07-26 19:19:34Z dmp $
 * $HeadURL: http://dmpots.com/svn/research/compilers/regalloc/src/chow.c $
 ********************************************************************/

#include <Shared.h>
#include <SSA.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <list>
#include <set>
#include <vector>
#include <stack>
#include <map>
#include <algorithm>
#include <functional>
#include <utility>

#include "chow.h"
#include "live.h"
#include "live_range.h"
#include "union_find.h"
#include "reach.h"
#include "debug.h"
#include "cleave.h"
#include "ra.h" //for computing loop nesting depth
#include "rc.h" //RegisterClass definitions 
#include "assign.h" //Handles some aspects of register assignment

#define SUCCESS 0
#define ERROR -1
#define EMPTY_NAME '\0'
#define NUM_RESERVED_REGS 2

/* types */
//using namespace std;
typedef unsigned int LOOPVAR;

/* globals */
LRList live_ranges;
Arena  chow_arena;
BB_Stats bb_stats;
Unsigned_Int** mBlkIdSSAName_Color;
Variable GBL_fp_origname;
Unsigned_Int* depths; //loop nesting depth
Chow_Stats chowstats = {0};

/* locals */
static LRID* lr_name_map;
static MemoryLocation stack_pointer = 0;
static LRID fp_lrid;
static Unsigned_Int clrInitial = 0; //count of initial live ranges

//controls which registers are considered temporary registers, the
//large number is due to FRAME and JSR ops which may require a large
//number of temporary registers (should only see this when pushed to
//a very low number of registers in the allocation)
static const Register tmpRegs[] =
  {901,902,903,904,905,906,907,908,909, 910,911,912,913,914,
    /* rkf45.i has JSRl with 22 param regs */
   915,916,917,918,919,920,921,922,923,
    /* efill.i has FRAME with 26 param regs */
   924,925,926,927
   };
static const Unsigned_Int num_tmp_regs =
  sizeof(tmpRegs)/sizeof(Register);

/* local functions */
static void DumpParams(void);
static void DumpChowStats(void);
static void Output(void);
static void Param_InitDefaults(void);
static void LiveRange_BuildInitialSSA(void);
static void RunChow();
static void RenameRegisters();
static BB_Stats Compute_BBStats(Arena, Unsigned_Int);
static void CreateLiveRangeNameMap(Arena);
static void assert_same_orig_name(LRID,Variable,VectorSet,Block* b);
static void AllocChowMemory();
static Operation* get_frame_operation();
static MemoryLocation Frame_GetStackSize(Operation* frame_op);
static void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp);
static void Frame_SetRegFP(Operation* frame_op);
static Variable Frame_GetRegFP(Operation* frame_op);
static Inst* Inst_CreateLoad(Opcode_Names opcode,
                             Expr tag, 
                             Unsigned_Int alignment, 
                             Unsigned_Int offset,
                             Register base_reg,
                             Register dest_reg);
static Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val);
static void InitChow();
static UFSet* Find_Set(Variable v);
static void DumpInitialLiveRanges();
static void ConvertLiveInNamespaceSSAToLiveRange();
static void CheckRegisterLimitFeasibility(Unsigned_Int cRegMax);

/*
 * allocation parameters 
 */
Unsigned_Int pBBMaxInsts;
float mMVCost;
float mLDSave;
float mSTRSave;
float wLoopDepth;
//
static Boolean fEnableRegisterClasses;
static Unsigned_Int mRegisters;

/* used to keep track of the type of a parameter in the param table */
typedef enum
{
  INT_PARAM,
  FLOAT_PARAM,
  BOOL_PARAM
} Param_Type;

/* index for help messages */
typedef enum
{
  NO_HELP,
  HELP_BBMAXINSTS,
  HELP_NUMREGISTERS,
  HELP_MVCOST,
  HELP_LDSAVE,
  HELP_STRSAVE,
  HELP_LOOPDEPTH,
  HELP_REGISTERCLASSES
} Param_Help;


/* info used to process parameters */
typedef struct param_details
{
  char name;
  int (*func)(struct param_details*, char*);
  //union {
    Int      idefault;
    float    fdefault;
    Boolean  bdefault;
  //};
  void* value;
  Param_Type type;
  Param_Help usage;
} Param_Details;

/* define these values to "block out" useless columns in the param
 * table. we can not use a union for the default value since only one
 * value of a union can be initialized */
static const int     I = 0;
static const float   F = 0.0;
static const Boolean B = FALSE;

/* functions to process parameters */
int process_(Param_Details*, char*);
static const char* get_usage(Param_Help idx);
static void usage(Boolean);

/* table of all parameters we accept. the idea for using a table 
 * like this was taken from the code for the zfs file system in
 * the file zpool_main.c
 *
 * each entry has the paramerter character, a function for
 * processing that parameter, a default value, and tag used for
 * listing help for that parameter. the default value is a union
 * to allow for different types of parameters.
 *
 */
static Param_Details param_table[] = 
{
  {'b', process_, 0,F,B, &pBBMaxInsts, INT_PARAM, HELP_BBMAXINSTS},
  {'r', process_, 32,F,B, &mRegisters, INT_PARAM, HELP_NUMREGISTERS},
  {'m', process_, I,1.0,B, &mMVCost, FLOAT_PARAM, HELP_MVCOST},
  {'l', process_, I,1.0,B, &mLDSave, FLOAT_PARAM, HELP_LDSAVE},
  {'s', process_, I,1.0,B,&mSTRSave, FLOAT_PARAM, HELP_STRSAVE},
  {'d', process_, 0,10.0,B,&wLoopDepth, FLOAT_PARAM, HELP_LOOPDEPTH},
  {'p', process_, I,F,FALSE,&fEnableRegisterClasses, BOOL_PARAM, 
                                                  HELP_REGISTERCLASSES} 
};
#define NPARAMS (sizeof(param_table) / sizeof(param_table[0]))
#define PARAMETER_STRING ":b:r:m:l:s:d:p"

/*
 *===========
 * main()
 *===========
 *
 ***/
int main(Int argc, Char **argv)
{
  LOOPVAR i;
  int c;

  /* process arguments */ 
  Param_InitDefaults();
  while((c = getopt(argc, argv, PARAMETER_STRING)) != -1)
  {
    switch(c)
    {
      case ':' :
        fprintf(stderr, "missing argument for '%c' option\n", optopt);
        usage(FALSE);
        break;

      case '?' :
        fprintf(stderr, "invalid option '%c' option\n", optopt);
        usage(FALSE);
        break;

      default :
        for(i = 0; i < NPARAMS; i++)
        {
          if(c == param_table[i].name)
          {
            char* val = optarg;
            if(param_table[i].type == BOOL_PARAM)
            {
              val = "TRUE"; //the presence of the arg sets it to true
            }
            param_table[i].func(&param_table[i], val);
            break;
          } 
        }
        
        //make sure we recognize the parameter
        if(i == NPARAMS)
        {
          fprintf(stderr,"BAD PARAMETER: %c\n",c);
          abort();
        }
    }

  }


  //assumes file is in first argument after the params
  if (optind < argc)
    Block_Init(argv[optind]);
  else
    Block_Init(NULL);

  //make an initial check to ensure not too many registers are used
  CheckRegisterLimitFeasibility(mRegisters);

  //areana for all chow memory allocations
  chow_arena = Arena_Create(); 

  //split basic blocks to desired size
  InitCleaver(chow_arena, pBBMaxInsts);
  CleaveBlocks();
  
  //initialize the register class data structures 
  InitRegisterClasses(chow_arena, mRegisters, fEnableRegisterClasses,
                      NUM_RESERVED_REGS);

  //compute initial live ranges
  LiveRange_BuildInitialSSA();
  //DumpInitialLiveRanges();

  //compute loop nesting depth needed for computing priorities
  find_nesting_depths(chow_arena);
  
  //run the priority algorithm
  RunChow();
  RenameRegisters();
  
  //Dump(); 
  Output(); 
  DumpParams();
  DumpChowStats();
  return EXIT_SUCCESS;
} /* main */


/*
 *=======================
 * RunChow()
 *=======================
 *
 * Does the actual register allocation
 *
 ***/
void RunChow()
{
  LiveRange* lr;
  LRSet constr_lrs;
  LRSet unconstr_lrs;


  //allocate any memory needed by the chow allocator
  InitChow();

      
  //separate unconstrained live ranges
  LRList_ForAllCandidates(&live_ranges, lr)
  {
    //lr = live_ranges[i];
    if(LiveRange_Constrained(lr))
    {
      LRSet_Add(&constr_lrs, lr);
      debug("Constrained LR:    %d", lr->id);
    }
    else
    {
      LRSet_Add(&unconstr_lrs, lr);
      debug("UN-Constrained LR: %d", lr->id);
    }
  }


  //assign registers to constrained live ranges
  lr = NULL;
  while(!(constr_lrs.empty()))
  {
    //steps 2: (a) - (c)
    lr = ComputePriorityAndChooseTop(&constr_lrs);
    if(lr == NULL)
    {
      debug("No more constrained lrs can be assigned");
      break;
    }
    LiveRange_AssignColor(lr);
    debug("LR: %d is top priority, given color: %d", lr->id, lr->color);
    LiveRange_SplitNeighbors(lr, &constr_lrs, &unconstr_lrs);
  }

  //assign registers to unconstrained live ranges
  debug("assigning unconstrained live ranges colors");
  //while(!LRList_Empty(unconstr_lrs))
  //some may no longe be candidates now
  LRSet_ForAllCandidates(&unconstr_lrs, lr)
  {
    //lr = LRList_Pop(&unconstr_lrs);
    debug("choose color for unconstrained LR: %d", lr->id);
    LiveRange_AssignColor(lr);
    debug("LR: %d is given color:%d", lr->id, lr->color);
  }
  //record some statistics about the allocation
  chowstats.clrFinal = live_ranges.size();
}

void AllocChowMemory()
{
  //allocate a mapping of block x SSA_name -> register
  mBlkIdSSAName_Color = (Unsigned_Int**)
    Arena_GetMemClear(chow_arena, 
                      sizeof(Unsigned_Int*) * (1+block_count));
  LOOPVAR i;
  for(i = 0; i < block_count+1; i++)
  {
    mBlkIdSSAName_Color[i] = (Unsigned_Int*)
      Arena_GetMemClear(chow_arena, 
                        sizeof(Unsigned_Int) * liverange_count );
      LOOPVAR j;
      for(j = 0; j < liverange_count; j++)
        mBlkIdSSAName_Color[i][j] = NO_COLOR;
  }

}

/*
 *=============================
 * InitChow()
 *=============================
 * Initialize global variables used during allocation
 */
void InitChow()
{
  AllocChowMemory();

  //the register that holds the frame pointer is not a candidate for
  //allocation since it resides in a special reserved register. remove
  //this from the interference graph and remember which lrid holds the
  //frame pointer
  Operation* frame_op = get_frame_operation();
  fp_lrid = SSAName2LRID(frame_op->arguments[frame_op->referenced]);
  LiveRange_MarkNonCandidateAndDelete(live_ranges[fp_lrid]);
  //LRList_Remove(&live_ranges, live_ranges[fp_lrid]);


  //initialize the stack pointer so that we have a correct value when
  //we need to insert loads and stores
  stack_pointer = Frame_GetStackSize(frame_op);
  GBL_fp_origname = Frame_GetRegFP(frame_op);
}

/*
 *=============================
 * LiveRange_BuildInitialSSA()
 *=============================
 * Each live range starts out where no definition reaches a use
 * outside the live range. This means that no stores will be necessary
 * at first. When we allocate a register it will stay across the
 * entire live range.
 *
 * If a successor block needs a load then we must generate a store
 * 
 * If the def reaches a use outside the live range we must generate a
 * store.
 * 
 * 
 */
void LiveRange_BuildInitialSSA()
{
  //build ssa
  Unsigned_Int ssa_options = 0;
  ssa_options |= SSA_PRUNED;
  ssa_options |= SSA_BUILD_DEF_USE_CHAINS;
  ssa_options |= SSA_BUILD_USE_DEF_CHAINS;
  ssa_options |= SSA_CONSERVE_LIVE_IN_INFO;
  ssa_options |= SSA_CONSERVE_LIVE_OUT_INFO;
  ssa_options |= SSA_IGNORE_TAGS;
  SSA_Build(ssa_options);
  //Dump();

  Arena uf_arena = Arena_Create();
  UFSets_Init(uf_arena, SSA_def_count);

  //visit each phi node and union the parameters and destination
  //register together since they should all be part of the same live
  //range
  Block* b;
  Phi_Node* phi;
  UFSet* set;
  Unsigned_Int* v;
  ForAllBlocks(b)
  {
    Block_ForAllPhiNodes(phi, b)
    {
      debug("process phi: %d at %s (%d)", phi->new_name, bname(b), id(b));
      set = Find_Set(phi->new_name);
      Phi_Node_ForAllParms(v, phi)
      {
        if(*v != 0)
          set = UFSet_Union(set, Find_Set(*v));
      }
    }
  }
  clrInitial = uf_set_count - 1; //no lr for SSA name 0
  debug("SSA NAMES: %d", SSA_def_count);
  debug("UNIQUE LRs: %d", clrInitial);
  chowstats.clrInitial = clrInitial;

  //create a mapping from ssa names to live range ids
  CreateLiveRangeNameMap(uf_arena);
  ConvertLiveInNamespaceSSAToLiveRange();
  if(fEnableRegisterClasses) //classes are by type
  {
    RegisterClass_CreateLiveRangeTypeMap(uf_arena,
                                       clrInitial,
                                       lr_name_map);
  }

  //now that we know how many live ranges we start with allocate them
  bb_stats = Compute_BBStats(uf_arena, SSA_def_count);
  LiveRange_AllocLiveRanges(chow_arena, live_ranges, clrInitial);

  //build the interference graph
  //find all live ranges that are referenced or live out in this
  //block. those are the live ranges that need to include this block.
  //each of those live ranges will interfere with every other live
  //range in the block. walk through the graph and examine each block
  //to build the live ranges
  LiveRange* lr;
  LRID lrid;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  LOOPVAR j;
  VectorSet lrset = VectorSet_Create(uf_arena, clrInitial);
  ForAllBlocks(b)
  {

    //we need to acccount for any variable that is referenced in this
    //block as it may not be in the live_out set, but should still be
    //included in the live range if it is referenced in this block. A
    //name may be in a live range under more than one original name.
    //for example when a name is defined in two branches of an if
    //statement it will be added to the live range by that definition,
    //but will also be live out in those blocks under a different name
    //(the name defined by the phi-node for those definitions). as
    //long as we get the last definition in the block we should be ok
    VectorSet_Clear(lrset);
    Block_ForAllInstsReverse(inst, b)
    {
      //go in reverse because we want the last def that we see to be
      //the orig_name for the live range, this must be so because we
      //use that name in the use-def chains to decide where to put a
      //store for the defs of a live range
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllDefs(reg, *op)
        {
          debug("DEF: %d", *reg);
          lrid = SSAName2LRID(*reg);
          //better not have two definitions in the same block for the
          //same live range
          assert_same_orig_name(lrid, *reg, lrset, b); 
          AddLiveUnitOnce(lrid, b, lrset, *reg);

          //set the type of the live range according to the type
          //defined by this operation
          live_ranges[lrid]->type = Operation_Def_Type(*op, *reg);
        }

        Operation_ForAllUses(reg, *op)
        {
          debug("USE: %d", *reg);
          lrid = SSAName2LRID(*reg);
          AddLiveUnitOnce(lrid, b, lrset, *reg);
        }
      } 
    }

    //Now add in the live_out set to the variables that include this
    //block in their live range
    Liveness_Info info;
    info = SSA_live_out[id(b)];
    for(j = 0; j < info.size; j++)
    {
      debug("LIVE: %d is LRID: %d",info.names[j], SSAName2LRID(info.names[j]));
      //add block to each live range
      lrid = SSAName2LRID(info.names[j]);
      //VectorSet_Insert(lrset, lrid);
      AddLiveUnitOnce(lrid, b, lrset, info.names[j]);
    }

    //now that we have the full set of lrids that need to include this
    //block we can add the live units to the live ranges and update
    //the interference graph
    Unsigned_Int v, i;
    debug("LIVE SIZE: %d\n", VectorSet_Size(lrset));
    VectorSet_ForAll(v, lrset)
    {
      lr = live_ranges[v];

      //update the interference lists
      VectorSet_ForAll(i, lrset)
      {
        if(v == i) continue; //skip yourself
        //add interference if in the same class
        LiveRange* lrT = live_ranges[i];
        if(LiveRange_RegisterClass(lr) == LiveRange_RegisterClass(lrT))
        {
          debug("%d conflicts with %d", v, i);
          LiveRange_AddInterference(lr, lrT);
        }
      }
    }
  }


  //compute where the loads and stores need to go in the live range
  LRList_ForAll(&live_ranges, lr)
    LiveRange_MarkLoadsAndStores(lr);
  LiveRange_DDumpAll(&live_ranges);
}

//make sure that if we have already added a live unit for this lrid
//that the original name matches this name. this is important for the
//rewriting step, but this check does not need to be made when adding
//from the live_out set since those names may be different but it is
//ok because they occur in a different block
void assert_same_orig_name(LRID lrid, Variable v, VectorSet set,
Block* b)
{
  if(VectorSet_Member(set,lrid))
  {
    LiveUnit* unit = LiveRange_LiveUnitForBlock(live_ranges[lrid],b);
    debug("already present: %d, orig_name: %d new_orig: %d  block: %s (%d)", 
                            lrid, unit->orig_name, v, bname(b), id(b));
    assert(unit->orig_name == v);
  }
}

/*
 *========================
 * CreateLiveRangeNameMap
 *========================
 * Makes a mapping from SSA name to live range id and stores the
 * result in the global variable lr_name_map
 ***/
void CreateLiveRangeNameMap(Arena arena)
{
  lr_name_map = (LRID*)
        Arena_GetMemClear(arena,sizeof(LRID) * (SSA_def_count));
  Unsigned_Int idcnt = 0; 
  Unsigned_Int setid; //a setid may be 0
  LOOPVAR i;

  //initialize to known value
  for(i = 0; i < SSA_def_count; i++)
    lr_name_map[i] = NO_LRID;
  
  for(i = 1; i < SSA_def_count; i++)
  {
    setid = Find_Set(i)->id;
    //debug("name: %d setid: %d lrid: %d", i, setid, lr_name_map[setid]);
    if(lr_name_map[setid] != NO_LRID)
    {
      lr_name_map[i] = lr_name_map[setid]; //already seen this lr
    } 
    else
    {
      //assign next available lr id
      lr_name_map[i] = lr_name_map[setid] = idcnt++;
    }

    debug("SSAName: %4d ==> LRID: %4d", i, lr_name_map[i]);
  }
  assert(idcnt == (clrInitial)); //we start lrids at 0
}

/*
 *========================================
 * ConvertLiveInNamespaceSSAToLiveRange()
 *========================================
 * Changes live range name space to use live range ids rather than the
 * ssa namespace.
 *
 * NOTE: Make sure you call this after building initial live ranges
 * because the live units need to know which SSA name they contain and
 * that information is taken from the ssa liveness info.
 ***/
void ConvertLiveInNamespaceSSAToLiveRange() 
{

  Liveness_Info info;
  Block* blk;
  LOOPVAR j;
  LRID lrid;
  ForAllBlocks(blk)
  {
    //TODO: do we need to convert live out too?
    info = SSA_live_in[id(blk)];
    for(j = 0; j < info.size; j++)
    {
      Variable vLive = info.names[j];
      if(!(vLive < SSA_def_count))
      {
        error("invalid live in name %d", vLive);
        continue;
      }
      debug("Converting LIVE: %d to LRID: %d",vLive,
             SSAName2LRID(vLive));
      lrid = SSAName2LRID(vLive);
      info.names[j] = lrid;
    }
  }
}

void DumpInitialLiveRanges()
{
  LOOPVAR i;
  for(i = 0; i < SSA_def_count; i++)
  {
    debug("SSA_map: %d ==> %d", i, SSA_name_map[i]);
    SSA_name_map[i] = SSAName2LRID(i);
  }
  SSA_Restore();   
  Output();
  exit(0);
}


/*
 *===================
 * RenameRegisters()
 *===================
 * Renames the variables in the code to use the registers assigned by
 * coloring
 ***/
void RenameRegisters()
{
  debug("allocation complete. renaming registers...");

  //stack pointer is the initial size of the stack frame
  debug("STACK: %d", stack_pointer);

  Block* b;
  Inst* inst;
  Operation** op;
  Unsigned_Int* reg;
  LRID lrid;
  std::vector<Register> instUses;
  std::vector<Register> instDefs;

  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {
      /* collect a list of uses and defs used in this regist that is
       * used in algorithms when deciding which registers can be
       * evicted for temporary uses */
      instUses.clear();
      instDefs.clear();
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          LRID lrid = SSAName2LRID(*reg);
          instUses.push_back(lrid);
        }

        Operation_ForAllDefs(reg, *op)
        {
          LRID lrid = SSAName2LRID(*reg);
          instDefs.push_back(lrid);
        }
      } 

      /* rename uses and then defs */
      //we need to keep the original inst separate. when we call
      //ensure_reg_assignment it may be the case that we insert some
      //stores after this instruction. we don't want to process those
      //store instructions in the register allocator so we have to
      //update the value of the inst pointer. thus we keep origInst
      //and updatedInst separate.
      Inst* origInst = inst;
      Inst** updatedInst = &inst;
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          *reg = GetMachineRegAssignment(b, lrid);
          //make sure the live range is in a register
          ensure_reg_assignment(reg, lrid, b, origInst, updatedInst,
                                *op, FOR_USE,
                                instUses, instDefs);
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          *reg = GetMachineRegAssignment(b, lrid); 
          //make sure the live range is in a register
          ensure_reg_assignment(reg, lrid, b, origInst, updatedInst,
                               *op, FOR_DEF,
                                instUses, instDefs);
        }

      } 
    }
    //make available the tmp regs used in this instruction
    //pass in a pointer to the last instruction in the block so
    //that we can insert loads for evicted registers
    reset_free_tmp_regs(b->inst->prev_inst); 
  }

  //finally rewrite the frame statement to have the first register be
  //the frame pointer and to adjust the stack size
  Operation* frame_op = get_frame_operation();
  assert(frame_op->opcode == FRAME);
  Frame_SetStackSize(frame_op, stack_pointer);
  Frame_SetRegFP(frame_op);
}



/*
 *============================
 * Frame_GetStackSize()
 *=============================
 * Gets the stack size on the frame operation
 **/
MemoryLocation Frame_GetStackSize(Operation* frame_op)
{
  return Expr_Get_Integer((frame_op)->arguments[0]);
}

/*
 *============================
 * Frame_SetStackSize()
 *=============================
 * Sets the stack size on the frame operation
 **/
void Frame_SetStackSize(Operation* frame_op, MemoryLocation sp)
{
  frame_op->arguments[0] = Expr_Install_Int(sp);
}

/*
 *============================
 * Frame_SetRegFP()
 *=============================
 * Sets the frame pointer register to use the global constant
 **/
void Frame_SetRegFP(Operation* frame_op)
{
  frame_op->arguments[(frame_op)->referenced] = REG_FP;
}

/*
 *============================
 * Frame_GetRegFP()
 *=============================
 * Sets the frame pointer register to use the global constant
 **/
Variable Frame_GetRegFP(Operation* frame_op)
{
  return frame_op->arguments[(frame_op)->referenced];
}


/*
 *==========================
 * get_frame_operation()
 *==========================
 * Gets the operation corresponding to the FRAME statement in the iloc
 * code
 **/
Operation* get_frame_operation()
{
  //cache the frame operation so we only have to do the lookup once
  static Operation* frame_op = NULL;
  if(frame_op) return frame_op;

  Block* b;
  Inst* inst;
  Operation** op;
  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {

      Inst_ForAllOperations(op, inst)
      {
        //grab some info from the frame instruction
        if((*op)->opcode == FRAME)
        {
          frame_op = *op;
          return frame_op;
        }
      }
    }
  }

  assert(FALSE); //should not get here
  return NULL;
}

/*
 *==========================
 * GetMachineRegAssignment()
 *==========================
 * Gets the machine register assignment for a lrid in a given block
 **/
Register GetMachineRegAssignment(Block* b, LRID lrid)
{

  if(lrid == fp_lrid)
    return REG_FP;

   /* return REG_UNALLOCATED; spill everything */

  Register color = mBlkIdSSAName_Color[id(b)][lrid];
  if(color == NO_COLOR)
    return REG_UNALLOCATED;

  RegisterClass rc = LiveRange_RegisterClass(live_ranges[lrid]);
  return RegisterClass_MachineRegForColor(rc, color);
}
/*
 *===================
 * Insert_Load()
 *===================
 * Inserts a load instruction for the given live range before the
 * passed instruction.
 */
void Insert_Load(LRID lrid, Inst* before_inst, Register dest, 
                                               Register base)
{
  LiveRange* lr = live_ranges[lrid];
  Expr tag = LiveRange_GetTag(lr);

  Opcode_Names opcode = LiveRange_LoadOpcode(lr);
  Unsigned_Int alignment = LiveRange_GetAlignment(lr);
  Unsigned_Int offset = LiveRange_MemLocation(lr);
  //assert(offset != MEM_UNASSIGNED); //can happen with use b4 def
  debug("Inserting load for LR: %d, to reg: %d, from offset: %d,"
         "base: %d", lrid, dest, offset, base);

  Inst* ld_inst = 
    Inst_CreateLoad(opcode, tag, alignment, offset, base, dest);

  //finally insert the new instruction
  Block_Insert_Instruction(ld_inst, before_inst);

}

/*
 *===================
 * Inst_CreateLoad()
 *===================
 * Creates a new load Inst based on the passed parameters
 * load instruction format:
 *  iLDor  @ref align offset base => reg
 */
static const int LD_OPSIZE = 7;
Inst* Inst_CreateLoad(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Unsigned_Int offset,
                      Register base_reg,
                      Register dest_reg)
{
  //allocate a new instruction
  Inst* ld_inst = (Inst*)Inst_Allocate(chow_arena, 1);
  Operation* ld_op = (Operation*)Operation_Allocate(chow_arena, 
                                                    LD_OPSIZE);
  ld_inst->operations[0] = ld_op;
  ld_inst->operations[1] = NULL;

  //fill in struct
  ld_op->opcode  = opcode;
  ld_op->comment = 0;
  ld_op->source_line_ref = Expr_Install_String("0");
  ld_op->constants = 3;
  ld_op->referenced = 4;
  ld_op->defined = 5;
  ld_op->critical = TRUE;

  //fill in arguments array
  ld_op->arguments[0] = tag;
  ld_op->arguments[1] = Expr_Install_Int(alignment);
  ld_op->arguments[2] = Expr_Install_Int(offset);
  ld_op->arguments[3] = base_reg;
  ld_op->arguments[4] = dest_reg;
  ld_op->arguments[5] = 0;
  ld_op->arguments[6] = 0;

  return ld_inst;
}


/*
 *===================
 * Insert_Store()
 *===================
 * Inserts a store instruction for the given live range before the
 * passed instruction.
 *
 * returns the instruction inserted
 */
Inst* Insert_Store(LRID lrid, Inst* around_inst, Register src,
                   Register base, InstInsertLocation loc)
{
  LiveRange* lr = live_ranges[lrid];
  Expr tag = LiveRange_GetTag(lr);

  MemoryLocation offset = LiveRange_MemLocation(lr);
  assert(offset != MEM_UNASSIGNED);
  debug("Inserting store for LR: %d, from reg: %d to offset: %d",
         lrid, src, offset);

  Opcode_Names opcode = LiveRange_StoreOpcode(lr);
  Unsigned_Int alignment = LiveRange_GetAlignment(lr);
  Inst* st_inst = 
    Inst_CreateStore(opcode, tag, alignment, offset, base, src);

  //finally insert the new instruction
  if(loc == AFTER_INST)
    Block_Insert_Instruction(st_inst, around_inst->next_inst);
  else
    Block_Insert_Instruction(st_inst, around_inst);

  return st_inst;
}

/*
 *===================
 * Inst_CreateStore()
 *===================
 * Creates a new store Inst based on the passed parameters
 * store instruction format:
 *  iSSTor @ref align offset base val
 */
static const int ST_OPSIZE = 7;
Inst* Inst_CreateStore(Opcode_Names opcode,
                      Expr tag,
                      Unsigned_Int alignment, 
                      Unsigned_Int offset,
                      Register base_reg,
                      Register val)
{
  //allocate a new instruction
  Inst* st_inst = (Inst*)Inst_Allocate(chow_arena, 1);
  Operation* st_op = (Operation*)Operation_Allocate(chow_arena, 
                                                    ST_OPSIZE);
  st_inst->operations[0] = st_op;
  st_inst->operations[1] = NULL;

  //fill in struct
  st_op->opcode  = opcode;
  st_op->comment = 0;
  st_op->source_line_ref = Expr_Install_String("0");
  st_op->constants = 3;
  st_op->referenced = 5;
  st_op->defined = 5;
  st_op->critical = TRUE;

  //fill in arguments array
  st_op->arguments[0] = tag;
  st_op->arguments[1] = Expr_Install_Int(alignment);
  st_op->arguments[2] = Expr_Install_Int(offset);
  st_op->arguments[3] = base_reg;
  st_op->arguments[4] = val;
  st_op->arguments[5] = 0;
  st_op->arguments[6] = 0;

  return st_inst;
}

/*
 *=====================
 * ReserveStackSpace()
 *=====================
 * Reserves space on the call stack and returns a pointer to the
 * beginning offset of the reserved space
 **/
MemoryLocation ReserveStackSpace(Unsigned_Int size)
{
  MemoryLocation sp = stack_pointer;
  stack_pointer += size;
  return sp;
}


/*
 *===================
 * Find_Set()
 *===================
 * Returns pointer to the UFSet structure for the given variable
 **/
UFSet* Find_Set(Variable v)
{
  return UFSet_Find(uf_sets[v]);
}

/*
 *===================
 * SSAName2LRID()
 *===================
 * Maps a variable to the initial live range id to which that variable
 * belongs. Once the splitting process starts this mapping may not be
 * valid and should not be used.
 **/
LRID SSAName2LRID(Variable v)
{
  assert(v < SSA_def_count);
  assert((lr_name_map[v] != NO_LRID) || v == 0);
  return lr_name_map[v];
}

/*
 *===================
 * Compute_BBStats()
 *===================
 * Gathers statistics about variables on a per-live range basis.
 * Collects:
 *  1) number of uses in the block
 *  2) number of defs in the block
 *  3) if the first occurance is a def
 ***/
BB_Stats Compute_BBStats(Arena arena, Unsigned_Int variable_count)
{
  Block* b;
  Inst* inst;
  Operation** op;
  Variable* reg;
  BB_Stats stats;
  BB_Stat* bstats;


  //allocate space to hold the stats
  stats = (BB_Stat**)
    Arena_GetMemClear(arena, sizeof(BB_Stat*) * (block_count + 1));
  ForAllBlocks(b)
  {
    stats[id(b)] = (BB_Stat*) 
      Arena_GetMemClear(arena, sizeof(BB_Stat) * variable_count);
  }

  LRID lrid;
  ForAllBlocks(b)
  {
    bstats = stats[id(b)];
    Block_ForAllInsts(inst, b)
    {
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].uses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          lrid = SSAName2LRID(*reg);
          bstats[lrid].defs++;
          if(bstats[lrid].uses == 0)
            bstats[lrid].start_with_def = TRUE;
        }
      } 
    }
  }

  return stats;
}

/*
 *======================
 * Param_InitDefaults()
 *======================
 *
 ***/
void Param_InitDefaults()
{
  LOOPVAR i;
  Param_Details param;

  for(i = 0; i < NPARAMS; i++)
  {
    param = param_table[i];
    if(param.name != EMPTY_NAME)
    {
      param.func(&param, NULL);
    }
  }
}


/*
 *========
 * Output
 *========
 *
 ***/
void Output()
{
  Block_Put_All(stdout);
}




/*
 *================
 * process_
 *================
 * default function for processing parameters. will set the default
 * value and parse an arg to the correct type if given.
 *
 * custom parameter processing functions can be defined if needed and
 * used by setting the appropriate entry in the param table.
 *
 ***/
int process_(Param_Details* param, char* arg)
{
  if(arg == NULL)
  {
    switch(param->type)
    {
      case INT_PARAM:
        *((int*)(param->value))   = (int)param->idefault;
        break;
      case FLOAT_PARAM:
        *((float*)(param->value)) = param->fdefault;
        break;
      case BOOL_PARAM:
        *((Boolean*)(param->value)) = param->bdefault;
        break;
      default:
        error("unknown type");
        abort();
    }
  }
  else
  {
    switch(param->type)
    {
      case INT_PARAM:
        *((int*)(param->value)) = atoi(arg);
        break;
      case FLOAT_PARAM:
        *((float*)(param->value)) = atof(arg);
        break;
      case BOOL_PARAM:
        *((Boolean*)(param->value)) = TRUE;
        break;
      default:
        error("unknown type");
        abort();
    }
  }

  return SUCCESS;
}


/*
 *===========
 * usage()
 *===========
 *
 ***/
void usage(Boolean requested)
{
  LOOPVAR i;
  FILE* fp = stdout;

  fprintf(fp, "usage: chow <params> [file]\n");
  fprintf(fp, "'file' is the iloc file (stdin if not given)\n");
  fprintf(fp, "'params' are one of the following \n\n");
  for(i = 0; i < NPARAMS; i++)
  {
    if(param_table[i].name == EMPTY_NAME)
      fprintf(fp, "\n");
    else
      fprintf(fp, "-%c%s\n", param_table[i].name,
                           get_usage(param_table[i].usage));
  }
  fprintf(fp, "\n");
  exit(requested ? SUCCESS : ERROR);
} 

/*
 *============
 * get_usage()
 *============
 *
 ***/
static const char* get_usage(Param_Help idx)
{
  //switch eventually
  switch(idx)
  {
    case HELP_BBMAXINSTS:
      return "[int]\tmaximum number of instructions in a basic block";

    case HELP_NUMREGISTERS:
      return "[int]\tnumber of machine registers";

    default:
      return " UNKNOWN PARAMETER\n";
  }
}

/*
 *===========
 * DumpParams()
 *===========
 *
 ***/
void DumpParams()
{
  LOOPVAR i;
  Param_Details param;
  for(i = 0; i < NPARAMS; i++)
  {
    param = param_table[i];
    fprintf(stderr, "%c ==> ", param.name);
    switch(param.type)
    {
      case INT_PARAM:
        fprintf(stderr, "%d", *((int*)param.value));
        break;
      case FLOAT_PARAM:
        fprintf(stderr, "%f", *((float*)param.value));
        break;
      case BOOL_PARAM:
        fprintf(stderr, "%s", *((Boolean*)param.value) ? "TRUE":"FALSE" );
        break;
      default:
        error("unknown type");
        abort();
    }
    fprintf(stderr, "\n");
  }
}


/*
 *================
 * DumpChowStats()
 *================
 *
 ***/
static void DumpChowStats()
{
  fprintf(stderr, "***** ALLOCATION STATISTICS *****\n");
  fprintf(stderr, " Inital  LiveRange Count: %d\n",
                                           chowstats.clrInitial);
  fprintf(stderr, " Final   LiveRange Count: %d\n",
                                           chowstats.clrFinal);
  fprintf(stderr, " Colored LiveRange Count: %d\n",
                                           chowstats.clrColored+1);
  fprintf(stderr, " Spilled LiveRange Count: %d\n", 
                                           chowstats.cSpills-1);
  fprintf(stderr, " Number of Splits: %d\n", chowstats.cSplits);
  fprintf(stderr, "***** ALLOCATION STATISTICS *****\n");
  //note: +/- 1 colored/spill count is for frame pointer live range
}

/*
 *====================================
 * CheckRegisterLimitFeasibility()
 *====================================
 * This function makes sure that we can allocate the code given the
 * number of machine registers. We walk the code and look for the
 * maximum number of registers used/defined in any instruction and
 * make sure that number is fewer than the number of machine registers
 * we are given.
 ***/
static void CheckRegisterLimitFeasibility(Unsigned_Int cRegMax)
{
  Block* b;
  Inst* inst;
  Operation** op;
  Register* reg;
  Unsigned_Int cRegUses;
  Unsigned_Int cRegDefs;

  ForAllBlocks(b)
  {
    Block_ForAllInsts(inst, b)
    {
      cRegUses = 0;
      cRegDefs = 0;
      Inst_ForAllOperations(op, inst)
      {
        Operation_ForAllUses(reg, *op)
        {
          cRegUses++;
        }

        Operation_ForAllDefs(reg, *op)
        {
          cRegDefs++;
        }
        if(cRegUses > cRegMax || cRegDefs > cRegMax)
        {
          Block_Dump(b, NULL, TRUE);
          fprintf(stderr, 
"Impossible allocation.\n\
You asked me to allocate with %d registers, but I found an operation\n\
with %d registers used and %d registers defined: %s. Sorry, but this\n\
is a research compiler not a magic wand. The offending block is \n\
printed above.\n", cRegMax,cRegUses,cRegDefs, oname(*op));
          exit(EXIT_FAILURE);
        }
      }
    } 


  }
}

/*
 *===========
 * Function()
 *===========
 *
 ***/

