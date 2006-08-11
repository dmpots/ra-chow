
#line 46 "ra.w"

#line 61 "ra.w"
#include <Shared.h>
#include <SSA.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
/* these two let me use getuid */
#include <sys/types.h>
#include <unistd.h>

#line 814 "ra.w"
#include <sys/fcntl.h>

#line 46 "ra.w"


#line 156 "ra.w"
#define machine_registers 32

#line 733 "rematerialize.inc"
#define MIN(x, y)             (((x) < (y)) ? (x) : (y))

#line 190 "interference_graph.inc"
/* Vector_ForAllNodes(Edge_Vector *runner, Edge_Vector *vector)*/
#define Vector_ForAllNodes(runner, vector) \
        for((runner) = (vector); (runner); (runner)=(runner)->next)

#line 47 "ra.w"


#line 45 "depths.inc"
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

#line 317 "depths.inc"
typedef struct
{
    Boolean visited;
    Boolean in_stack;
    Unsigned_Int2 dfs_num;
    Unsigned_Int2 low;
    Unsigned_Int2 next;
} SCC_INFO;

#line 302 "ra.w"
typedef struct
{
    Expr *stack_frame_expr;         /* points into the FRAME oper */
    Unsigned_Int stack_frame;       /* size of the stack frame */
    Unsigned_Int stack_pointer;     /* SSA name of the stack pointer */
    Unsigned_Int static_data_area;  /* SSA name of the static data area */
} Frame;

#line 36 "rematerialize.inc"
typedef enum  {TOP = 0, CONSTANT = -1, BOTTOM = -2} Lattice_Val;
typedef struct lattice Lattice;
struct lattice {
    Lattice_Val lattice_val;
    Operation *oper;       /* meaningful when lattice_val == CONSTANT */
};

#line 576 "rematerialize.inc"
typedef struct
{
    Unsigned_Int2 dom_index;
    Unsigned_Int2 dom_size;
} DOMINATOR_INFO;

#line 618 "rematerialize.inc"
typedef struct
{
    Boolean visited;
    Boolean in_stack;
    Unsigned_Int2 dfs_num;
    Unsigned_Int2 low;
    Unsigned_Int2 next;
    Block *iv_block;
    Boolean constant;
} DFS_INFO;

#line 998 "rematerialize.inc"
typedef struct iv_info
{
    Expr initial_val;
    Expr increment_expr;
    Unsigned_Int increment_reg;
    Unsigned_Int scc;
    Boolean is_base_iv;
    struct iv_info *base_iv;
    Expr offset;
    Expr multiplier;
    struct iv_info *next;
} IV_INFO;

#line 1257 "rematerialize.inc"
typedef struct
{
    Lattice_Val lattice_val;
    IV_INFO *entry;
} IV_LATTICE;

#line 162 "interference_graph.inc"
typedef struct edge_vector Edge_Vector;
struct edge_vector {
    Unsigned_Int edge_count;
    Edge_Vector *next;
    Unsigned_Int *edges;
};

#line 49 "coalesce.inc"
typedef struct linked_list Linked_List;
struct linked_list {
    Unsigned_Int name;
    Linked_List *next;
};

#line 126 "spill.inc"
typedef struct range
{
    Double loads;
    Double stores;
    Double copies;
    Double cost;
    Boolean infinite;
} Range;

#line 291 "spill.inc"
typedef struct partner
{
    Unsigned_Int partner;
    Double cost;
    struct partner *next;
} Partner;

#line 66 "splits.inc"
typedef struct
{
    Double loads;
    Double stores;
} Split_Range;

#line 77 "bergner.inc"
typedef struct
{
    Double loads;
    Double stores;
    Double copies;
    Double cost;
    Boolean infinite;
    Boolean spill_entirely;
    Unsigned_Int2 spill_color;
    Unsigned_Int2 num_live_neighbors;
} IR_Range;

#line 625 "bergner.inc"
typedef struct delay_info
{
    Frame *frame;
    Inst *inst;
    Unsigned_Int name;
    struct delay_info *next;
} Delay_Info;

