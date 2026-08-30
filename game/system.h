#ifndef CBLUEPRINT_GAME_SYSTEM_H
#define CBLUEPRINT_GAME_SYSTEM_H

#include "../core/compile.h"
#include "../core/flow.h"
#include "../core/graph_ir.h"
#include "../core/program.h"
#include "../ecs/ecs.h"

/* A System owns one graph definition and applies its Tick event to each Entity
 * matching the definition's declared component effects. The graph stays the
 * source of truth; the reusable instance is only an execution scratchpad. */
typedef struct game_system game_system;

game_system *game_system_new(ecs_world *world, blueprint *definition);
void game_system_free(game_system *system);
int game_system_replace(game_system *system, blueprint *definition);
/* The authored filter is combined with the graph's inferred component access
 * terms. The expression is copied and may be freed by the caller. */
int game_system_set_query(game_system *system, const ecs_query_expr *expression);
const ecs_query_expr *game_system_query_expression(const game_system *system);

typedef enum
{
    GAME_SYSTEM_EXEC_AUTO,
    /* Profiling hooks; normal callers should use GAME_SYSTEM_EXEC_AUTO. */
    GAME_SYSTEM_EXEC_GENERIC_BYTECODE,
    GAME_SYSTEM_EXEC_TYPED_BYTECODE,
    GAME_SYSTEM_EXEC_SJLIT,
} game_system_exec_mode;

int game_system_tick_mode(
    game_system *system,
    double monotonic_time,
    double delta_seconds,
    flow_error *error,
    game_system_exec_mode mode
);
int game_system_tick(game_system *system, double monotonic_time, double delta_seconds, flow_error *error);

const blueprint *game_system_blueprint(const game_system *system);
const compile_report *game_system_compile(const game_system *system);
int game_system_query_count(const game_system *system);
ecs_component_id game_system_query_at(const game_system *system, int index);
int game_system_match_count(const game_system *system);
int game_system_uses_bytecode(const game_system *system);
int game_system_uses_typed_bytecode(const game_system *system);
int game_system_uses_sljit(const game_system *system);
int game_system_uses_sljit_batch(const game_system *system);
int game_system_uses_sljit_registers(const game_system *system);
int game_system_uses_sljit_simd(const game_system *system);
const graph_ir *game_system_graph_ir(const game_system *system);
const graph_program *game_system_program(const game_system *system);

#endif
