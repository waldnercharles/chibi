#include "runtime/visual_effects.h"

#include "graph/runtime/flow.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A pulse that falls from its amount to zero over its duration. */
typedef struct
{
    float amount, duration, age;
} pulse;

struct game_visual_effects
{
    float flash_color[3];
    pulse flash, aberration;
    game_shockwave shockwaves[GAME_SHOCKWAVE_MAX];
    int shockwave_count;
};

game_visual_effects *game_visual_effects_new()
{
    return xcalloc(1, sizeof(game_visual_effects));
}

void game_visual_effects_free(game_visual_effects *effects)
{
    free(effects);
}

void game_visual_effects_reset(game_visual_effects *effects)
{
    memset(effects, 0, sizeof *effects);
}

/* Quadratic ease out: a sharp start that settles smoothly. */
static float pulse_value(pulse p)
{
    if (p.age >= p.duration) {
        return 0;
    }
    float left = 1 - p.age / p.duration;
    return p.amount * left * left;
}

void game_visual_effects_tick(game_visual_effects *effects, float delta)
{
    effects->flash.age += delta;
    effects->aberration.age += delta;
    int kept = 0;
    for (int i = 0; i < effects->shockwave_count; i++) {
        game_shockwave wave = effects->shockwaves[i];
        wave.age += delta;
        if (wave.age < wave.duration) {
            effects->shockwaves[kept++] = wave;
        }
    }
    effects->shockwave_count = kept;
}

game_visual_effects_state game_visual_effects_current(const game_visual_effects *effects)
{
    game_visual_effects_state state = {
        .flash = { effects->flash_color[0],
                   effects->flash_color[1],
                   effects->flash_color[2],
                   pulse_value(effects->flash) },
        .aberration = pulse_value(effects->aberration),
        .shockwaves = effects->shockwaves,
        .shockwave_count = effects->shockwave_count,
    };
    return state;
}

static bool valid_pulse(float amount, float duration)
{
    return isfinite(amount) && isfinite(duration) && amount >= 0 && duration > 0;
}

/* A new pulse replaces the current one when it is at least as strong now. */
static void start_pulse(pulse *p, float amount, float duration)
{
    if (amount >= pulse_value(*p)) {
        *p = (pulse){ amount, duration, 0 };
    }
}

bool game_visual_effects_flash(game_visual_effects *effects, unsigned color, float amount, float duration)
{
    if (!valid_pulse(amount, duration)) {
        return false;
    }
    if (amount >= pulse_value(effects->flash)) {
        for (int i = 0; i < 3; i++) {
            effects->flash_color[i] = ((color >> (24 - 8 * i)) & 255u) / 255.0f;
        }
    }
    start_pulse(&effects->flash, fminf(amount, 1), duration);
    return true;
}

bool game_visual_effects_pulse_aberration(game_visual_effects *effects, float amount, float duration)
{
    if (!valid_pulse(amount, duration)) {
        return false;
    }
    start_pulse(&effects->aberration, amount, duration);
    return true;
}

bool game_visual_effects_add_shockwave(game_visual_effects *effects, game_shockwave wave)
{
    if (!isfinite(wave.position.x + wave.position.y) ||
        !valid_pulse(wave.radius, wave.duration) || !isfinite(wave.thickness) ||
        wave.thickness <= 0 || !isfinite(wave.strength)) {
        return false;
    }
    wave.age = 0;
    if (effects->shockwave_count < GAME_SHOCKWAVE_MAX) {
        effects->shockwaves[effects->shockwave_count++] = wave;
        return true;
    }
    int oldest = 0;
    for (int i = 1; i < GAME_SHOCKWAVE_MAX; i++) {
        if (effects->shockwaves[i].age > effects->shockwaves[oldest].age) {
            oldest = i;
        }
    }
    effects->shockwaves[oldest] = wave;
    return true;
}

void game_visual_effects_fade_flashes(ecs_world *world, float delta)
{
    ecs_component_id component = GAME_COMPONENT_GameFlash.id;
    ecs_query query;
    if (!ecs_world_component(world, component) ||
        ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameFlash *flash = GAME_GameFlash_get(world, entity);
        if (flash->amount > 0) {
            flash->amount = fmaxf(flash->amount - fmaxf(flash->decay, 0) * delta, 0);
        }
    }
    ecs_query_end(&query);
}

/* Reads a flow node's data inputs after its exec pin. */
static int inputs(flow_context *ctx, pin_value *values, int count)
{
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static game_visual_effects *service(flow_context *ctx)
{
    return flow_context_service(ctx, "game.visual_effects");
}

static int flash_screen(node *, flow_context *ctx)
{
    game_visual_effects *effects = service(ctx);
    if (!effects) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Flash Screen requires a game world");
    }
    pin_value v[3];
    int result = inputs(ctx, v, 3);
    if (result < 0) {
        return result;
    }
    if (!game_visual_effects_flash(effects, v[0].u, v[1].f, v[2].f)) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Flash Screen needs a nonnegative Amount and a positive Duration");
    }
    return flow_context_continue(ctx, 0);
}

static int pulse_aberration(node *, flow_context *ctx)
{
    game_visual_effects *effects = service(ctx);
    if (!effects) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Pulse Chromatic Aberration requires a game world");
    }
    pin_value v[2];
    int result = inputs(ctx, v, 2);
    if (result < 0) {
        return result;
    }
    if (!game_visual_effects_pulse_aberration(effects, v[0].f, v[1].f)) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Pulse Chromatic Aberration needs a nonnegative Amount and a positive Duration");
    }
    return flow_context_continue(ctx, 0);
}