#line 52 "avail.inc"
typedef struct
{
    Double loads;
    Double stores;
    Double copies;
    Double cost;
    Boolean infinite;
    Boolean spill_entirely;
    Unsigned_Int2 spill_color;
    Unsigned_Int2 num_live_neighbors;
} AVAIL_Range;

#line 85 "avail.inc"
struct block_extension
{
    Boolean dirty;
    VectorSet avail_load;
    VectorSet gen;
    VectorSet killed;
    VectorSet avin;
    VectorSet avout;
};

#line 595 "avail.inc"
typedef struct
{
    Inst *inst;
    Operation *oper;
} Location;

#line 42 "iterated.inc"
typedef struct
{
    Inst *inst;
    Operation *oper;
    Double cost;
} COPY_INFO;

#line 48 "ra.w"


#line 110 "ra.w"
extern Timer my_clock;

#line 160 "ra.w"
extern Unsigned_Int num_registers;
extern Unsigned_Int total_registers;
extern Boolean single_register_set;

#line 172 "ra.w"
extern Boolean negative_spill_only;
extern Unsigned_Int debug;
extern Boolean write_tables;
extern Boolean print_at_end_of_iteration;

#line 11 "depths.inc"
extern Unsigned_Int max_depth;
extern Unsigned_Int2 *depths;

#line 313 "ra.w"
extern Frame **frames;

#line 348 "ra.w"
extern Unsigned_Int original_register_count;

#line 362 "ra.w"
extern Unsigned_Int ssa_flags;

#line 602 "ra.w"
extern Unsigned_Int *val2range;
extern Unsigned_Int name_count;

#line 638 "ra.w"
extern Unsigned_Int *types;

#line 26 "rematerialize.inc"
extern Boolean perform_rematerialization;
extern Boolean rematerialize_loads;

#line 45 "rematerialize.inc"
extern Lattice *remat_vals;

#line 393 "rematerialize.inc"
extern Lattice *remat_opers;

#line 532 "rematerialize.inc"
extern Boolean advanced_remat;

#line 1035 "rematerialize.inc"
extern IV_INFO **iv_entries;

#line 1266 "rematerialize.inc"
extern IV_LATTICE *iv_lattice;

#line 172 "interference_graph.inc"
extern Edge_Vector **edge_vectors;

#line 17 "live_range.inc"
extern Boolean use_live_ranges;

#line 51 "liveness.inc"
extern VectorSet *live_in;
extern VectorSet *live_out;

#line 103 "coalesce.inc"
extern Boolean ultra_conservative_coalescing;

#line 172 "coalesce.inc"
extern Boolean fast_edge_count;

#line 49 "color.inc"
extern SparseSet low, high, high_degree_infinite_cost;

#line 107 "color.inc"
extern Unsigned_Int *coloring_stack;
extern Int coloring_stack_index;

#line 295 "color.inc"
extern Unsigned_Int *colors;

#line 365 "color.inc"
extern VectorSet spill_set;

#line 11 "edge_counts.inc"
extern Boolean modified_edge_counts;

#line 18 "spill.inc"
extern Unsigned_Int *spill_locations;
extern VectorSet already_stored;

#line 137 "spill.inc"
extern Range *range;

#line 300 "spill.inc"
extern Boolean biased_coloring;
extern Partner **partners;

#line 433 "spill.inc"
extern Unsigned_Int load_count, store_count, load_immediate_count;

#line 10 "splits.inc"
extern Boolean split_live_ranges;

#line 12 "bergner.inc"
extern Boolean bergner;

#line 11 "avail.inc"
extern Boolean do_avail;

#line 11 "iterated.inc"
extern Boolean iterated_coalesce;

#line 49 "ra.w"


#line 25 "depths.inc"
extern Void find_nesting_depths(Arena external_arena);

#line 664 "ra.w"
extern Boolean same_register_set(Unsigned_Int x, Unsigned_Int y);

#line 681 "ra.w"
extern Unsigned_Int min_color(Unsigned_Int name);
extern Unsigned_Int max_color(Unsigned_Int name);

#line 60 "rematerialize.inc"
Void compute_rematerialization(Arena external_arena);

#line 401 "rematerialize.inc"
extern Void update_rematerialization(Arena external_arena);

#line 434 "rematerialize.inc"
extern Boolean rematerializable(Unsigned_Int live_range);

