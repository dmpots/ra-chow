#ifndef __GUARD_CFG_TOOLS_H
#define __GUARD_CFG_TOOLS_H

#include <Shared.h>

//call this before using functions below
void InitCFGTools(Arena arena);

/* types */
enum EdgeOwner{PRED_OWNS, SUCC_OWNS};

/* external module functions */
Block* CreateEmptyBlock();
Block* SplitEdge(Block* blkPred, Block* blkSucc);
void InsertJumpFromTo(Block* blkFrom, Block* blkTo);
Operation* GetControlFlowOp(Block* b);
void InsertInstAfter(Inst* newInst, Inst* afterInst);
Inst* Block_LastInst(Block* blk);
Inst* Block_FirstInst(Block* blk);
Unsigned_Int Block_PredCount(Block* blk);
Unsigned_Int Block_SuccCount(Block* blk);
Edge* FindEdge(Block* blkPred, Block* blkSucc, EdgeOwner owner);

#endif
