/*====================================================================
 * cfg_tools.c
 * this file contains tools that are useful for manipulating the
 * control flow graph
 * 
 ********************************************************************/
/*-----------------------MODULE INCLUDES-----------------------*/
#include <Shared.h>
#include <cassert>
#include <cstring>

#include "cfg_tools.h"

/*------------------MODULE LOCAL DEFINITIONS-------------------*/
/* module types */

/* module variables */
static Arena tools_arena;

/* module functions */
static void AddEdgePart(Block* blkPred, Block* blkSucc, EdgeOwner owner);
static void RewriteControlFlow(Block*,Block*,Block*);
static Inst* CreateJmpTo(Block* blk);


/*--------------------BEGIN IMPLEMENTATION---------------------*/
void InitCFGTools(Arena arena)
{
  tools_arena = arena;
}


Block* SplitEdge(Block* blkPred, Block* blkSucc)
{
  debug("splitting edge %s ---> %s", bname(blkPred), bname(blkSucc));
  //create a new empty block
  Block* blkNew = CreateEmptyBlock();

  /* -- fix predecessor --*/
  //rewrite the label in the control flow op in the predecessor
  //block to be the label for the new block
  RewriteControlFlow(blkPred, blkNew, blkSucc);

  //update predecessor's edge to have a sink at the new block
  Edge* edgPred = FindEdge(blkPred, blkSucc, PRED_OWNS);
  edgPred->succ = blkNew;

  //add new edge to blkNew for pred -> blkNew
  AddEdgePart(blkPred, blkNew, SUCC_OWNS);

  /* -- fix succssor --*/
  //insert control flow to jump from the new block to the successor
  InsertJumpFromTo(blkNew, blkSucc);

  //update the succesor's edge to have a source at the new block
  Edge* edgSucc = FindEdge(blkPred, blkSucc, SUCC_OWNS);
  edgSucc->pred = blkNew;

  //add new edge to blkNew for blkNew -> succ
  AddEdgePart(blkNew, blkSucc, PRED_OWNS);

  return blkNew;
}

/*
 *========================
 * CreateEmptyBlock()
 *========================
 * 
 ***/
Block* CreateEmptyBlock()
{
  Block* blk = Block_Build_Drone_Block();

  //create a new label
  Label* plbl = Label_Invent();
  plbl->next = blk->labels;
  blk->labels = plbl;

  block_count++;

  return blk;
}



/* RewriteControlFlow() 
 * Changes the control flow operation in a block to point to a new
 * block. The control flow must be based on a label. Note that the
 * Edge structures are not changed at all by this function, just the
 * control flow op. They need to be modified separately.
 *
 * blkPred - the predecessor block for the control flow
 * blkSucc - the succor block for the control flow
 * blockOldSucc - the block that the control flow used to point to.
 */
