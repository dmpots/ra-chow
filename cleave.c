/*====================================================================
 * Cleave limits the size of a basic block based on a predicate
 * function. The defualt predicate limits the size based on the number
 * of instructions. Blocks are split from the bottom up until it is
 * decided that they do not need any more splitting.
 *
 * Variable type meanings:
 * arn - Arena
 * in - Inst*
 * blk - Block*
 * ex - Expr
 * lbl - Label
 * edg - Edge*
 * 
 *====================================================================
 * $Id: cleave.c 159 2006-07-27 16:04:36Z dmp $
 * $URL: http://dmpots.com/svn/research/compilers/regalloc/src/cleave.c $
 ********************************************************************/


#include <Shared.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <list>
#include "debug.h"
#include "util.h"
#include "cleave.h"

//file local variables
static Unsigned_Int cinLast = 0;
static Inst* (*fnCleavePred)(Block*) = NULL;
static Arena arnCleave = NULL;

//file local functions
static Inst* In_CleavePred(Block* blk);
static Block* Blk_CleaveBlock(Block* blk, Inst* in);
static BlockTuple Blk2_CleaveBlockAt(Inst* in, Block* blk);
static Block* Blk_CreateBlock();
static Inst* MoveInstTo(Inst* in, Block* blk);
static void InsertJumpFromTo(Block* blkFrom, Block* blkTo);
static void RemoveInstFromBlock(Inst* in);
static void InsertInstInsertAfter(Inst* inInsert, Inst* inAfter);
static void InsertInstInsertBefore(Inst* inInsert, Inst* inBefore);
static Inst* In_CreateJmpTo(Expr ex);
static Expr Ex_ExprFromLabel(Label* plbl);
static Inst* In_LastInst(Block* blk);
static void DumpBlock(Block* blk);
static void FixControlFlow(Block* blkTop, Block* blkBot);
static Boolean F_IsFrameInst(Inst* in);
static Inst* In_FirstInst(Block* blk);

/*
 *===============
 * InitCleaver()
 *===============
 *
 ***/
void InitCleaver(Arena arn, Unsigned_Int cinLastT)
{
  fnCleavePred = &In_CleavePred;
  cinLast = cinLastT;
  arnCleave = arn;

  assert(cinLast != 1);//min 2 inst: 1 inst + 1 control flow
}


/*
 *===============
 * In_CleavePred()
 *===============
 *
 ***/
Inst* In_CleavePred(Block* blk)
{
  Inst* in = NULL;
  Inst* inCleaveHere = NULL;
  Unsigned_Int cin = 0;

  Block_ForAllInstsReverse(in, blk)
  {
    cin++;
    if(cin == cinLast)
    {
      if(in != In_FirstInst(blk))
      {
        inCleaveHere = in;
        break;
      }
    }
  }

  return inCleaveHere;
}


/*
 *===============
 * CleaveBlocks()
 *===============
 *
 ***/
void CleaveBlocks()
{
  //do not split if 0 is passed as max number of insts
  if(cinLast == 0) return;
  CleaveBlocksWithPred(NULL);
}

/*
 *========================
 * CleaveBlocksWithPred()
 *========================
 *
 ***/
void CleaveBlocksWithPred(Inst* (*fnPred)(Block*))
{
  if(fnPred != NULL)
  {
    fnCleavePred = fnPred;
  }
  assert(fnCleavePred);


  Block* blk;
  Inst* in;
  std::list<Block*> lstblk;
  ForAllBlocks(blk)
  {
    if((in = fnCleavePred(blk)))
    {
      lstblk.push_back(blk);
      //blkT = Blk_CleaveBlock(blk, in);

      //fix the loop variable 
      //blk = blkT;
    }
  }

  //work through list
  std::list<Block*>::iterator itblk;
  for(itblk = lstblk.begin(); itblk != lstblk.end(); itblk++)
  {
    blk = *itblk;
    debug("processing block: %s", bname(blk));
    in = fnCleavePred(blk);
    (void) Blk_CleaveBlock(blk, in);
  }

  Block_Order(); //fix control flow graph
}


/*
 *========================
 * Blk_CleaveBlk()
 *========================
 * cleaves a block repeatedly starting at Inst in unitl the block no
 * longer needs to be split as determined by the predicate function
 ***/
Block* Blk_CleaveBlock(Block* blk, Inst* in)
{
  Block* blkExit;
  BlockTuple blk2;

  blk2  = Blk2_CleaveBlockAt(in, blk);
  blkExit = blk2.bottom;
  blk = blk2.top;

  while((in = fnCleavePred(blk)))
  {
    //cleave until the block should be clove no more
    blk2 = Blk2_CleaveBlockAt(in, blk);
    blk = blk2.top;
  }

  return blkExit;
}

/*
 *========================
 * Blk_CleaveBlkAt()
 *========================
 * cleaves the block from the bottom up to inLast
 ***/
BlockTuple Blk2_CleaveBlockAt(Inst* inLast, Block* blk)
{
  Inst* in;
  Inst* inIterFix;
  Block* blkNew = Blk_CreateBlock();

  debug("cleaving block: %s(%d)", bname(blk), id(blk));
  //walk up the insts backward until arriving at the clip point
  Block_ForAllInstsReverse(in, blk)
  {
    debug("Move %s from %s to %s", 
      opcode_specs[in->operations[0]->opcode].opcode,
      bname(blk), bname(blkNew));

    inIterFix = MoveInstTo(in, blkNew);
    //DumpBlock(blk); DumpBlock(blkNew);

    if(in == inLast)
    {
      //insert control flow to new block
      FixControlFlow(blk, blkNew);
      break;
    }

    in = inIterFix;
  }

  BlockTuple blk2;
  blk2.top = blk;
  blk2.bottom = blkNew;
  return blk2;
}

