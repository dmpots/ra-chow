/* chow.main.c
 *
 * entry point for chow register allocation. handle parsing command
 * line paramerters before handing the work off to other functions.
 */

/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <SSA.h>
#include "chow.h"
#include "params.h"
#include "shared_globals.h" 
#include "stats.h"
#include "rc.h"
#include "mapping.h"
#include "cleave.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
/*#### module types ####*/
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
  HELP_REGISTERCLASSES,
  HELP_LOADSTOREMOVEMENT,
  HELP_ENHANCEDCODEMOTION,
  HELP_FORCEMINIMUMREGISTERCOUNT,
  HELP_DUMPPARAMSONLY,
  HELP_REMATERIALIZE,
  HELP_TRIMUSELESS
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

/*#### module functions ####*/
/* functions to process parameters */
static int process_(Param_Details*, char*);
static const char* get_usage(Param_Help idx);
static void usage(Boolean);
static void Param_InitDefaults(void);
static void DumpParamTable(FILE* =stderr);
static void Output(void);
static void EnforceParameterConsistency();
static void CheckRegisterLimitFeasibility(Arena);
static void SetupMachineParams(Arena arena);
static void  BuildSSA(Arena);
static inline int max(int a, int b) { return a > b ? a : b;}

/*#### module variables ####*/
static const int SUCCESS = 0;
static const int ERROR = -1;
static const char EMPTY_NAME = '\0';

/* define these values to "block out" useless columns in the param
 * table. we can not use a union for the default value since only one
 * value of a union can be initialized */
static const int     I = 0;
static const float   F = 0.0;
static const Boolean B = FALSE;

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
using Params::Machine::num_registers;
using Params::Machine::enable_register_classes;
using Params::Algorithm::bb_max_insts;
using Params::Algorithm::loop_depth_weight;
using Params::Algorithm::move_loads_and_stores;
using Params::Algorithm::enhanced_code_motion;
using Params::Algorithm::rematerialize;
using Params::Algorithm::trim_useless_blocks;
using Params::Program::force_minimum_register_count;
using Params::Program::dump_params_only;
static Param_Details param_table[] = 
{
  {'b', process_, bb_max_insts,F,B, &bb_max_insts,
         INT_PARAM, HELP_BBMAXINSTS},
  {'r', process_, num_registers,F,B, &num_registers,
         INT_PARAM, HELP_NUMREGISTERS},
  {'d', process_, I,loop_depth_weight,B,&loop_depth_weight,
         FLOAT_PARAM, HELP_LOOPDEPTH},
  {'p', process_, I,F,enable_register_classes,&enable_register_classes,
         BOOL_PARAM, HELP_REGISTERCLASSES},
  {'m', process_, I,F,move_loads_and_stores,&move_loads_and_stores,
         BOOL_PARAM, HELP_LOADSTOREMOVEMENT},
  {'e', process_, I,F,enhanced_code_motion,&enhanced_code_motion,
         BOOL_PARAM, HELP_ENHANCEDCODEMOTION},
  {'f', process_,I,F,force_minimum_register_count,
                    &force_minimum_register_count, 
         BOOL_PARAM, HELP_FORCEMINIMUMREGISTERCOUNT},
  {'y', process_, I,F,dump_params_only,&dump_params_only,
         BOOL_PARAM, HELP_DUMPPARAMSONLY},
  {'z', process_, I,F,rematerialize,&rematerialize,
         BOOL_PARAM, HELP_REMATERIALIZE},
  {'t', process_, I,F,trim_useless_blocks,&trim_useless_blocks,
         BOOL_PARAM, HELP_TRIMUSELESS}
};
const unsigned int NPARAMS = (sizeof(param_table) / sizeof(param_table[0]));
const char* PARAMETER_STRING  = ":b:r:d:mpefyzt";