static int add_shockwave(node *, flow_context *ctx)
{
    game_visual_effects *effects = service(ctx);
    if (!effects) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Add Shockwave requires a game world");
    }
    pin_value v[5];
    int result = inputs(ctx, v, 5);
    if (result < 0) {
        return result;
    }
    if (!v[0].ptr) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Add Shockwave needs a Position");
    }
    game_shockwave wave = { .radius = v[1].f,
                            .thickness = v[2].f,
                            .strength = v[3].f,
                            .duration = v[4].f };
    memcpy(&wave.position, v[0].ptr, sizeof wave.position);
    if (!game_visual_effects_add_shockwave(effects, wave)) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Add Shockwave needs a finite Position and Strength, and positive Radius, Thickness and Duration");
    }
    return flow_context_continue(ctx, 0);
}

static int flash_entity(node *, flow_context *ctx)
{
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    if (!world) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Flash Entity requires a live world");
    }
    pin_value v[3];
    int result = inputs(ctx, v, 3);
    if (result < 0) {
        return result;
    }
    ecs_entity entity = ecs_entity_from_u64(v[0].u64);
    GameFlash *flash = ecs_entity_is_alive(world, entity)
                         ? GAME_GameFlash_get(world, entity)
                         : NULL;
    if (!flash) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Flash Entity needs a live Entity with a GameFlash component");
    }
    if (!isfinite(v[2].f) || v[2].f < 0) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Flash Entity needs a nonnegative Amount");
    }
    flash->color = v[1].u;
    flash->amount = fmaxf(flash->amount, fminf(v[2].f, 1));
    return flow_context_continue(ctx, 0);
}

#define FLOAT_PIN(label, value)                                                  \
    {                                                                            \
        .name = label, .type = PIN_F32, .type_name = "float", .default_value = { \
            .type = PIN_F32,                                                     \
            .f = value                                                           \
        }                                                                        \
    }
#define EXEC_PIN { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC }

static pin_spec FLASH_IN[] = { EXEC_PIN,
                               { .name = "Color",
                                 .flags = PIN_FLAG_COLOR,
                                 .type = PIN_U32,
                                 .type_name = "unsigned int",
                                 .default_value = { .type = PIN_U32, .u = 0xffffffffu } },
                               FLOAT_PIN("Amount", .5f),
                               FLOAT_PIN("Duration", .2f) };
static pin_spec FLASH_ENTITY_IN[] = {
    EXEC_PIN,
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF },
    { .name = "Color",
      .flags = PIN_FLAG_COLOR,
      .type = PIN_U32,
      .type_name = "unsigned int",
      .default_value = { .type = PIN_U32, .u = 0xffffffffu } },
    FLOAT_PIN("Amount", 1)
};
static pin_spec PULSE_IN[] = { EXEC_PIN,
                               FLOAT_PIN("Amount", 6),
                               FLOAT_PIN("Duration", .3f) };
static pin_spec SHOCKWAVE_IN[] = {
    EXEC_PIN,
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    FLOAT_PIN("Radius", 160),
    FLOAT_PIN("Thickness", 24),
    FLOAT_PIN("Strength", 12),
    FLOAT_PIN("Duration", .5f)
};
static pin_spec THEN[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

static node_desc FLASH_SCREEN = {
    .name = "Flash Screen",
    .category = "Visual/Effects",
    .keywords = "flash hit impact white juice",
    .description = "Washes the frame toward Color by Amount (0 to 1), fading "
                   "out over Duration seconds. Needs tone mapping.",
    .input_count = 4,
    .inputs = FLASH_IN,
    .output_count = 1,
    .outputs = THEN,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flash_screen,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &ACCESS,
};

static node_desc PULSE_ABERRATION = {
    .name = "Pulse Chromatic Aberration",
    .category = "Visual/Effects",
    .keywords = "chromatic aberration damage hit lens juice",
    .description = "Splits red and blue by Amount output pixels at the screen "
                   "corners, fading out over Duration seconds. Needs tone "
                   "mapping.",
    .input_count = 3,
    .inputs = PULSE_IN,
    .output_count = 1,
    .outputs = THEN,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = pulse_aberration,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &ACCESS,
};

static node_desc ADD_SHOCKWAVE = {
    .name = "Add Shockwave",
    .category = "Visual/Effects",
    .keywords = "shockwave blast explosion distortion ring refraction",
    .description = "Expands a ring from Position to Radius world units over "
                   "Duration seconds that bends the frame by up to Strength "
                   "output pixels across Thickness world units. Needs tone "
                   "mapping.",
    .input_count = 6,
    .inputs = SHOCKWAVE_IN,
    .output_count = 1,
    .outputs = THEN,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = add_shockwave,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &ACCESS,
};

static const node_access FLASH_ENTITY_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
static node_desc FLASH_ENTITY = {
    .name = "Flash Entity",
    .category = "Visual/Effects",
    .keywords = "hit flash damage white blink tint",
    .description = "Tints Entity's material, which defaults to Self, toward "
                   "Color by Amount (0 to 1) and lights its surroundings; its "
                   "GameFlash decay fades it.",
    .input_count = 4,
    .inputs = FLASH_ENTITY_IN,
    .output_count = 1,
    .outputs = THEN,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = flash_entity,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &FLASH_ENTITY_ACCESS,
};

static node_desc *const NODES[] = { &FLASH_SCREEN,
                                    &PULSE_ABERRATION,
                                    &ADD_SHOCKWAVE,
                                    &FLASH_ENTITY };
const registry_pack GAME_VISUAL_EFFECTS_PACK = { .name = "game.visual_effects",
                                                 .nodes = NODES,
                                                 .node_count = 4 };
