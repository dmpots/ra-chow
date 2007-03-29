/* debug.cc
 *
 * contains functions and variables used only for debugging the
 * allocator 
 */
#include <SSA.h>
#include "debug.h"
#include "live_range.h"
#include "live_unit.h"
using std::vector;


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
}


namespace Debug {
//keep track of all the lrids that are dumped throughout the program.
//there can be multiple lrids if the original lrid indicated by
//dot_dump_lr is split during allocation
vector<LRID> dot_dumped_lrids;

//used to control whether we dot dump a lr throughout the life of the
//allocator. a non-zero value indicates the live range to watch
LRID dot_dump_lr = 0;


/*
 *======================
 * LiveRange_DDumpAll()
 *======================
 *
 ***/
void LiveRange_DDumpAll(LRList* lrs)
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
void LiveRange_DumpAll(LRList* lrs)
{
  for(LRList::iterator i = lrs->begin(); i != lrs->end(); i++)
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

}//end Debug namespace


