
/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include "chow.h"
#include "chow_params.h"
#include "cleave.h"
#include "debug.h"
#include "ra.h" //for computing loop nesting depth
#include "rc.h" //RegisterClass definitions 
#include "dot_dump.h"

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
  HELP_ENHANCEDCODEMOTION
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
static void DumpParams(void);
static void DumpChowStats(void);
static void Output(void);
static void DumpInitialLiveRanges();
static void DotDumpLR(LRID lrid, const char* tag);
static void DotDumpFinalLRs(LRID lrid);
static void EnforceParameterConsistency();

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
static Param_Details param_table[] = 
{
  {'b', process_, 0,F,B, &PARAM_BBMaxInsts, INT_PARAM,
                                                  HELP_BBMAXINSTS},
  {'r', process_, 32,F,B, &PARAM_NumMachineRegs, INT_PARAM,
                                                  HELP_NUMREGISTERS},
  {'d', process_, 0,10.0,B,&PARAM_LoopDepthWeight, FLOAT_PARAM, 
                                                  HELP_LOOPDEPTH},
  {'p', process_, I,F,FALSE,&PARAM_EnableRegisterClasses, BOOL_PARAM, 
                                                  HELP_REGISTERCLASSES},
  {'m', process_, I,F,FALSE,&PARAM_MoveLoadsAndStores, BOOL_PARAM, 
                                                  HELP_LOADSTOREMOVEMENT},
  {'e', process_, I,F,FALSE,&PARAM_EnhancedCodeMotion, BOOL_PARAM, 
                                                  HELP_ENHANCEDCODEMOTION}
//  {'m', process_, I,1.0,B, &mMVCost, FLOAT_PARAM, HELP_MVCOST},
//  {'l', process_, I,1.0,B, &mLDSave, FLOAT_PARAM, HELP_LDSAVE},
//  {'s', process_, I,1.0,B,&mSTRSave, FLOAT_PARAM, HELP_STRSAVE},
};
#define NPARAMS (sizeof(param_table) / sizeof(param_table[0]))
#define PARAMETER_STRING ":b:r:d:mpe"

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


  //assumes file is in first argument after the params
  if (optind < argc)
    Block_Init(argv[optind]);
  else
    Block_Init(NULL);

  //some paramerters should implicitly set other params, and this
  //function takse care of making sure our flags are consistent
  EnforceParameterConsistency();

  //make an initial check to ensure not too many registers are used
  CheckRegisterLimitFeasibility(PARAM_NumMachineRegs);

  //areana for all chow memory allocations
  chow_arena = Arena_Create(); 

  //split basic blocks to desired size
  InitCleaver(chow_arena, PARAM_BBMaxInsts);
  CleaveBlocks();
  
  //initialize the register class data structures 
  InitRegisterClasses(chow_arena, 
                      PARAM_NumMachineRegs,
                      PARAM_EnableRegisterClasses,
                      PARAM_NumReservedRegs);

  //compute initial live ranges
  LiveRange_BuildInitialSSA();
  //DotDumpLR(DEBUG_DotDumpLR);
  //DumpInitialLiveRanges();
  if(DEBUG_DotDumpLR){DotDumpLR(DEBUG_DotDumpLR, "initial");}

  //compute loop nesting depth needed for computing priorities
  find_nesting_depths(chow_arena);
  
  //run the priority algorithm
  RunChow();
  if(DEBUG_DotDumpLR){DotDumpFinalLRs(DEBUG_DotDumpLR);}
  RenameRegisters();
 
  //Dump(); 
  Output(); 
  DumpParams();
  DumpChowStats();
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
  if(PARAM_EnhancedCodeMotion) PARAM_MoveLoadsAndStores = true;
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
  fprintf(stderr, " Thwarted Copies : %d\n", chowstats.cThwartedCopies);
  fprintf(stderr, "***** ALLOCATION STATISTICS *****\n");
  //note: +/- 1 colored/spill count is for frame pointer live range
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

void DotDumpLR(LRID lrid, const char* tag)
{
  char fname[32] = {0};
  sprintf(fname, "tmp_%d_%d_%s.dot", lrid, lrid, tag);
  dot_dump_lr(live_ranges[lrid], fname);
}
void DotDumpFinalLRs(LRID lrid)
{
  DotDumpLR(DEBUG_DotDumpLR, "final");
  for(unsigned int i = 0; i < DEBUG_WatchLRIDs.size(); i++)
    DotDumpLR(DEBUG_WatchLRIDs[i], "final");
}

//define this to avoid a compiler warning for unused functions
void thisMayGiveACompilerWarning()
{
  DotDumpLR(0, "bullshit"); DumpInitialLiveRanges();
}