static void 
RewriteControlFlow(Block* blkPred, Block* blkSucc, Block* blkOldSucc)
{
  Operation* op = NULL;
  Expr new_label;
  Expr exprT;
  Expr* exprp = &exprT;
  int i = 0;
  int label_indx = -1;
  Boolean found_label = FALSE;

  debug("connecting blocks %s ==> %s", bname(blkPred), bname(blkSucc));
  debug("original edge     %s ==> %s", bname(blkPred), bname(blkOldSucc));

  /* == STEP ==
   * fix label in control flow instruction */
  //determine the new label 
  new_label = blkSucc->labels->label;

  //find the control flow op within the block
  op = GetControlFlowOp(blkPred); 
  
  //check all the labels for a match with the original destination
  debug("trying to match label: %s",
  Label_Get_String(blkOldSucc->labels->label));

  if(op->opcode == JMPr || op->opcode == JMPT) //search through the list
  {
    i = op->defined;
    while((*exprp = op->arguments[i]))
    {
      debug("expr: %d is label: %s", *exprp, Label_Get_String(*exprp));
     
      for(Label* label = blkOldSucc->labels; label != NULL; 
          label = label->next)
        {
          //compare with the label for the block we cloned and 
          //overwrite with the new label if they match
          if(strcmp(Label_Get_String(*exprp),
                    Label_Get_String(label->label)) == 0)
          {
            label_indx = i;
            found_label = TRUE;
          }
        } 
      i++; 
    }
  }
  else //look in the constants of the instruction
  {

    Operation_ForAllConstants(exprp, op)
    {
      //if(Expr_Is_Block_Label(*exprp)) does not work for new labels
      //{
        debug("expr: %d is label: %s", *exprp, Label_Get_String(*exprp));
        //see if this label corresponds to the block we cloned
        for(Label* label = blkOldSucc->labels; label != NULL; 
            label = label->next)
        {
          //compare with the label for the block we cloned and 
          //overwrite with the new label if they match
          if(strcmp(Label_Get_String(*exprp),
                    Label_Get_String(label->label)) == 0)
          {
            label_indx = i;
            found_label = TRUE;
          }
        } 
      //} 
      i++;
    }
  }
  assert(found_label);
  //save the new label in the instruction
  Expr old_label = op->arguments[label_indx];
  op->arguments[label_indx] = new_label;
  debug("found label for cloned block, overwriting with: %s" ,
         Label_Get_String(new_label));

  //if using a jump table we must fix the label in the static data
  //area as well
  if(op->opcode == JMPT || op->opcode == JMPr)
  {
    debug("opcode is JMPT/JMPr - fixing static data area");
    assert(static_code_list); //must have it if there is a JMPT

    //find the lable for the jump table
    Expr jmptab_label = 0;
    bool search_all_labels = false;
    if(op->opcode == JMPT)
    {
      jmptab_label = op->arguments[0];
      debug("jump table lable is: %s", Label_Get_String(jmptab_label));
    }
    else //JMPr
    {
      //WARNING: HACK ATTACK
      //so this is a huge hack, we are hoping that the first
      //instruction in the block is a LDI of the label indicating the
      //jump table. in all examples we saw it was, but may not always
      //be so. also, if the block gets split the first inst may not be
      //the correct loadI. heres to hope...
      //assert(blkPred->inst->next_inst->operations[0]->opcode == iLDI);
      //jmptab_label =
      //  blkPred->inst->next_inst->operations[0]->arguments[0];

      //WARNING: HACK ATTACK (part 2)
      // so the above did not work for the reasons outlined. the next
      // attempt is to just search through all static labels until we
      // find the label we are trying to replace. hopefully this works
      // ok and the lable is not in the static code list for another
      // reason as well...
      search_all_labels = true;
    }
    Static_Code_List* scl = static_code_list;
    do
    {
      debug("looking at static label: %s",
            Label_Get_String(scl->label->label));
      if(scl->label->label == jmptab_label || search_all_labels)
      {
        debug("found jmp table label");
        debug("attempting to replace iDATA label: %s",
                  Label_Get_String(old_label));
        Static_Code_Node* scn = scl->operation_list;
        do
        {
          debug("looking at iDATA label: %s",
                  Label_Get_String(scn->expression));
          if(scn->expression == old_label)
          {
            debug("found old label in jump table, replacing with: %s", 
                  Label_Get_String(new_label));
            scn->expression = new_label;
          }
          scn = scn->next_operation;
        }while(scn != scl->operation_list);
        if(!search_all_labels) break;
      }

      scl = scl->next_list;
    } while (scl != static_code_list);
  }

  debug("control flow fixed");
}



/* AddEdgePart() 
 * Adds an Edge structure to the list of edges for a block.
 *
 * blkPred - the predecessor block for the edge
 * blkSucc - the succor block for the edge
 * owner - which block this edge should be added to. remember that
 *         each edge in the graph has two Edge structures associate
 *         with it, one for each end.
 */
static void AddEdgePart(Block* blkPred, Block* blkSucc, EdgeOwner owner)
{
  Edge* edge = (Edge*) Arena_GetMem(tools_arena, sizeof(Edge));
  Block* blkOwner = (owner == PRED_OWNS ? blkPred : blkSucc);

  edge->next_pred = blkOwner->pred;
  edge->next_succ = blkOwner->succ;
  edge->pred = blkPred;
  edge->succ = blkSucc;
  edge->back_edge = FALSE;
  edge->edge_extension = NULL;


  if(owner == PRED_OWNS)
  {
    blkOwner->succ = edge;
  }
  else
  {
    blkOwner->pred = edge;
  }
}



