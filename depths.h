#include <Shared.h>
#include <SSA.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct
{
    Unsigned_Int2 dom_index;
    Unsigned_Int2 dom_size;
    Unsigned_Int2 dom_level;
    Unsigned_Int2 dfs_index;
    Unsigned_Int2 dfs_size;
    Unsigned_Int2 loop_head;
    struct linked_list *children;
} DJ_GRAPH_INFO;

typedef struct
{
    Boolean visited;
    Boolean in_stack;
    Unsigned_Int2 dfs_num;
    Unsigned_Int2 low;
    Unsigned_Int2 next;
} SCC_INFO;

typedef struct linked_list Linked_List;
struct linked_list {
    Unsigned_Int name;
    Linked_List *next;
};

extern Unsigned_Int max_depth;
extern void find_nesting_depths(Arena external_arena);

#define MIN(x, y)             (((x) < (y)) ? (x) : (y))
