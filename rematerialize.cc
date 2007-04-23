/* contains functions and data for implementing rematerialization in
 * the chow allocator.
 *
 * The core ideas for the implemenation come from the paper
 * "Rematerialization" by Briggs, Cooper, Torczan. They have been
 * modified to fit into the framework of a chow allocator.
 *
 * the code and algorithm for finding the tag values borrows heavily
 * from the ra implementation:
 *   /home/compiler/installed/ra/rematerialize.inc
 */

/*--------------------------INCLUDES---------------------------*/
#include <SSA.h>
#include <list>
#include "rematerialize.h"
#include "cfg_tools.h"

/*------------------MODULE LOCAL DECLARATIONS------------------*/
namespace {
using Remat::LatticeVal;
using Remat::LatticeElem;
using std::vector;
using std::list;

//functions
void InitializeTagsAndWorklist(vector<LatticeElem>&,list<Variable>&);
LatticeElem MeetOverPhiNodeOps(const Phi_Node*, 
                              LatticeElem, 
                              const vector<LatticeElem>&);
bool OpersAllEqual(Operation* oper1, Operation* oper2);
}

/*--------------------BEGIN IMPLEMENTATION---------------------*/
namespace Remat{
using std::vector;
vector<LatticeElem> tags;

void ComputeTags()
{
  //save space for all the tags and initailize to TOP
  tags.resize(SSA_def_count);
  for(unsigned int i = 0; i < tags.size(); i++) tags[i].val = TOP;

  //find constant values and initalize worklist
  std::list<Variable> worklist;
  InitializeTagsAndWorklist(tags, worklist);

  //run the sparse constant propagation algorithm on the lattice to
  //find which values are rematerializable
  while(worklist.size() > 0)
  {
    Variable def = worklist.front(); worklist.pop_front();

    //follow the def to its uses and update those lattice elements
    Chains_List* use;
    Chain_ForAllUses(use,def)
    {
      Chain chain = use->chain;
      if(chain.is_phi_node)
      //meet over all phi-node params 
      {
        Variable phi_name = chain.op_pointer.phi_node->new_name; 
        LatticeElem orig_lattice_elem = tags[phi_name];
        LatticeElem new_elem = 
          MeetOverPhiNodeOps(chain.op_pointer.phi_node,
                             orig_lattice_elem,
                             tags);
        //and update lattice if the value changes
        if(new_elem.val != orig_lattice_elem.val)
        {
          tags[phi_name].val = new_elem.val;
          tags[phi_name].op  = new_elem.op;
          worklist.push_back(phi_name);
        }
      }
      else
      //lower the defs of the operation, handling copies special
      {
        Operation* op = chain.op_pointer.operation;
        if(opcode_specs[op->opcode].details & COPY)
        {
          Register src = op->arguments[0];
          Register dest = op->arguments[1];
          //update lattice if the source is lower than copy
          if(tags[src].val < tags[dest].val)
          {
            tags[dest].val = tags[src].val;
            tags[dest].op  = tags[src].op;
            worklist.push_back(dest);
          }
        }
        else //not a copy
        {
          //lower all defined registers to bottom
          Register* reg;
          Operation_ForAllDefs(reg, op)
          {
            if(tags[*reg].val != BOTTOM)
            {
              tags[*reg].val = BOTTOM;
              worklist.push_back(*reg);
            }
          }
        }
      }
    }
  }

  DumpTags();
}

void DumpTags()
{
  #ifdef __DEBUG
  for(unsigned int i = 0; i < tags.size(); i++)
  {
    LatticeElem lv = tags[i];
    const char* val = lv.val == CONST ? "CONST" : "NON-CONST";
    const char* op  = lv.val == CONST ? Debug::StringOfOp(lv.op) : "";
    debug("%s %s", val, op);
  }
  #endif
}

}//end Rematerialize namespace

/*-------------------BEGIN LOCAL DEFINITIONS-------------------*/

namespace {

LatticeElem
MeetOverPhiNodeOps(const Phi_Node* phi_node, 
                   LatticeElem phi_elem,
                   const vector<LatticeElem>& tags)
{
  LatticeElem new_elem;
  new_elem.val = phi_elem.val;
  new_elem.op  = phi_elem.op;
  const Variable *parm_ptr;

  Phi_Node_ForAllParms(parm_ptr, phi_node)
  {
    Variable parm = *parm_ptr;
    LatticeElem parm_elem = tags[parm];
    if (parm_elem.val < new_elem.val)
    {
      new_elem.val = parm_elem.val;
      new_elem.op  = parm_elem.op;
    }
    else if (parm_elem.val == new_elem.val && new_elem.val == Remat::CONST)
    {
      if(!OpersAllEqual(new_elem.op, parm_elem.op))
        new_elem.val = Remat::BOTTOM;
    }
  }
  return new_elem;
}

void
InitializeTagsAndWorklist(vector<LatticeElem>& tags,
                          list<Variable>& worklist)
{
  using Remat::CONST;
  using Remat::BOTTOM;
  Block *block; Inst *inst; Operation **oper_ptr;
  
  //need the frame to note that the static_pointer_reg and
  //static_data_reg are known values. we can rematerialize operations 
  //that use these registers
  Operation *frame = GetFrameOperation();
  Register stack_pointer_reg = frame->arguments[1];
  Register static_data_reg   = frame->arguments[2];

  ForAllBlocks(block)
  {
    Block_ForAllInsts(inst, block)
    {
      Inst_ForAllOperations(oper_ptr, inst)
      {
        Operation *oper = *oper_ptr;
        Unsigned_Int details = opcode_specs[oper->opcode].details;

        //see whether or not we can rematerialize the expression
        if ((details & EXPR))
        {
          Unsigned_Int2 *arg_ptr;
          Boolean bad_reg_found = FALSE;
          Unsigned_Int result = oper->arguments[oper->defined-1];
          Operation_ForAllUses(arg_ptr, oper)
          if (*arg_ptr != stack_pointer_reg)
          {
              bad_reg_found = TRUE;
              break;
          }

          LatticeVal   lv = BOTTOM;
          Operation* lop = NULL;
          if (!bad_reg_found)
          {
            lv = CONST;
            lop = oper;
          }

          tags[result].val = lv;
          tags[result].op  = lop;
          worklist.push_back(result);
        }

        //else the operation can not be rematerialized, send it to bottom
        //unless it is a copy, in which case we will wait for more
        //information about the source before we lower the copy dest
        else if (!(details & COPY))
        {
          //Mark all the defined registers as BOTTOM
          Unsigned_Int *arg_ptr;
          Operation_ForAllDefs(arg_ptr, oper)
          {
              Unsigned_Int result = *arg_ptr;
              if (result != stack_pointer_reg &&
                  result != static_data_reg)
              {
                worklist.push_back(result);
                tags[result].val = BOTTOM;
              }
          }
        }
      }
    }
  }
}

bool OpersAllEqual(Operation* oper1, Operation* oper2)
{
  Unsigned_Int i;

  if (oper1->opcode != oper2->opcode)
      return false;

  for (i = 0; i < oper1->referenced; i++)
      if (oper1->arguments[i] != oper2->arguments[i])
          return false;

  /*  If we haven't already returned FALSE, they must be equal.  */
  return true;
}

}//end anonymous namespace

