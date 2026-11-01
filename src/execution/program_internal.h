#ifndef CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#define CHIBI_EXECUTION_PROGRAM_INTERNAL_H
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_internal.h"
#include "types/pointer.h"
typedef struct
{
    pin_value value;
    arena storage;
    arena *retired;
} state_value;
/* One registration of a module: its retained states, the services its calls
 * see, and the managed heap its values live in. Imported modules are child
 * programs that share the root's heap and services. Every call runs to
 * completion; a wait or task is refused. */
struct execution_program
{
    execution_module *module;
    double time; /* Read through the context's instance clock. */
    execution_program_failure failure;
    char *name;
    service_binding *services;
    /* Borrowed native entries its calls run; NULL runs them all in the VM. */
    const struct execution_natives *natives;
    service_binding *pending_services;
    arena service_names;
    behavior_managed_heap *managed;
    state_value *states;                /* One per plan state. */
    pin_value *values;                  /* The calls' view of states. */
    behavior_variable_storage *layouts; /* Layout of each value. */
    struct execution_program **imports;
    struct execution_program *owner_program;
    bool running;   /* Set on the root while one of its calls runs. */
    arena *payload; /* The running call's payload, for names it owns. */
    const execution_program *replacement_of;
    pin_value *replacement_roots; /* Stable slots pinned in the original heap. */
    int replacement_root_count;   /* Successfully registered prefix only. */
    const execution_module *replacement_module;
};

bool program_same_type(pin_value a, pin_value b);
/* Converts old, a kept value whose type changed, to shape's type: records
 * match fields by name, and numbers convert as T(value) does. Shape's payload,
 * when it has one, supplies new fields and fields that cannot convert. False
 * when no conversion exists; out may borrow old and storage. */
bool program_migrate_value(arena *storage, pin_value old, pin_value shape, pin_value *out);
/* Visited managed cells, open-addressed. Free slots when done. */
typedef struct
{
    const pin_pointer **slots;
    size_t capacity, count;
} program_pointer_set;
bool program_pointer_first_visit(program_pointer_set *, const pin_pointer *);
/* Whether a borrowed view of [address, address + bytes) stays clear of the
 * state storage the function's calls write. */
bool program_view_disjoint(
    const behavior_function *,
    const behavior_function_context *,
    const void *,
    size_t
);
/* Whether every function the module declares links: no component access,
 * and no host call that hands a managed pointer to a service. */
bool program_preflight(const execution_module *);
int program_validate_values(execution_program *, const pin_value *, int count);
/* Collects the managed heap once what it allocated since the last collection
 * outgrows what it kept. The states are the roots. Not while a call runs. */
void program_collect_states(execution_program *);
void program_apply_services(execution_program *);
#endif