#line 452 "rematerialize.inc"
extern Void insert_remat(Unsigned_Int live_range, Inst *inst);

#line 541 "rematerialize.inc"
extern Void find_induction_vars(Arena external_arena);

#line 1303 "rematerialize.inc"
extern Boolean base_induction_variable(Unsigned_Int live_range);

#line 1317 "rematerialize.inc"
extern Boolean derived_base_induction_variable(Unsigned_Int live_range);

#line 1331 "rematerialize.inc"
extern Void insert_induction_variable(Unsigned_Int live_range, Inst *inst);

#line 1517 "rematerialize.inc"
extern Void phi_node_printer(Block *block);

#line 23 "interference_graph.inc"
Void build_interference_graph(Arena external_arena);

#line 114 "interference_graph.inc"
Boolean interferes(Unsigned_Int x, Unsigned_Int y);

#line 136 "interference_graph.inc"
Void insert_edge_into_matrix(Unsigned_Int x, Unsigned_Int y);

#line 26 "live_range.inc"
extern Void build_live_ranges(Boolean union_all);

#line 82 "live_range.inc"
extern Void compress(Unsigned_Int *mapping, Arena arena);

#line 73 "liveness.inc"
Void build_liveness_analysis(Arena external_arena);

#line 224 "interference_graph.inc"
extern Void Vector2SparseSet(VectorSet vs, SparseSet ss);

#line 15 "coalesce.inc"
extern Boolean coalesce(Boolean conservative);

#line 81 "coalesce.inc"
Unsigned_Int ra_find_root(Unsigned_Int lookup, Unsigned_Int *table);

#line 354 "coalesce.inc"
extern Void remove_oper(Operation *oper, Inst *inst);

#line 12 "color.inc"
Void simplify(Arena external_arena);

#line 245 "color.inc"
extern Boolean select_colors(Arena external_arena);

#line 500 "color.inc"
extern Void final_coloring(Void);

#line 19 "edge_counts.inc"
extern Void build_unbroken_graph(Void);

#line 136 "edge_counts.inc"
extern Boolean unbroken_edge(Unsigned_Int i, Unsigned_Int j);

#line 154 "edge_counts.inc"
extern Unsigned_Int unbroken_edges(Unsigned_Int live_range);

#line 37 "spill.inc"
extern Void find_locations(Arena external_arena);

#line 92 "spill.inc"
extern Void calculate_spill_costs(Arena external_arena);

#line 359 "spill.inc"
extern Double my_raise(Unsigned_Int power);

#line 391 "spill.inc"
extern Void insert_spill_code(Arena external_arena);

#line 655 "spill.inc"
extern Void insert_load(Inst *inst, Unsigned_Int arg, Frame *frame,
                        Char *comment);

#line 730 "spill.inc"
extern Void insert_store(Inst *inst, Unsigned_Int arg, Frame *frame,
                         Char *comment);

#line 817 "spill.inc"
extern Void save_locations(Void);

#line 24 "splits.inc"
extern Void splits_init(Arena external_arena);

#line 190 "splits.inc"
extern Boolean find_splits(Unsigned_Int name, Unsigned_Int *colors,
                           Arena external_arena);

#line 429 "splits.inc"
extern Void insert_splits(Void);

#line 23 "bergner.inc"
extern Void ir_spill_costs(Arena external_arena);

#line 410 "bergner.inc"
extern Void ir_spill_code(Arena external_arena);

#line 783 "bergner.inc"
extern Void group_neighbors(Unsigned_Int name, Arena split_arena);

#line 837 "bergner.inc"
extern Void find_color(VectorSet live, Unsigned_Int color);

#line 22 "avail.inc"
extern Void avail_spill_costs(Arena external_arena);

#line 549 "avail.inc"
extern Void avail_spill_code(Arena external_arena);

#line 21 "iterated.inc"
extern Void iterated_simplify(Arena external_arena);

#line 812 "iterated.inc"
extern Boolean iterated_select_colors(Arena external_arena);

#line 906 "ra.w"
Void print_mapping(char *name, Unsigned_Int *array, Unsigned_Int size);

#line 50 "ra.w"

