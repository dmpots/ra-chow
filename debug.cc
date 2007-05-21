/* debug.cc
 *
 * contains functions and variables used only for debugging the
 * allocator 
 */
#include <Shared.h>
#include <SSA.h>
#include <string>

#include "debug.h"
#include "live_range.h"
#include "live_unit.h"
#include "mapping.h"
#include "dot_dump.h"


namespace {
/* local variables */
const char* type_str[] = 
  {"NO-TYPE",    /* NO_DEFS */
   "INTEGER", /* INT_DEF */ 
   "FLOAT", /* FLOAT_DEF */
   "DOUBLE", /* DOUBLE_DEF */
   "COMPLEX", /* COMPLEX_DEF */
   "DCOMPLEX", /* DCOMPLEX_DEF */ 
   "MULT-TYPE"};   /* MULT_DEFS */


/* local functions */
std::string write_known_quantity(
				 Operation *op,
				 int number_to_write,
				 std::string (*function_to_use)(unsigned int),
				 int *starting_position);

std::string string_of_op(Operation *op);
std::string string_of_expr(Expr expr);
std::string string_of_register(unsigned int reg);
}


namespace Debug {
//keep track of all the lrids that are dumped throughout the program.
//there can be multiple lrids if the original lrid indicated by
//dot_dump_lr is split during allocation
std::vector<LiveRange*> dot_dumped_lrs;

//used to control whether we dot dump a lr throughout the life of the
//allocator. a non-zero value indicates the live range to watch
LRID dot_dump_lr = 0;
bool dump_all_splits = false;


/*
 *======================
 * LiveRange_DDumpAll()
 *======================
 *
 ***/
void LiveRange_DDumpAll(LRVec* lrs)
{
#ifdef __DEBUG
  LiveRange_DumpAll(lrs);
#endif
}

/*
 *======================
 * LiveRange_DumpAll()
 *======================
 *
 ***/
void LiveRange_DumpAll(LRVec* lrs)
{
  for(LRVec::iterator i = lrs->begin(); i != lrs->end(); i++)
  {
    LiveRange_Dump(*i);
  }
}


/*
 *======================
 * LiveRange_DDump()
 *======================
 *
 ***/
void LiveRange_DDump(LiveRange* lr)
{
#ifdef __DEBUG
  LiveRange_Dump(lr);
#endif
}

/*
 *======================
 * LiveRange_Dump()
 *======================
 *
 ***/
void LiveRange_Dump(LiveRange* lr)
{

  fprintf(stderr,"************ BEGIN LIVE RANGE DUMP **************\n");
  fprintf(stderr,"LR: %d\n",lr->id);
  fprintf(stderr,"type: %s\n",type_str[lr->type]);
  fprintf(stderr,"color: %d\n", lr->color);
  fprintf(stderr,"orig_lrid: %d\n", lr->orig_lrid);
  fprintf(stderr,"candidate?: %c\n", lr->is_candidate ? 'T' : 'F');
  fprintf(stderr,"rematerializable?: %c\n", lr->rematerializable 
                                            ? 'T' : 'F');
  fprintf(stderr,"forbidden colors: \n");
    VectorSet_Dump(lr->forbidden);
  fprintf(stderr, "BB LIST:\n");
    Unsigned_Int b;
    VectorSet_ForAll(b, lr->bb_list)
    {
      fprintf(stderr, "  %d\n", (b));
    }

  fprintf(stderr, "Live Unit LIST:\n");
    //LiveUnit* unit;
    //LiveRange_ForAllUnits(lr, unit)
    for(LiveRange::iterator i = lr->begin(); i != lr->end(); i++)
    {
      LiveUnit_Dump(*i);
    }

  fprintf(stderr, "Interference List:\n");
    //LiveRange* intf_lr;
    //LiveRange_ForAllFears(lr,intf_lr)
    for(LRSet::iterator i = lr->fear_list->begin();
        i != lr->fear_list->end();
        i++)
    {
      fprintf(stderr, "  %d\n", (*i)->id);
    }


  fprintf(stderr,"************* END LIVE RANGE DUMP ***************\n"); 
}


/*
 *======================
 * LiveUnit_DDump()
 *======================
 *
 ***/
void LiveUnit_DDump(LiveUnit* unit)
{
#ifdef __DEBUG
  LiveUnit_Dump(unit);
#endif
}

/*
 *======================
 * LiveUnit_Dump()
 *======================
 *
 ***/
void LiveUnit_Dump(LiveUnit* unit)
{
  fprintf(stderr,"LR Unit: %s (%d)\n",bname(unit->block),
                                      id(unit->block));
  fprintf(stderr,"  Need Load: %c\n",unit->need_load ? 'Y' : 'N');
  fprintf(stderr,"  Need Store: %c\n",unit->need_store ? 'Y' : 'N');
  fprintf(stderr,"  Uses: %d\n",unit->uses);
  fprintf(stderr,"  Defs: %d\n",unit->defs);
  fprintf(stderr,"  Start With Def: %c\n",unit->start_with_def?'Y':'N');
  fprintf(stderr,"  SSA    Name: %d\n",unit->orig_name);
  fprintf(stderr,"  Source Name: %d\n",SSA_name_map[unit->orig_name]);
}

/*
 *========================
 * DumpInitialLiveRanges()
 *========================
 *
 ***/
void DumpInitialLiveRanges()
{
  LOOPVAR i;
  for(i = 0; i < SSA_def_count; i++)
  {
    debug("SSA_map: %d ==> %d", i, SSA_name_map[i]);
    SSA_name_map[i] = Mapping::SSAName2OrigLRID(i);
  }
  SSA_Restore();   
  Block_Put_All(stdout);
  fprintf(stderr,"DumpInitialLiveRanges -- exit early\n");
  exit(0);
}

/*
 *============
 * DotDumpLR()
 *============
 *
 ***/
void DotDumpLR(LiveRange* lr, const char* tag)
{
  char fname[128] = {0};
  sprintf(fname, "tmp_%d_%d_%s.dot", lr->orig_lrid, lr->id, tag);
  Dot::Dump(lr, fname);
}

/*
 *===================
 * DotDumpFinalLRs()
 *===================
 *
 ***/
void DotDumpFinalLRs()
{
  for(unsigned int i = 0; i < Debug::dot_dumped_lrs.size(); i++)
    DotDumpLR(Debug::dot_dumped_lrs[i], "final");
}

/*
 *===================
 * DotDumpFinalLRs()
 *===================
 *
 ***/
const char* StringOfInst(Inst* inst)
{
  static std::string inst_s;
  int i;
  bool first = true; /* set to false after we put out the first operation
			   in an instruction, so that we know when to put
			   out the operation delimiter character */

  if (inst != NULL)
  {
    inst_s.clear();
    for(i=0; inst->operations[i] != NULL; i++)
    {
      if (first != TRUE) 
      {
        inst_s.append(" "); inst_s.append(";"); inst_s.append(" ");
      }

      inst_s.append(string_of_op(inst->operations[i]));
      first = FALSE;
    }
  }
  return inst_s.c_str();
}

const char* StringOfOp(Operation* op)
{
  static std::string op_s;
  op_s = string_of_op(op);
  return op_s.c_str();
}

}//end Debug namespace

