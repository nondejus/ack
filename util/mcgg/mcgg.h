#ifndef MCGG_H
#define MCGG_H

/* Excruciating macro which packs ir opcodes and sizes into an int for iburg's benefit.
 *
 * Sizes are mapped as: 0=1, 1=1, 2=2, 4=3, 8=4.
 */
#define ir_to_esn(iropcode, size) \
	((iropcode)*4 + \
		(((size) == 4) ? 2 : \
		 ((size) == 8) ? 3 : \
		 ((size) == 0) ? 0 : \
		  (size-1)))

#define STATE_TYPE void*

#define STATE_LABEL(p) ((p)->state_label)
#define OP_LABEL(p) ((p)->label)
#define LEFT_CHILD(p) ((p)->left)
#define RIGHT_CHILD(p) ((p)->right)

struct burm_node
{
    int label;
    void* state_label;
    struct burm_node* left;
    struct burm_node* right;
    struct ir* ir;
};

typedef struct burm_node* NODEPTR_TYPE;

extern void* burm_label(NODEPTR_TYPE node);
extern int burm_rule(void* state, int goalnt);
extern const short *burm_nts[];
extern NODEPTR_TYPE* burm_kids(NODEPTR_TYPE p, int eruleno, NODEPTR_TYPE kids[]);
extern void burm_trace(NODEPTR_TYPE p, int ruleno, int cost, int bestcost);

struct burm_emitter_data
{
    void (*emit_string)(const char* data);
    void (*emit_fragment)(NODEPTR_TYPE node, int goal);
    void (*emit_reg)(NODEPTR_TYPE node, int goal);
    void (*emit_value)(NODEPTR_TYPE node);
    void (*emit_eoi)(void);
    void (*emit_constraint_equals)(NODEPTR_TYPE node, int goal);
};

typedef void burm_emitter_t(NODEPTR_TYPE node, const struct burm_emitter_data* data);

struct burm_instruction_data
{
    const char* name;
    burm_emitter_t* emitter;
    int allocate;
    bool is_fragment;
};

extern const struct burm_instruction_data burm_instruction_data[];

struct burm_register_data
{
    const char* name;
    uint32_t classes;
};

extern const struct burm_register_data burm_register_data[];
extern const char* burm_register_class_names[];

#endif

/* vim: set sw=4 ts=4 expandtab : */