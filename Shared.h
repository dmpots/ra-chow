#ifndef SHARED
#define SHARED 1

#ifdef __cplusplus
extern "C" {
#endif /*__cpluplus */

#include <stdio.h>
#ifdef RUNTIME_CHECK
#define ABORT abort()
#else
#define ABORT exit(-1)
#endif

typedef FILE *File;
typedef char Char;
typedef double Double;
typedef int Int;
typedef signed char Int1;
typedef int Int2;
typedef long Int4;
typedef unsigned int Unsigned_Int;
typedef unsigned char Unsigned_Int1;
typedef unsigned int Unsigned_Int2;
typedef unsigned long Unsigned_Int4;
#undef FALSE
#undef TRUE
enum bbool {
  FALSE = 0,
  TRUE = 1
};

typedef Unsigned_Int1 Boolean;

typedef struct tms *Timer;

extern Boolean time_print;

Timer Time_Start(void);
void Time_Dump(Timer timer_var, Char *format_string);


typedef union arena *Arena;

#ifndef __cplusplus
Boolean mem_stats_print;
#endif

Arena Arena_Create(void);
void Arena_Destroy(Arena);
void *Arena_GetMem(Arena, Unsigned_Int num_bytes);
void *Arena_GetMemClear(Arena, Unsigned_Int num_bytes);
void Arena_Mark(Arena);
void Arena_Release(Arena);
void Arena_DumpStats(void);


struct sparse_set_node {
  Unsigned_Int2 stack_element;
  Unsigned_Int2 stack_index;
};

struct sparse_set {
  Unsigned_Int2 universe_size;
  Unsigned_Int2 member_count;
  struct sparse_set_node node[1];
};

typedef struct sparse_set *SparseSet;

#define SparseSet_ForAll(x, set)                        \
  for (x = (set)->member_count;                         \
       x && (x = (set)->node[x-1].stack_element, 1);    \
       x = (set)->node[x].stack_index)

SparseSet SparseSet_Create(Arena, Unsigned_Int universe_size);
void SparseSet_Clear(SparseSet);
void SparseSet_Insert(SparseSet, Unsigned_Int x);
void SparseSet_Delete(SparseSet, Unsigned_Int x);
Boolean SparseSet_Member(SparseSet, Unsigned_Int x);
Unsigned_Int SparseSet_Size(SparseSet);
Unsigned_Int SparseSet_ChooseMember(SparseSet);
Boolean SparseSet_Equal(SparseSet, SparseSet);
void SparseSet_Copy(SparseSet dst, SparseSet src);
void SparseSet_Union(SparseSet x, SparseSet y, SparseSet z);
void SparseSet_Intersect(SparseSet x, SparseSet y, SparseSet z);
void SparseSet_Difference(SparseSet x, SparseSet y, SparseSet z);
void SparseSet_Complement(SparseSet x, SparseSet y);
void SparseSet_Sort(SparseSet);
void SparseSet_Dump(SparseSet);
void SparseSet_Dump_Pretty(SparseSet);


struct vector_set {
  Unsigned_Int4 universe_size;
  Unsigned_Int4 word_count;
  Unsigned_Int4 word[1];
};

typedef struct vector_set *VectorSet;

#define VectorSet_ForAll(x, set)                \
  for (x = 0; x < (set)->universe_size; x++)    \
    if (VectorSet_Member((set), x))

VectorSet VectorSet_Create(Arena, Unsigned_Int universe_size);
void VectorSet_Clear(VectorSet);
void VectorSet_Insert(VectorSet, Unsigned_Int);
void VectorSet_Delete(VectorSet, Unsigned_Int);
Boolean VectorSet_Member(VectorSet, Unsigned_Int);
Unsigned_Int VectorSet_Size(VectorSet);
Unsigned_Int VectorSet_ChooseMember(VectorSet);
void VectorSet_Copy(VectorSet dst, VectorSet src);
Boolean VectorSet_Equal(VectorSet, VectorSet);
void VectorSet_Union(VectorSet x, VectorSet y, VectorSet z);
void VectorSet_Intersect(VectorSet x, VectorSet y, VectorSet z);
void VectorSet_Difference(VectorSet x, VectorSet y, VectorSet z);
void VectorSet_Imply(VectorSet x, VectorSet y, VectorSet z);
void VectorSet_DifferenceUnion(VectorSet w, VectorSet x, 
                               VectorSet y, VectorSet z);
void VectorSet_Complement(VectorSet x, VectorSet y);
void VectorSet_Dump(VectorSet);


typedef Unsigned_Int2 Expr;
Boolean Expr_Is_Integer(Expr);
Unsigned_Int Expr_Count(void);
Int Expr_Get_Integer(Expr);
Expr Expr_Install_Int(Int);
Char *Expr_Get_String(Expr);
Expr Expr_Install_String(Char *);
extern Expr question_mark_expr;
extern Expr exclamation_point_expr;

enum def_types {
  NO_DEFS, INT_DEF, FLOAT_DEF, DOUBLE_DEF,
  COMPLEX_DEF, DCOMPLEX_DEF, MULT_DEFS
};

typedef Unsigned_Int1 Def_Type;
typedef
  enum {
    NAME, ALIAS,
    bDATA, wDATA, iDATA, fDATA, dDATA, BYTES,
    bSSTACK, wSSTACK, iSSTACK, fSSTACK, dSSTACK,
    bSTACK, wSTACK, iSTACK, fSTACK, dSTACK,
    bSGLOBAL, wSGLOBAL, iSGLOBAL, fSGLOBAL, dSGLOBAL,
    bGLOBAL, wGLOBAL, iGLOBAL, fGLOBAL, dGLOBAL,
    bXDATA, wXDATA, iXDATA, fXDATA, dXDATA,
    XFUNC, iXFUNC, fXFUNC, dXFUNC,
    HALT, NOP, BLOCKID,
    FRAME,
    JSRr, iJSRr, fJSRr, dJSRr, cJSRr, qJSRr,
    JSRl, iJSRl, fJSRl, dJSRl, cJSRl, qJSRl,
    RTN,  iRTN,  fRTN,  dRTN,  cRTN,  qRTN,
    iLDI, fLDI, dLDI, cLDI, qLDI,
    bCONor, wCONor, iCONor, fCONor, dCONor, cCONor, qCONor,
    bPLDor, wPLDor, iPLDor, fPLDor, dPLDor, cPLDor, qPLDor,
    bPLDrr, wPLDrr, iPLDrr, fPLDrr, dPLDrr, cPLDrr, qPLDrr,
    bPSTor, wPSTor, iPSTor, fPSTor, dPSTor, cPSTor, qPSTor,
    bPSTrr, wPSTrr, iPSTrr, fPSTrr, dPSTrr, cPSTrr, qPSTrr,
    bSLDor, wSLDor, iSLDor, fSLDor, dSLDor, cSLDor, qSLDor,
    bSLDrr, wSLDrr, iSLDrr, fSLDrr, dSLDrr, cSLDrr, qSLDrr,
    bSSTor, wSSTor, iSSTor, fSSTor, dSSTor, cSSTor, qSSTor,
    bSSTrr, wSSTrr, iSSTrr, fSSTrr, dSSTrr, cSSTrr, qSSTrr,
    bLDor, wLDor, iLDor, fLDor, dLDor, cLDor, qLDor,
    bLDrr, wLDrr, iLDrr, fLDrr, dLDrr, cLDrr, qLDrr,
    bSTor, wSTor, iSTor, fSTor, dSTor, cSTor, qSTor,
    bSTrr, wSTrr, iSTrr, fSTrr, dSTrr, cSTrr, qSTrr,
    iADD, iSUB, iNEG, iMUL, iDIV, iADDI, iSUBI,
    fADD, fSUB, fNEG, fMUL, fDIV,
    dADD, dSUB, dNEG, dMUL, dDIV,
    cADD, cSUB, cNEG, cMUL, cDIV,
    qADD, qSUB, qNEG, qMUL, qDIV,
    JMPl, JMPr, JMPT, BR,
    iCMP, fCMP, dCMP, cCMP, qCMP,
    EQ, NE, LE, GE, LT, GT,
    iCMPeq, iCMPne, iCMPle, iCMPge, iCMPlt, iCMPgt, 
    fCMPeq, fCMPne, fCMPle, fCMPge, fCMPlt, fCMPgt, 
    dCMPeq, dCMPne, dCMPle, dCMPge, dCMPlt, dCMPgt, 
    cCMPeq, cCMPne,
    qCMPeq, qCMPne,
    BReq, BRne, BRle, BRge, BRlt, BRgt,
    b2b, b2w, b2i, b2f, b2d, b2c, b2q,
    w2b, w2w, w2i, w2f, w2d, w2c, w2q,
    i2b, i2w, i2i, i2f, i2d, i2c, i2q,
    f2i, f2f, f2d, f2c, f2q,
    d2i, d2f, d2d, d2c, d2q,
    c2i, c2f, c2d, c2c, c2q,
    q2i, q2f, q2d, q2c, q2q,
    cCOMPLEX, qCOMPLEX,
    fTRUNC, dTRUNC,
    fROUND, dROUND, fNINT, dNINT,
    iABS, fABS, dABS, cABS, qABS,
    iSL, iSLI, iSR, iSRI,
    lSHIFT, lSL, lSLI, lSR, lSRI,
    lAND, lOR, lNAND, lNOR, lEQV, lXOR, lNOT,
    iMOD, fMOD, dMOD,
    iSIGN, fSIGN, dSIGN,
    iDIM, fDIM, dDIM,
    dPROD,
    iMAX, fMAX, dMAX,
    iMIN, fMIN, dMIN,
    cIMAG, qIMAG,
    cCONJ, qCONJ,
    fSQRT, dSQRT, cSQRT, qSQRT,
    fEXP, dEXP, cEXP, qEXP,
    fLOG, dLOG, cLOG, qLOG,
    fLOG10, dLOG10,
    iPOW, fPOW, dPOW, cPOW, qPOW,
    fPOWi, dPOWi, cPOWi, qPOWi,
    fSIN, dSIN, cSIN, qSIN,
    fCOS, dCOS, cCOS, qCOS,
    fTAN, dTAN,
    fASIN, dASIN,
    fACOS, dACOS,
    fATAN, dATAN,
    fATAN2, dATAN2,
    fSINH, dSINH,
    fCOSH, dCOSH,
    fTANH, dTANH,
    iASRT, iASRTeq, fASRTeq, dASRTeq, cASRTeq, qASRTeq,
    iOUT, fOUT, dOUT,
    FETCHor, FETCHrr, FLUSHor, FLUSHrr, WHEREor, WHERErr,
    MCOPY,
    SP_ADD, iSP_STORE, fSP_STORE, dSP_STORE, iSP_LOAD, fSP_LOAD, dSP_LOAD,
    iFP_STORE, fFP_STORE, dFP_STORE, iFP_LOAD, fFP_LOAD, dFP_LOAD,
    REG2STACK, f2if, d2if,
    iKILL, fKILL, dKILL, iUSE, fUSE, dUSE,
    MACHi2i, MACHf2f, MACHd2d
    
  } names_of_opcodes;
typedef Unsigned_Int2 Opcode_Names;
typedef struct {
  Char *opcode;                 /* the spelling of the opcode */
  Opcode_Names opcode_name;     /* the index that will yield this entry;
                                        used for error checking */
  Int1 consts;                  /* the expected number of constants 
                                        (-n indicates at least
                                          n constants expected) */
  Int1 uses;                    /* the expect number of referenced registers */
  Int1 defs;                    /* the expected number of defined registers */
  Int1 lists;                   /* the expected number of expression lists */
  Def_Type def_type;            /* the type of defined registers */
  Unsigned_Int4 details;        /* bit vector specifying interesting details
                                        (see below) */
} Opcode;
#define NO_DETAILS      0
#define PSEUDO          1       /* pseudo-ops */
#define CONDITIONAL     (1<<1)  /* conditional branches */
#define CRITICAL        (1<<2)  /* must not be deleted by dead code elimination */
#define DATA            (1<<3)  /* marks data statements */
#define CALL            (1<<4)  /* JSRr and JSRl operations */
#define EXPR            (1<<5)  /* marks "expression" operations */
#define SCALAR          (1<<6)
#define LOAD            (1<<7)  /* operations that load from non-constant memory */
#define STORE           (1<<8)  /* operations that store to memory */
#define COMMUTE         (1<<9)  /* commutative operations */
#define COPY            (1<<10) /* register-to-register copies */
#define USES_TAGS       (1<<11)
#define DEFINES_TAGS    (1<<12)
#define ASSOCIATIVE     (1<<13) /* associative operations */
#define POINTER         (1<<14) /* loads from pointer-accessed memory */
#define SPARC_CALL      (1<<15) /* iloc intrinsics that turn into functions */
#define MAC_CALL        (1<<16) /* iloc intrinsics that turn into functions */
#define total_number_of_opcodes 403
extern Unsigned_Int number_of_opcodes;
extern Opcode opcode_specs[];

typedef struct {
  Opcode_Names opcode;
  Unsigned_Int2 comment;        /* index of optional comment */
  Expr source_line_ref;         /* reference back to the original program source */
  Unsigned_Int1 constants;      /* the number of constant actually found */
  Unsigned_Int1 referenced;     /* constants + references */
  Unsigned_Int1 defined;        /* constants + references + defs */
  Boolean critical;             /* true if operation is not dead */
  Unsigned_Int2 arguments[3];   /* an array of arguments (expressions and register numbers) */
} Operation;
#define Operation_Allocate(arena, args) \
  Arena_GetMem(arena, sizeof(Operation) + (args-3)*sizeof(Unsigned_Int2))
#define Operation_ForAllConstants(p, op)        \
   for (p = &(op)->arguments[0];                \
        p < &(op)->arguments[(op)->constants];  \
        p++)

#define Operation_ForAllUses(p, op)             \
   for (p = &(op)->arguments[(op)->constants];  \
        p < &(op)->arguments[(op)->referenced]; \
        p++)

#define Operation_ForAllDefs(p, op)             \
   for (p = &(op)->arguments[(op)->referenced]; \
        p < &(op)->arguments[(op)->defined];    \
        p++)

#define Operation_ForAllRefTags(p, op)                         \
    for(p = (opcode_specs[(op)->opcode].details & USES_TAGS) ? \
                 &(op)->arguments[(op)->defined] :           \
                 NULL;                                                     \
        p && *p != 0;                                            \
        p++)

extern Unsigned_Int2 *Operation_Second_List_Start(Operation *);

#define Operation_ForAllDefTags(p, op)                            \
    for(p = (opcode_specs[(op)->opcode].details & DEFINES_TAGS) ? \
                 Operation_Second_List_Start((op)) :                   \
                 NULL;                                                        \
        p && *p != 0;                                               \
        p++)
Unsigned_Int Operation_Def_Type(Operation *, Unsigned_Int);

typedef struct inst_node Inst;

struct inst_node {
  Inst *prev_inst;
  Inst *next_inst;
  Operation *operations[1];
};
#define Inst_Allocate(arena, ops) \
  Arena_GetMem(arena, sizeof(Inst) + ops * sizeof(Operation *))
#define Inst_ForAllOperations(op, inst) \
   for (op = &(inst)->operations[0]; *op; op++)

typedef struct block Block;
typedef struct edge Edge;
typedef struct label_node Label;

struct label_node {
  Label *next;
  Expr label;
};
Char *Label_Get_String(Expr label);
Block *Label_Get_Destination(Expr label);
Boolean Expr_Is_Block_Label(Expr expression);
typedef struct dom_node DomNode;
typedef struct block_extension Block_Extension;
typedef struct edge_extension Edge_Extension;
struct block {
  Label *labels;                  /* list of labels for the block */
  Inst *inst;                     /* points to head of circularly-linked list of
                                     instructions in the block */
  Edge *pred;                     /* list of incoming edges */
  Edge *succ;                     /* list of outgoing edges */
  Unsigned_Int2 preorder_index;   /* this block's position in preorder_block_list */
  Unsigned_Int2 postorder_index;  /* this block's position in postorder_block_list */
  Unsigned_Int2 pred_count;       /* number of predecessors in the graph */
  DomNode *dom_node;              /* forward dominator information */
  DomNode *post_dom_node;         /* reverse dominator info */
  Boolean visited;                /* occasionally used during traversals of the graph */
  Unsigned_Int2 descendant_count; /* number of descendants in the depth-first
                                     spanning tree (used to find loops) */
  Block_Extension *block_extension;
};
struct edge {
  Edge *next_pred;      /* rest of the predecessor list for the target block */
  Edge *next_succ;      /* rest of the successor list for the source block */
  Block *pred;          /* source of the edge */
  Block *succ;          /* target of the edge */
  Boolean back_edge;    /* TRUE if this is a back edge */
  Edge_Extension *edge_extension;
};
typedef struct block_node Block_List_Node;
struct block_node {
  Block_List_Node *next;
  Block *block;
};
#define Block_ForAllInsts(instruction, block)           \
   for (instruction = (block)->inst->next_inst;         \
        instruction != (block)->inst;                   \
        instruction = instruction->next_inst)

#define Block_ForAllInstsReverse(instruction, block)    \
   for (instruction = (block)->inst->prev_inst;         \
        instruction != (block)->inst;                   \
        instruction = instruction->prev_inst)
#define Block_ForAllPreds(edge, block)          \
   for (edge = (block)->pred;                   \
        edge;                                   \
        edge = edge->next_pred)

#define Block_ForAllSuccs(edge, block)          \
   for (edge = (block)->succ;                   \
        edge;                                   \
        edge = edge->next_succ)
void Block_Dump(Block *block, void (*extension_printer)(Block *), Boolean);

void Block_Init(Char *file_name);
extern Boolean insert_landing_pads;
extern Boolean insert_edge_splits;
extern Boolean keep_comments;
extern Boolean replace_source_line_ref_with_line_count;
extern Boolean replace_source_line_ref_with_character_count;
extern Block *start_block;
extern Block *end_block;
extern Arena block_memory_bank;
extern Unsigned_Int block_count;
extern Unsigned_Int edge_count;
extern Unsigned_Int register_count; /* largest register plus one */
  extern Unsigned_Int tag_count;/* total number of tags*/
extern Block **preorder_block_list;
extern Block **postorder_block_list;
#define ForAllBlocks_Preorder(b)                        \
   for (b=preorder_block_list[1];                       \
        b;                                              \
        b = preorder_block_list[b->preorder_index+1])

#define ForAllBlocks_rPreorder(b)                       \
   for (b=preorder_block_list[block_count];             \
        b;                                              \
        b = preorder_block_list[b->preorder_index-1])

#define ForAllBlocks_Postorder(b)                       \
   for (b=postorder_block_list[1];                      \
        b;                                              \
        b = postorder_block_list[b->postorder_index+1])

#define ForAllBlocks_rPostorder(b)                      \
   for (b=postorder_block_list[block_count];            \
        b;                                              \
        b = postorder_block_list[b->postorder_index-1])
#define ForAllBlocks(b) ForAllBlocks_rPostorder(b)
extern Block **preorder_subgraph_list;
extern Block **postorder_subgraph_list;
typedef struct subgraph_points {
    Unsigned_Int front;
    Unsigned_Int back;
} Subgraph_Points;
extern Subgraph_Points **subgraph_points;
#define ForSubgraph_Preorder(i, b, start)                           \
   for (i = subgraph_points[start->preorder_index]->front, b=start; \
        b;                                                          \
        b = preorder_subgraph_list[++i])

#define ForSubgraph_rPreorder(i, b, start)                          \
   for (i = subgraph_points[start->preorder_index]->back,           \
        b=preorder_subgraph_list[subgraph_points[start->preorder_index]->back];\
        b;                                                          \
        b = preorder_subgraph_list[--i])

#define ForSubgraph_Postorder(i, b, start)                          \
   for (i = subgraph_points[start->preorder_index]->front,          \
        b=postorder_subgraph_list[subgraph_points[start->preorder_index]->front]; \
        b;                                                          \
        b = postorder_subgraph_list[++i])

#define ForSubgraph_rPostorder(i, b, start)                         \
   for (i = subgraph_points[start->preorder_index]->back,           \
        b=postorder_subgraph_list[subgraph_points[start->preorder_index]->back]; \
        b;                                                          \
        b = postorder_subgraph_list[--i])
void Block_Order(void);
void Block_Add_Edge(Block * source_block, Block * dest_block);
void Block_Put_All(File);
extern Boolean print_all_operations;
void Block_Dump_All(void (*extension_printer)(Block *), Boolean);
void Block_Edge_Dump(void (*extension_printer)(Edge *));
Char *Block_Print_Name(Block *block);
void Dominator_CalcDom(Arena,Boolean);
void Dominator_CalcPostDom(Arena,Boolean);
void Block_Find_Infinite_Loops(void);
struct dom_node {
  Block *parent; /* a pointer to the parent in the dominator tree,
                                NULL if the root of the tree */
  Block_List_Node *children; /* a list of the children in the dominator
                                tree */
  Block_List_Node *frontier; /* a list of the blocks in this block's
                                dominance frontier */
};
#define Dominator_ForChildren(b, node) \
   for (b = (node)->children;          \
        b;                             \
        b = b->next)

#define Dominator_ForFrontier(b, node) \
   for (b = (node)->frontier;          \
        b;                             \
        b = b->next)
void Dominator_Dump(Block *root, Boolean forward_flag);
Int Dominator_Walk_Preparation(void);
Int Post_Dominator_Walk_Preparation(void);
extern Block **dominator_preorder_block_list;
extern Block **dominator_postorder_block_list;
extern Block **postdominator_preorder_block_list;
extern Block **postdominator_postorder_block_list;
/* Dominator_ForAllBlocks_Preorder(int i;, Block *b) */
#define Dominator_ForAllBlocks_Preorder(i,b)                                  \
   if (!dominator_preorder_block_list) Dominator_Walk_Preparation();          \
   for (i=1,b=dominator_preorder_block_list[i];                               \
        b;                                                                    \
        b=dominator_preorder_block_list[++i])

/* Dominator_ForAllBlocks_Postorder(int i;, Block *b) */
#define Dominator_ForAllBlocks_Postorder(i,b)                                 \
   if (!dominator_postorder_block_list) Dominator_Walk_Preparation();         \
   for (i=1, b=dominator_postorder_block_list[i];                             \
        b;                                                                    \
        b=dominator_postorder_block_list[++i])

/* Post_Dominator_ForAllBlocks_Preorder(int i;, Block *b) */
#define Post_Dominator_ForAllBlocks_Preorder(i,b)                             \
   if (!postdominator_preorder_block_list) Post_Dominator_Walk_Preparation(); \
   for (i=1, b=postdominator_preorder_block_list[i];                          \
        b;                                                                    \
        b=postdominator_preorder_block_list[++i])

/* Post_Dominator_ForAllBlocks_Postorder(int i;, Block *b) */
#define Post_Dominator_ForAllBlocks_Postorder(i,b)                            \
   if (!postdominator_postorder_block_list) Post_Dominator_Walk_Preparation();\
   for (i=1, b=postdominator_postorder_block_list[i];                         \
        b;                                                                    \
        b=postdominator_postorder_block_list[++i])

/* Dominator_ForAllBlocks_rPreorder(int i;, Block *b) */
#define Dominator_ForAllBlocks_rPreorder(i,b)                                 \
   if (!dominator_preorder_block_list) Dominator_Walk_Preparation();          \
   for (i=block_count,b=dominator_preorder_block_list[i];                   \
        b;                                                                    \
        b=dominator_preorder_block_list[--i])

/* Dominator_ForAllBlocks_rPostorder(int i;, Block *b) */
#define Dominator_ForAllBlocks_rPostorder(i,b)                                \
   if (!dominator_postorder_block_list) Dominator_Walk_Preparation();         \
   for (i=block_count, b=dominator_postorder_block_list[i];                 \
        b;                                                                    \
        b=dominator_postorder_block_list[--i])

/* Post_Dominator_ForAllBlocks_rPreorder(int i;, Block *b) */
#define Post_Dominator_ForAllBlocks_rPreorder(i,b)                            \
   if (!postdominator_preorder_block_list) Post_Dominator_Walk_Preparation(); \
   for (i=block_count, b=postdominator_preorder_block_list[i];              \
        b;                                                                    \
        b=postdominator_preorder_block_list[--i])

/* Post_Dominator_ForAllBlocks_rPostorder(int i;, Block *b) */
#define Post_Dominator_ForAllBlocks_rPostorder(i,b)                           \
   if (!postdominator_postorder_block_list) Post_Dominator_Walk_Preparation();\
   for (i=block_count, b=postdominator_postorder_block_list[i];             \
        b;                                                                    \
        b=postdominator_postorder_block_list[--i])

extern Boolean conserve_BLOCKID;
/* holds each static code operation, which is any of : xDATA, BYTES, or the
   NAME statement; the next and prev pointers are used to form doubly linked,
   circular lists of operations */
typedef struct static_code_node
{
    Opcode_Names op_name;
    Expr expression;
    Expr repetition_factor; /* set to positive integer for xDATA statements,
                               zero for other static code statements */
    struct static_code_node *next_operation;
    struct static_code_node *prev_operation;

} Static_Code_Node, *Static_Code_Node_Ptr;
/* returns the address of the opcode name (specified by the opcode_names
   enumerated type in opcode.h) from the opcode array (above) */
Opcode *Opcode_Get_Address(Opcode_Names opcode_name);

/* holds a linked list of operations; the next and prev pointers are used to
    form doubly linked, circular lists of operation lists */
typedef struct static_code_list
{
    Static_Code_Node* operation_list;
    Label* label;
    struct static_code_list *next_list;
    struct static_code_list *prev_list;

} Static_Code_List;
extern Static_Code_List *static_code_list;
typedef Unsigned_Int2 Tag;
typedef Unsigned_Int2 Comment_Val;
extern Static_Code_List *NAME_list;
#define FILL_CHAR 0xda
/* this is used by Arena.c; I'm not sure if it matters to anyone else... */
extern Opcode_Names *op_list;
extern Unsigned_Int line_number;
Char *Comment_Get_String(Comment_Val);
Label *Label_Invent(void);

void Block_Insert_Instruction(Inst *new_inst, Inst *current_inst);
Block *Block_Build_Drone_Block();

/* Creation date: 9/28/94 TJHARVEY */
/* this module contains the calls necessary to support the linked-list
   data structure used to hole the information from the STACK and GLOBAL
   statements. */

/* this data structure is used to support both GLOBAL and STACK statements;
   since STACK statements don't include the global_label, this value will
   be zero for nodes representing STACK statements */
typedef struct stack_def_node_type Stack_Def_Node;
struct stack_def_node_type{
    Opcode_Names opcode;
    Expr name;
    Expr offset;
    Expr global_label;
    Boolean is_scalar;
    Stack_Def_Node *next;
};

extern Stack_Def_Node *list_of_STACKs;
extern Stack_Def_Node *list_of_GLOBALs;

/* This routine adds a new node onto a list of nodes */
/* for STACK statements, the value sent in for the parameter global_label
   should be zero */
extern void Stack_Def_Insert(Stack_Def_Node **list, Opcode_Names opcode,
                             Expr offset, Expr name, Expr global_label);

/* This routine prints out all of the nodes in the list of nodes */
extern void Stack_Def_Put_All(File output_file, Stack_Def_Node *list);

/* This macro simulates a C for-loop and is analogous to the other
   loops such as ForAllBlocks */
/* Usage: Stack_Def_ForAllNodes(Stack_Def_Node *runner, Stack_Def_Node *list)*/
#define Stack_Def_ForAllNodes(runner, list) \
    for (runner=list; runner; runner = runner->next)

typedef struct xfunc_node Xfunc_Node;
struct xfunc_node {
    Opcode_Names opcode;
    Expr function_name;
    Xfunc_Node *next;
};
extern Xfunc_Node *list_of_function_names;

/* returns a new tag name which is guaranteed to be unique */
Expr Tag_Create(void);

/* this data structure holds the alias names for a tag in a linked list */
typedef struct alias_node Alias_Node;
struct alias_node
{
    Tag alias_name;
    Alias_Node *next;

};



/* this data structure holds the original tag name; it's set up to exist
   in a hash table (thus, the "next" pointer), and to have hanging off of
   it a list of the tag's aliases; the "alias_list_back" pointer is so
   that we can enter the aliases into the list in the same order that
   we read them in, while the "alias_list_front" is so that we can
   output them in the same order that we read them in */
typedef struct tag_node Tag_Node;
struct tag_node
{
    Tag tag_name;
    Unsigned_Int2 alias_count;
    Boolean accessed; /* initialized to FALSE on creation of a new tag_node;
                         gets set to TRUE when the alias list is accessed
                         by an executable operation - note that (currently)
                         this is used only by the parser */
    Boolean added_after_accessed; /* initialized to FALSE on creation of
                                     a new tag node; gets set to TRUE if
                                     an alias is added for this tag after
                                     an executable operation has referenced
                                     it */
    Alias_Node *alias_list_front;
    Alias_Node *alias_list_back;
    Tag_Node *next;
    /* "next" pointer for the hash bucket */
    Tag_Node *canonical_next;
    /* "next" pointer for our global list of values; this linked list
       hooks together all of the tag_nodes so that we can print them
       out easily */

};



/* supercedes the Alias_Store_Tag call; this does all of the allocation
   and entry into the hash table automatically; if the tag already
   has an entry in hash table, the pointer to this data structure is
   returned */
Tag_Node *Alias_Install(Tag tag_name);


/* adds an "alias_node" onto the tag's list */
void Alias_Add(Tag_Node *tag, Tag alias);
/* this for loop runs through the list of aliases for a tag */
/*
 * Alias_ForAllAliases(Alias_Node *next_alias_node, Tag_Node *tag_node)
 */
#define Alias_ForAllAliases(next_alias, tag_node) \
    for (next_alias = tag_node->alias_list_front; \
         next_alias != NULL;                      \
         next_alias = next_alias->next)

/* returns a pointer to the tag node */
Tag_Node *Alias_Retrieve_Tag(Tag tag_name);

/* this is used by the all-in-one compiler to reset the
     tag counter when the number of registers changes */
Unsigned_Int Tag_Get_Tag_Offset(void);


Comment_Val Comment_Install(Char *comment_string); //for c++ to see
#ifdef __cplusplus
};
#endif /*__cpluplus */
#endif /* SHARED */