namespace {
std::string string_of_op(Operation *op)
{
  std::string op_s;
  int running_position = 0;

  /* put the source line reference number out (the appropriate
      tab has already been put out by Inst_Put) */
  op_s.append(Expr_Get_String(op->source_line_ref));
  op_s.append("\t");
  
  /* put the opcode out */
  op_s.append((opcode_specs[(op)->opcode]).opcode);
  op_s.append("\t");

  /* put the expressions out */
  op_s.append(
    write_known_quantity(op,
        (int)op->constants,
        (std::string (*)(unsigned int))string_of_expr, &running_position)
  );
  
  /* put the used registers out */
  op_s.append(
    write_known_quantity(op,
        op->referenced - op->constants,
        (std::string(*)(unsigned int))string_of_register, &running_position)
  );
  
  /* put the arrow out if there are any defined registers */
  if (op->referenced < op->defined)
  {
    op_s.append("=> ");
      
    /* put the defined registers out */
    op_s.append(
      write_known_quantity(op,
            op->defined - op->referenced,
            (std::string (*)(unsigned int))string_of_register, &running_position)
      );
  }
  
  /* put the lists out */
  //write_lists(output_file, operation, &running_position, FALSE);
  
  /* put out the comment */
  if(op->comment)
    op_s.append("# ").append(Comment_Get_String(op->comment));

  return op_s;
} /* Operation_Put */

std::string string_of_expr(Expr expr)
{
  std::string s;
  if(Expr_Is_Integer(expr))
  {
    char buf[32] = {0};
    sprintf(buf, "%d", Expr_Get_Integer(expr));
    s.append(buf);
  }
  else
  {
    s.append(Expr_Get_String(expr));
  }

  return s;
}

std::string string_of_register(unsigned int reg)
{
  char buf[32] = {0};
  sprintf(buf, "r%d", reg);
  return std::string(buf);
}

/* writes out a given number of values from the arguments array */
std::string write_known_quantity(
				 Operation *op,
				 int number_to_write,
				 std::string (*function_to_use)(unsigned int),
				 int *starting_position)
{
  std::string s;
  int i;

  for(i=0; i < number_to_write; i++)
  {
      s.append(function_to_use(op->arguments[(*starting_position)++]));
      s.append(" ");
  }
  return s;

} /* write_known_quantity */






}
