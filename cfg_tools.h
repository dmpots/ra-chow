#ifndef __GUARD_CFG_TOOLS_H
#define __GUARD_CFG_TOOLS_H

//call this before using functions below
void InitCFGTools(Arena arena);


Block* CreateEmptyBlock();
Block* SplitEdge(Block* blkPred, Block* blkSucc);
void InsertJumpFromTo(Block* blkFrom, Block* blkTo);
Operation* GetControlFlowOp(Block* b);
void InsertInstAfter(Inst* newInst, Inst* afterInst);
Inst* LastInst(Block* blk);

#endif
