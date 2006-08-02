#ifndef SSA
#define SSA 1

#ifdef __cplusplus
extern "C" {
#endif /*__cpluplus */

#include <Shared.h>
typedef struct phi_node Phi_Node;
struct phi_node
{
    Phi_Node *next;
    Unsigned_Int2 old_name;
    Unsigned_Int2 new_name;
    Unsigned_Int2 def_type; /* values defined in Shared.h */
    Boolean useless;
    Unsigned_Int2 parms[1];
};
typedef struct liveness_info Liveness_Info;
struct liveness_info {
    Unsigned_Int size;
    Unsigned_Int *names;
}; /* liveness_info */
typedef struct chain_type Chain;
struct chain_type
{
    union
    {
        Operation *operation;
        Phi_Node *phi_node;
    } op_pointer;
    Boolean is_phi_node;
};
typedef struct chains_list_type Chains_List;
struct chains_list_type
{
    Chain chain;
    Block *block;
    Unsigned_Int position;
    Chains_List *next;
};
typedef struct parm_map_node {
    Unsigned_Int index;
    Block *succ_block;
    struct parm_map_node *next;
} Parm_Map_Node;

extern Phi_Node **SSA_phi_nodes;
extern Edge ***SSA_edge_maps;
extern Unsigned_Int SSA_def_count;
extern Unsigned_Int2 *SSA_name_map;
extern Block **SSA_block_map;
extern Liveness_Info *SSA_live_in;
extern Liveness_Info *SSA_live_out;
extern Inst **SSA_inst_map;
extern Chain *SSA_use_def_chains;
extern Chains_List **SSA_def_use_chains;
extern Boolean SSA_CFG_renumbered;
extern Parm_Map_Node **parm_map;

#define SSA_MINIMAL      1<<0
#define SSA_SEMI_PRUNED  1<<1
#define SSA_PRUNED       1<<2
#define SSA_BUILD_DEF_USE_CHAINS   1<<3
#define SSA_BUILD_USE_DEF_CHAINS   1<<4
#define SSA_FOLD_COPIES  1<<5
#define SSA_FOLD_USELESS_PHI_NODES  1<<6
#define SSA_IGNORE_TAGS 1<<8

#define SSA_CONSERVE_LIVE_IN_INFO 1<<9
#define SSA_CONSERVE_LIVE_OUT_INFO 1<<10
#define SSA_PRINT_WARNING_MESSAGES 1<<7

/* Block_ForAllPhiNodes(Phi_Node *phi_node, Block *block) */
#define Block_ForAllPhiNodes(phi_node, block)                     \
        for (phi_node=SSA_phi_nodes[(block)->preorder_index];     \
             phi_node;                                            \
             phi_node=phi_node->next)
/* Phi_Node_ForAllParms(Unsigned_Int2 *runner, Phi_Node *phi_node) */
#define Phi_Node_ForAllParms(runner, phi_node)                                             \
        for (runner = &((phi_node)->parms[0]);                                             \
             runner < (phi_node)->parms + SSA_block_map[(phi_node)->new_name]->pred_count; \
             runner++)
/* Phi_Node_ForAllUpdatedParms(Unsigned_Int2 *runner,
                               Phi_Node *phi_node, Unsigned_Int parm_count) */
#define Phi_Node_ForAllUpdatedParms(runner, phi_node, parm_count)  \
        for (runner = &((phi_node)->parms[0]);                     \
             runner < (phi_node)->parms + parm_count ;             \
             runner++)
 /* Chain_ForAllUses(Chains_List *runner, Unsigned_Int resource_name) */
#define Chain_ForAllUses(runner, resource_name) \
    for (runner = SSA_def_use_chains[(resource_name)]; runner; runner = runner->next)
extern Void SSA_Build(Unsigned_Int flags);
extern Void SSA_Replace_Phi_Nodes(Void);
extern Void SSA_Restore(Void);
Void SSA_Phi_Node_Dump(Void);
Void print_phi_nodes(Block *block);
Void print_def_use_chains(Void);
Void print_use_def_chains(Void);

#ifdef __cplusplus
};
#endif /*__cpluplus */
#endif /* SSA */
