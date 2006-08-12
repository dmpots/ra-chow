#include "ra.h"
static Arena depth_arena;
static DJ_GRAPH_INFO *DJ_graph_info;
static Unsigned_Int max_level = 0;
static Linked_List **levels;
static SCC_INFO *scc_info;
static Unsigned_Int dfs_stack;
static Unsigned_Int next_dfs_num;

static Unsigned_Int walk_tree(Block *block, Unsigned_Int *index,
                              Unsigned_Int level);
static Boolean dominates(Block *parent, Block *child);
static Unsigned_Int DFS_DJ_graph(Block *block, Unsigned_Int *index);
static Boolean sp_back_edge(Block *parent, Block *child);
static Void reach_under(Block *block, Block *head, SparseSet loop);
static Void dfs(Unsigned_Int i);
static Void visit_successors(Unsigned_Int pred, Unsigned_Int i);

//added by dave
static Void Block_Clear_Visited_Flags();
Unsigned_Int max_depth = 0;
Unsigned_Int debug = TRUE;

Void find_nesting_depths(Arena external_arena)
{
    depths = (Unsigned_Int*)
        Arena_GetMemClear(external_arena,
                          (block_count+1)*sizeof(Unsigned_Int2));
    depth_arena = Arena_Create();
    {
        Unsigned_Int index = 1;
        Unsigned_Int level = 0;
        DJ_graph_info = (DJ_GRAPH_INFO*)
            Arena_GetMemClear(depth_arena, (block_count+1)*sizeof(DJ_GRAPH_INFO));
        
        Dominator_CalcDom(depth_arena, FALSE);
        walk_tree(start_block, &index, level);
        {
            Block *block;
        
            levels = (Linked_List**)
                Arena_GetMemClear(depth_arena, (max_level+1)*sizeof(Linked_List *));
            ForAllBlocks(block)
            {
                Unsigned_Int block_num = block->preorder_index;
                Unsigned_Int level = DJ_graph_info[block_num].dom_level;
                Linked_List *node = (Linked_List*)
                  Arena_GetMem(depth_arena, sizeof(Linked_List));
                node->name = block_num;
                node->next = levels[level];
                levels[level] = node;
            }
        }
        
    }
    
    {
        Unsigned_Int index = 1;
        Block_Clear_Visited_Flags();
        DFS_DJ_graph(start_block, &index);
    }
    
    {
        Int i;
        SparseSet loop = SparseSet_Create(depth_arena, block_count+1);
        for (i = max_level; i >= 0; i--)
        {
            Boolean irreducible_loop = FALSE;
            Linked_List *node;
            for (node = levels[i]; node; node = node->next)
            {
                Block *block = preorder_block_list[node->name];
                Edge *edge;
                SparseSet_Clear(loop);
                Block_ForAllPreds(edge, block)
                {
                    Block *pred = edge->pred;
                    Unsigned_Int pred_index = pred->preorder_index;
                    if (pred != block->dom_node->parent)
                    {
                        if (!dominates(block, pred) && sp_back_edge(block, pred))
                            irreducible_loop = TRUE;
                        if (dominates(block, pred))
                            reach_under(pred, block, loop);
                    }
                }
                if (SparseSet_Size(loop))
                    {
                        Unsigned_Int block_index = block->preorder_index;
                        Unsigned_Int i;
                        SparseSet_ForAll(i, loop)
                        {
                            Unsigned_Int depth = ++(depths[i]);
                            if (depth > max_depth) max_depth = depth;
                    
                            if (!DJ_graph_info[i].loop_head || DJ_graph_info[i].loop_head == i)
                            {
                                DJ_graph_info[i].loop_head = block_index;
                                if (i != block_index)
                                {
                                    Linked_List *node = (Linked_List*)
                                        Arena_GetMem(depth_arena, sizeof(Linked_List));
                                    node->name = i;
                                    node->next = DJ_graph_info[block_index].children;
                                    DJ_graph_info[block_index].children = node;
                                }
                            }
                        }
                    }
                    
                if (irreducible_loop)
                    {
                        Arena_Mark(depth_arena);
                        scc_info = (SCC_INFO*)
                          Arena_GetMemClear(depth_arena, (block_count+1)*sizeof(SCC_INFO));
                        next_dfs_num = 0;
                        
                        {
                            Unsigned_Int j;
                            for (j = i; j <= max_depth; j++)
                            {
                                Linked_List *node;
                                for (node = levels[j]; node; node = node->next)
                                {
                                    Unsigned_Int index = node->name;
                                    if (!DJ_graph_info[index].loop_head && !scc_info[index].visited)
                                        dfs(i);
                                }
                            }
                        }
                        
                        Arena_Release(depth_arena);
                    }
                    
            }
        }
    }
    
    if (debug)
    {
        Block *block;
        ForAllBlocks(block)
        {
            fprintf(stderr, "Depth is %d for block ",
                    depths[block->preorder_index]);
            Block_Print_Name(block);
        }
        ABORT;
    }
    
    Arena_Destroy(depth_arena);
}
static Unsigned_Int walk_tree(Block *block, Unsigned_Int *index,
                              Unsigned_Int level)
{
    Unsigned_Int size = 1;
    Unsigned_Int preorder_index = block->preorder_index;
    Unsigned_Int dom_index = (*index)++;
    Block_List_Node *child;

    DJ_graph_info[preorder_index].dom_index = dom_index;
    DJ_graph_info[preorder_index].dom_level = level;
    if (level > max_level) max_level = level;
    Dominator_ForChildren(child, block->dom_node)
        size += walk_tree(child->block, index, level + 1);

    return DJ_graph_info[preorder_index].dom_size = size;
}
static Boolean dominates(Block *parent, Block *child)
{
    Unsigned_Int p_index = DJ_graph_info[parent->preorder_index].dom_index;
    Unsigned_Int p_size = DJ_graph_info[parent->preorder_index].dom_size;
    Unsigned_Int c_index = DJ_graph_info[child->preorder_index].dom_index;

    return p_index <= c_index && c_index < p_index + p_size;
}
static Unsigned_Int DFS_DJ_graph(Block *block, Unsigned_Int *index)
{
    Unsigned_Int size = 1;
    Unsigned_Int preorder_index = block->preorder_index;
    Block_List_Node *child;
    Edge *succ;

    block->visited = TRUE;
    DJ_graph_info[preorder_index].dfs_index = (*index)++;
    Dominator_ForChildren(child, block->dom_node)
        size += DFS_DJ_graph(child->block, index);
    Block_ForAllSuccs(succ, block)
    {
        Block *next_block = succ->succ;
        if (!next_block->visited)
            size += DFS_DJ_graph(next_block, index);
    }

    return DJ_graph_info[preorder_index].dfs_size = size;
}
static Boolean sp_back_edge(Block *parent, Block *child)
{
    Unsigned_Int p_index = DJ_graph_info[parent->preorder_index].dfs_index;
    Unsigned_Int p_size = DJ_graph_info[parent->preorder_index].dfs_size;
    Unsigned_Int c_index = DJ_graph_info[child->preorder_index].dfs_index;

    return p_index <= c_index && c_index < p_index + p_size;
}
static Void reach_under(Block *block, Block *head, SparseSet loop)
{
    SparseSet worklist;

    Arena_Mark(depth_arena);
    worklist = SparseSet_Create(depth_arena, block_count+1);
    SparseSet_Insert(loop, block->preorder_index);
    SparseSet_Insert(loop, head->preorder_index);
    {
        Edge *edge;
        Block_ForAllPreds(edge, block)
        {
            Block *pred = edge->pred;
            Unsigned_Int pred_index = pred->preorder_index;
            if (dominates(head, pred))
                SparseSet_Insert(worklist, pred_index);
        }
    }
    
    while (SparseSet_Size(worklist))
    {
        Unsigned_Int index = SparseSet_ChooseMember(worklist);
        SparseSet_Delete(worklist, index);
        if (!SparseSet_Member(loop, index))
        {
            Edge *edge;
            SparseSet_Insert(loop, index);
            Block_ForAllPreds(edge, preorder_block_list[index])
            {
                Block *pred = edge->pred;
                Unsigned_Int pred_index = pred->preorder_index;
                if (dominates(head, pred))
                    SparseSet_Insert(worklist, pred_index);
            }
        }
        
    }
    Arena_Release(depth_arena);
}
static Void dfs(Unsigned_Int i)
{
    scc_info[i].visited = TRUE;
    scc_info[i].dfs_num = scc_info[i].low = next_dfs_num++;
    scc_info[i].in_stack = TRUE;
    scc_info[i].next = dfs_stack;
    dfs_stack = i;
    
    visit_successors(i, i);
    if (scc_info[i].low == scc_info[i].dfs_num)
    {
        Unsigned_Int name = dfs_stack;
        if (i == name)
            {
                Unsigned_Int name = dfs_stack;
                scc_info[name].in_stack = FALSE;
                dfs_stack = scc_info[name].next;
            }
            
        else
        {
            Unsigned_Int scc = 0;
            do
            {
                name = dfs_stack;
                {
                    Unsigned_Int name = dfs_stack;
                    scc_info[name].in_stack = FALSE;
                    dfs_stack = scc_info[name].next;
                }
                
                scc_info[name].next = scc;
                scc = name;
            } while (name != i);
            {
                Unsigned_Int name;
                for (name = scc; name; name = scc_info[name].next)
                {
                    Unsigned_Int depth = ++(depths[i]);
                    if (depth > max_depth) max_depth = depth;
            
                    if (!DJ_graph_info[name].loop_head || DJ_graph_info[i].loop_head == name)
                    {
                        DJ_graph_info[name].loop_head = i;
                        if (name != i)
                        {
                            Linked_List *node = (Linked_List*)
                                Arena_GetMem(depth_arena, sizeof(Linked_List));
                            node->name = name;
                            node->next = DJ_graph_info[i].children;
                            DJ_graph_info[i].children = node;
                        }
                    }
                }
            }
            
        }
    }
    
}
static Void visit_successors(Unsigned_Int pred, Unsigned_Int i)
{
    Block *block = preorder_block_list[i];
    Edge *edge;
    Linked_List *node;
    Block_ForAllSuccs(edge, block)
    {
        Unsigned_Int succ_index = edge->succ->preorder_index;
        if (!DJ_graph_info[succ_index].loop_head)
        {
            if (!scc_info[succ_index].visited)
            {
                dfs(succ_index);
                scc_info[pred].low =
                    MIN(scc_info[pred].low, scc_info[succ_index].low);
            }

            if (scc_info[succ_index].dfs_num < scc_info[pred].dfs_num &&
                scc_info[succ_index].in_stack)
                scc_info[pred].low =
                    MIN(scc_info[succ_index].dfs_num, scc_info[pred].low);
        }
    }
    for (node = DJ_graph_info[i].children; node; node = node->next)
        visit_successors(pred, node->name);
}

//added by dave... where was this originally?
static Void Block_Clear_Visited_Flags()
{
  Block* blk;
  ForAllBlocks(blk)
  {
    blk->visited = FALSE;
  }
}
