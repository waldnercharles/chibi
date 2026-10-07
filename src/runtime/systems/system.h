#ifndef CHIBI_GAME_SYSTEM_H
#define CHIBI_GAME_SYSTEM_H

#include "ecs/ecs.h"
#include "graph/compiler/compile.h"
#include "graph/compiler/graph_ir.h"
#include "graph/compiler/program.h"
#include "graph/runtime/flow.h"

/* A System owns one graph definition and applies its Tick event to each Entity
 * matching the definition's declared component effects. The graph stays the
 * source of truth. Synchronous stateless work uses compiled batches; stateful
 * or suspended work retains one invocation per matched entity generation. */
typedef struct game_system game_system;

/* Whether the System compiler turns a node into bytecode: the Event node,
 * Get_<Component>, Set_<Component> for a component without text, and the data
 * nodes it evaluates per entity: literals, Delta Time, copies, generated
 * Make_ and Break_, Behavior Expression, Random Real, and calls with one
 * result and only data inputs. Studio and the MCP offer exactly these for
 * System graphs; other nodes fall back to the flow interpreter. */
bool game_system_node_supported(const node_desc *desc);

game_system *game_system_new(ecs_world *world, behavior_asset *definition);
void game_system_free(game_system *system);
/* Host pointers are borrowed for this registration and its retained entities. */
int game_system_bind_service(game_system *, const char *type_name, void *value);
/* The authored filter is combined with the graph's inferred component access
 * terms. The expression is copied and may be freed by the caller. */
int game_system_set_query(game_system *system, const ecs_query_expr *expression);

typedef enum
{
    GAME_SYSTEM_EXEC_AUTO,
    /* Explicit parity/debug backends; runtime callers use AUTO. */
    GAME_SYSTEM_EXEC_GENERIC_BYTECODE,
    GAME_SYSTEM_EXEC_TYPED_BYTECODE,
    GAME_SYSTEM_EXEC_SJLIT,
    GAME_SYSTEM_EXEC_ORACLE,
} game_system_exec_mode;

int game_system_tick_mode(
    game_system *system,
    double monotonic_time,
    double delta_seconds,
    flow_error *error,
    game_system_exec_mode mode
);

const behavior_asset *game_system_behavior_asset(const game_system *system);
const compile_report *game_system_compile(const game_system *system);
int game_system_query_count(const game_system *system);
int game_system_match_count(const game_system *system);
int game_system_uses_bytecode(const game_system *system);
int game_system_uses_typed_bytecode(const game_system *system);
int game_system_uses_sljit(const game_system *system);
int game_system_uses_sljit_batch(const game_system *system);
int game_system_uses_sljit_registers(const game_system *system);
int game_system_uses_sljit_simd(const game_system *system);
const graph_program *game_system_program(const game_system *system);

/* Runtime inspection. Entity-private state exists only while query membership
 * is retained; shared state belongs to this registration. Values are borrowed. */
int game_system_entity_variable(const game_system *, ecs_entity, uint64_t id, pin_value *out);
int game_system_shared_variable(const game_system *, uint64_t id, pin_value *out);
int game_system_suspended_count(const game_system *);
int game_system_retained_count(const game_system *);

/* Scheduler preparation runs on the host thread before disjoint systems run.
 * It binds columns and snapshots query membership without concurrent ECS queries. */
int game_system_prepare_snapshot(game_system *system, flow_error *error);
/* Refresh query membership, bindings and event subscriptions without Tick. */
int game_system_maintain(game_system *, double time, double delta, flow_error *error);
int game_system_field_changed(game_system *, ecs_entity, ecs_component_id, size_t, pin_value, flow_error *);
int game_system_run_snapshot(game_system *system, double time, double delta, flow_error *error);

#endif