/*--------------------BEGIN IMPLEMENTATION---------------------*/
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

  if(Params::Program::dump_params_only)
  {
    DumpParamTable(stdout);
    exit(EXIT_SUCCESS);
  }

  //assumes file is in first argument after the params
  Stats::program_timer.Start();
  if (optind < argc)
    Block_Init(argv[optind]);
  else
    Block_Init(NULL);

  //some paramerters should implicitly set other params, and this
  //function takse care of making sure our flags are consistent
  EnforceParameterConsistency();

  //build ssa for register requirement analysis and chow allocation
  Chow::arena = Arena_Create();
  if(Params::Algorithm::bb_max_insts > 0)
  {
    Stats::Start("Cleave Blocks");
    //split basic blocks to desired size
    InitCleaver(Chow::arena, Params::Algorithm::bb_max_insts);
    CleaveBlocks();
    Stats::Stop();
  }
  BuildSSA(Chow::arena);

  //setup machine configuration
  CheckRegisterLimitFeasibility(Chow::arena);
  SetupMachineParams(Chow::arena);

  //Run the priority algorithm
  Chow::Run();

  //output results
  Output(); 

  //dump input paramerters and allocation stats
  Stats::program_timer.Stop();
  DumpParamTable();
  Stats::DumpAllocationStats();

  return EXIT_SUCCESS;
} /* main */

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
        *((Boolean*)(param->value)) = !(param->bdefault);
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
      return "[int]    maximum number of instructions in a basic block";

    case HELP_NUMREGISTERS:
      return "[int]    number of machine registers";

    case  HELP_LOADSTOREMOVEMENT:
      return 
      "         enable movement of load/store at live range boundaries";

    case  HELP_ENHANCEDCODEMOTION:
      return 
      "         connect live ranges with copy instead of load/store if\n" 
      "           possible. This flag automatically turns on -m to move\n"
      "           loads and stores";
    case HELP_LOOPDEPTH:
      return "         loop nesting depth weight";
    case HELP_REGISTERCLASSES:
      return "         disable partitioned register classes";
    case HELP_FORCEMINIMUMREGISTERCOUNT:
      return "         force allocation by increasing minimum register";
    case HELP_DUMPPARAMSONLY:
      return "         dump values of allocation params and exit";
    case HELP_REMATERIALIZE:
      return "         rematerialize values instead of spilling";
    case HELP_TRIMUSELESS:
      return "         trim useless blocks after splitting";

    default:
      return " UNKNOWN PARAMETER\n";
  }
}

/*
 *==================
 * DumpParamTable()
 *==================
 *
 ***/
void DumpParamTable(FILE* outfile)
{
  LOOPVAR i;
  Param_Details param;
  for(i = 0; i < NPARAMS; i++)
  {
    param = param_table[i];
    fprintf(outfile, "%c: ", param.name);
    switch(param.type)
    {
      case INT_PARAM:
        fprintf(outfile, "%d", *((int*)param.value));
        break;
      case FLOAT_PARAM:
        fprintf(outfile, "%f", *((float*)param.value));
        break;
      case BOOL_PARAM:
        fprintf(outfile, "%s", *((Boolean*)param.value) ? "true":"false" );
        break;
      default:
        error("unknown type");
        abort();
    }
    fprintf(outfile, "\n");
  }
}

/*
 *===========
 * EnforceParameterConsistency()
 *===========
 * some paramerters should implicitly set other params, and this
 * function takse care of making sure our flags are consistent
 * 
 * For example: enhanced motion of loads and stores should make sure
 * that the move loads and stores flag is set.
 ***/
void EnforceParameterConsistency()
{
  if(Params::Algorithm::enhanced_code_motion) 
      Params::Algorithm::move_loads_and_stores = true;
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
 *
 * if ForceMinimumRegisterCount is enabled then we will modify
 * the number of machine registers.
 ***/
void CheckRegisterLimitFeasibility(Arena arena)
{
  Block* b;
  Inst* inst;
  Operation** op;
  Register* reg;
  int cRegUses;
  int cRegDefs;
  int cRegMax = 0;

  //need to know how wide each register type is
  Mapping::CreateSSANameTypeMap(arena);
  RegisterClass::InitRegWidths();

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
          cRegUses +=
            RegisterClass::RegWidth(Mapping::SSANameDefType(*reg));
        }

        Operation_ForAllDefs(reg, *op)
        {
          cRegDefs +=
            RegisterClass::RegWidth(Mapping::SSANameDefType(*reg));
        }
        if(cRegUses > cRegMax ){cRegMax = cRegUses; }
        if(cRegDefs > cRegMax) {cRegMax = cRegDefs; }
      }
    } 
  }
  if(cRegMax > Params::Machine::num_registers)
  {
    if(Params::Program::force_minimum_register_count)
    {
      Params::Machine::num_registers = max(cRegMax,4); //4 is my minimum
      fprintf(stderr, 
      "Adjusting the number of machine registers "
      "to permit allocation: %d\n", Params::Machine::num_registers);
    }
    else
    {
      //Block_Dump(b, NULL, TRUE);
          fprintf(stderr, 
"Impossible allocation.\n\
You asked me to allocate with %d registers, but I found an operation\n\
that needs %d registers. Sorry, but this is a research compiler not \n\
a magic wand.\n", Params::Machine::num_registers, cRegMax);
          exit(EXIT_FAILURE);
    }
  }
}

void SetupMachineParams(Arena arena)
{
  RegisterClass::Init(arena, 
                      Params::Machine::num_registers,
                      Params::Machine::enable_register_classes,
                      Params::Algorithm::num_reserved_registers);
}


void BuildSSA(Arena arena)
{
  Unsigned_Int ssa_options = 0;
  ssa_options |= SSA_PRUNED;
  ssa_options |= SSA_BUILD_DEF_USE_CHAINS;
  ssa_options |= SSA_BUILD_USE_DEF_CHAINS;
  ssa_options |= SSA_CONSERVE_LIVE_IN_INFO;
  ssa_options |= SSA_CONSERVE_LIVE_OUT_INFO;
  ssa_options |= SSA_IGNORE_TAGS;
  SSA_Build(ssa_options);
}