Operation* GetControlFlowOp(Block* b)
{
  Inst* inst;
  Operation** op;
  Operation* cfop = NULL;

  Block_ForAllInstsReverse(inst, b)
  {
    Inst_ForAllOperations(op, inst)
    { 
      switch((*op)->opcode)
      {
        case JMPl:
          cfop = *op;
          break;
        case JMPr:
          cfop = *op;
          break;
        case JMPT:
          cfop = *op;
          break;
        case BR:
          cfop = *op;
          break;
        default:
          debug("Not a control flow op");
      }
      if(cfop) break; //found some control flow
    }
    if(cfop) break; //found some control flow
  }
  assert(cfop != NULL);
  return cfop;
}

/*
 *========================
 * InsertJumpFromTo()
 *========================
 * 
 ***/
void InsertJumpFromTo(Block* blkFrom, Block* blkTo)
{
  Inst* in = CreateJmpTo(blkTo);
  InsertInstAfter(in, Block_LastInst(blkFrom));
}

/*
 *========================
 * CreateJmpTo()
 *========================
 * 
 ***/
static const int JMPL_OPSIZE = 7;
Inst* CreateJmpTo(Block* blk)
{
  Inst* inJmp;
  Operation* opJmp;

  //allocate a new instruction and operation
  inJmp = (Inst*)Inst_Allocate(tools_arena, 1);
  opJmp = (Operation*) Operation_Allocate(tools_arena, JMPL_OPSIZE);
  
  //attach operation to instruction
  inJmp->operations[0] = opJmp;
  inJmp->operations[1] = NULL;

  //fill in struct
  opJmp->opcode  = JMPl;
  opJmp->comment = 0;
  opJmp->source_line_ref = Expr_Install_String("0");
  opJmp->constants = 1;
  opJmp->referenced = 1;
  opJmp->defined = 1;
  opJmp->critical = FALSE;

  //fill in arguments array
  Expr label = blk->labels->label;
  opJmp->arguments[0] = label;
  opJmp->arguments[1] = 0;

  return inJmp;
}


/*
 *========================
 * InsertInstAfter()
 *========================
 * 
 ***/
void InsertInstAfter(Inst* newInst, Inst* afterInst)
{
  Block_Insert_Instruction(newInst, afterInst->next_inst);
}
void InsertInstBefore(Inst* newInst, Inst* beforeInst)
{
  Block_Insert_Instruction(newInst, beforeInst);
}


/*
 *========================
 * LastInst()
 *========================
 * 
 ***/
Inst* Block_LastInst(Block* blk)
{
  return blk->inst->prev_inst;
}

/*
 *========================
 * FirstInst()
 *========================
 * 
 ***/
Inst* Block_FirstInst(Block* blk)
{
  return blk->inst->next_inst;
}

/*
 *========================
 * PredCount()
 *========================
 * 
 ***/
Unsigned_Int Block_PredCount(Block* blk)
{
  Unsigned_Int cnt = 0;
  Edge* e;
  Block_ForAllPreds(e, blk) cnt++;

  return cnt;
}

/*
 *========================
 * SuccCount()
 *========================
 * 
 ***/
Unsigned_Int Block_SuccCount(Block* blk)
{
  Unsigned_Int cnt = 0;
  Edge* e;
  Block_ForAllSuccs(e, blk) cnt++;

  return cnt;
}



/*
 *========================
 * FindEdge()
 *========================
 * returns the edge for blkPred ---> blkSucc. The edge returned is the
 * one owned by the requested block.
 ***/
Edge* FindEdge(Block* blkPred, Block* blkSucc, EdgeOwner owner)
{
  Edge* edge;

  if(owner == PRED_OWNS)
  {
    Block_ForAllSuccs(edge, blkPred)
    {
      if(edge->succ == blkSucc) return edge;
    }
  }
  else
  {
    Block_ForAllPreds(edge, blkSucc)
    {
      if(edge->pred == blkPred) return edge;
    }
  }

  //edge not found
  assert(edge != NULL);
  return NULL;
}

/*
 *========================
 * Block_IsLoopHeader()
 *========================
 * returns true if this block is a loop header
 ***/
bool Block_IsLoopHeader(Block* blk)
{
  Edge* e;
  Block_ForAllPreds(e, blk) {if(e->back_edge) return true;}

  return  false;
}

/*
 *==========================
 * GetFrameOperation()
 *==========================
 * Gets the operation corresponding to the FRAME statement in the iloc
 * code
 **/
Operation* GetFrameOperation()
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