/*
 *========================
 * F_IsFrameInst()
 *========================
 * 
 ***/
Boolean F_IsFrameInst(Inst* in)
{
  return in->operations[0]->opcode == FRAME;
}


/*
 *========================
 * DumpBlock()
 *========================
 * 
 ***/
void DumpBlock(Block* blk)
{
#ifdef __DEBUG
  Block_Dump(blk, NULL, TRUE);
#endif
}

/*
 *========================
 * MoveInstFromTo()
 *========================
 * 
 ***/
Inst* MoveInstTo(Inst* in, Block* blk)
{
  //save so can continue iteration from the previous inst
  //since we are going bottom up we need to grab the next inst
  Inst* inNext = in->next_inst; 
  RemoveInstFromBlock(in);

  //insert after the first instruction since we are moving the
  //instructions over from the bottom up
  InsertInstInsertAfter(in, blk->inst);


  return inNext;
}

/*
 *========================
 * RemoveInstFromBlock()
 *========================
 * 
 ***/
void RemoveInstFromBlock(Inst* in)
{
  in->next_inst->prev_inst = in->prev_inst;
  in->prev_inst->next_inst = in->next_inst;
}

/*
 *========================
 * InsertJumpFromTo()
 *========================
 * 
 ***/
void InsertJumpFromTo(Block* blkFrom, Block* blkTo)
{
  Inst* in = In_CreateJmpTo(Ex_ExprFromLabel(blkTo->labels));
  InsertInstInsertAfter(in, In_LastInst(blkFrom));

}

/*
 *========================
 * InsertJumpFromTo()
 *========================
 * 
 ***/
Inst* In_LastInst(Block* blk)
{
  return blk->inst->prev_inst;
}


/*
 *========================
 * FixControlFlow()
 *========================
 * 
 * 1) 
 ***/
void FixControlFlow(Block* blkTop, Block* blkBot)
{
  debug("fixing control flow: %s ==> %s", bname(blkTop), bname(blkBot));

  //bottom block edges
  Edge* edgBotPred = (Edge*)Arena_GetMem(arnCleave, sizeof(Edge));
  edgBotPred->pred = blkTop;
  edgBotPred->succ = blkBot;
  edgBotPred->next_pred = NULL;
  edgBotPred->next_succ = NULL;
  edgBotPred->back_edge = FALSE;
  edgBotPred->edge_extension = NULL;
  blkBot->pred = edgBotPred;
  blkBot->succ = blkTop->succ; //previous whole block succ list

  //need to fix the pred edge for the succs of the blkTop 
  //to point to blkBot
  Edge* edg;
  Edge* edgT;
  Block_ForAllSuccs(edg, blkTop)
  {
    Block_ForAllPreds(edgT, edg->succ)
    {
      if(edgT->pred == blkTop)
      {
        edgT->pred = blkBot;
      }
    }
  }

  //top block edges
  InsertJumpFromTo(blkTop, blkBot);
  Edge* edgTopSucc = (Edge*)Arena_GetMem(arnCleave, sizeof(Edge));
  edgTopSucc->pred = blkTop;
  edgTopSucc->succ = blkBot;
  edgTopSucc->next_pred = NULL;
  edgTopSucc->next_succ = NULL;
  edgTopSucc->back_edge = FALSE;
  edgTopSucc->edge_extension = NULL;

  //blkTop->pred does not change
  blkTop->succ = edgTopSucc;

  //DumpBlock(blkTop); DumpBlock(blkBot);
}



/*
 *========================
 * Blk_CreateBlock()
 *========================
 * 
 ***/
Block* Blk_CreateBlock()
{
  Block* blk = Block_Build_Drone_Block();

  //create a new label
  Label* plbl = Label_Invent();
  plbl->next = blk->labels;
  blk->labels = plbl;

  block_count++;

  return blk;
}



/*
 *========================
 * InsertInstInsertBefore()
 *========================
 * 
 ***/
void InsertInstInsertBefore(Inst* inInsert, Inst* inBefore)
{
  Block_Insert_Instruction(inInsert, inBefore);
}


/*
 *========================
 * InsertInstInsertAfter()
 *========================
 * 
 ***/
void InsertInstInsertAfter(Inst* inInsert, Inst* inAfter)
{
  Block_Insert_Instruction(inInsert, inAfter->next_inst);
}

Expr Ex_ExprFromLabel(Label* plbl)
{
  return plbl->label;
}

Inst* In_FirstInst(Block* blk)
{
  return blk->inst->next_inst;
}




/*
 *========================
 * In_CreateJmpTo()
 *========================
 * 
 ***/
static const int JMPL_OPSIZE = 7;
Inst* In_CreateJmpTo(Expr ex)
{
  Inst* inJmp;
  Operation* opJmp;

  //allocate a new instruction and operation
  inJmp = (Inst*)Inst_Allocate(arnCleave, 1);
  opJmp = (Operation*) Operation_Allocate(arnCleave, JMPL_OPSIZE);
  
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
  opJmp->arguments[0] = ex;
  opJmp->arguments[1] = 0;

  return inJmp;
}

