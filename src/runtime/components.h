#ifndef CHIBI_GAME_COMPONENTS_H
#define CHIBI_GAME_COMPONENTS_H

#include "graph/model/annotate.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <stdbool.h>
#include <stdint.h>

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameTransform;
typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameVelocity;

enum
{
    GAME_BODY_STATIC,
    GAME_BODY_DYNAMIC,
    GAME_BODY_KINEMATIC
};
enum
{
    GAME_COLLIDER_CIRCLE,
    GAME_COLLIDER_BOX
};

/* All spatial values use project units. There are 32 project units per meter. */
#define GAME_UNITS_PER_METER 32.0f

typedef NODE_COMPONENT_TYPE_DENSE(GameBody)
{
    int type;
    bool continuous;
    float angular_velocity; /* Kinematic radians/second; requires Orientation. */
}
GameBody;

typedef NODE_COMPONENT_TYPE_DENSE(GameCollider)
{
    float radius;
    unsigned category;
    unsigned mask;
    int shape;
    float width;
    float height;
    bool sensor;
    bool pierce_fragments; /* Continuous sensors report debris hits without stopping. */
    uint64_t ignore_entity NODE_HANDLE(ecs.entity);
    float restitution;
    float friction;
}
GameCollider;

/* Target velocity with finite acceleration; physics owns actual velocity. */
typedef NODE_COMPONENT_TYPE_DENSE(GameDrive)
{
    CF_V2 target;
    float acceleration;
}
GameDrive;

/* Optional material world. The camera activates nearby chunks; moving bodies
 * wake additional local regions. Population limits bound retained resources. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMaterialWorld)
{
    float chunk_size;
    float buffer;
    float kernel;
    int particle_limit;
    int fragment_limit;
    /* Optional gas flow; a zero cell size leaves particles independent. */
    float gas_cell_size;
    float gas_vorticity;
    float gas_drag;
}
GameMaterialWorld;

/* Optional local combustion. Heat is a gameplay quantity, not kelvin. */
typedef NODE_COMPONENT_TYPE_DENSE(GameCombustion)
{
    float ignition;
    float cooling;
    float burn_rate;
    float heat_release;
    float smoke_lifetime;
    unsigned smoke_color;
    float smoke_absorption;
    unsigned flame_color;
    float flame_emission;
    float blast_radius;
    float blast_speed;
    float blast_damage;
}
GameCombustion;

typedef NODE_COMPONENT_TYPE_DENSE(GameFuel)
{
    float amount;
}
GameFuel;

typedef NODE_COMPONENT_TYPE_DENSE(GameIgniter)
{
    float heat;
}
GameIgniter;

/* Damage per surviving cell. Optical properties remain in GameMaterial. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSubstance)
{
    float hardness;
    float blast_multiplier;
    bool retain_chips;
}
GameSubstance;

/* Optional circular void in a destructible mask, for finite sealed pockets. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMaterialCavity)
{
    float radius;
}
GameMaterialCavity;

/* Stylized planar reflection; angle is the local mirror tangent in radians. */
typedef NODE_COMPONENT_TYPE_DENSE(GameReflection)
{
    float strength;
    float roughness;
    float angle;
    float offset;
}
GameReflection;

typedef NODE_COMPONENT_TYPE_DENSE(GameDestructible)
{
    float cell_size;
    int min_fragment_cells;
    /* Release control below this fraction of the original hull; zero disables. */
    float release_fraction;
}
GameDestructible;

/* Detached material keeps its collision but gradually loses emitted light. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFragmentGlow)
{
    float duration;
    float flicker;
}
GameFragmentGlow;

/* One-time generated, axis-aligned terrain. Appearance and Material on the
 * source style its chunks; collision and destruction use the generated cells. */
typedef NODE_COMPONENT_TYPE_DENSE(GameCaveTerrain)
{
    unsigned seed;
    float width;
    float height;
    float cell_size;
    float tunnel_radius;
    float roughness;
    unsigned category;
    unsigned mask;
    int min_fragment_cells;
}
GameCaveTerrain;

/* A one-time, elliptical fill in local space; its matter then lives freely. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFluidVolume)
{
    int kind;
    float width;
    float height;
    float spacing;
    float mass;
    unsigned color;
    float absorption;
    float emission;
    /* Camera-only minimum visibility; contributes no radiance or bounce. */
    float visibility;
    /* Screen-space liquid distortion in pixels; zero disables refraction. */
    float refraction;
    CF_V2 velocity;
}
GameFluidVolume;

/* Persistent, axis-aligned participating medium. Density advects periodically
 * within this world-space domain; no particles or material world are required. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMedium)
{
    CF_V2 origin;
    int columns;
    int rows;
    float cell_size;
    float density;
    float variation;
    float detail_size;
    CF_V2 velocity;
    /* Extinction per world unit at density 1; linear RGB scattering albedo. */
    float extinction;
    unsigned scattering;
    /* Homogeneous slab depth toward the camera, in world units. */
    float depth;
}
GameMedium;

typedef NODE_COMPONENT_TYPE_DENSE(GameHealth)
{
    int current;
    int maximum;
}
GameHealth;

typedef NODE_COMPONENT_TYPE_DENSE(GameMovement)
{
    float speed;
}
GameMovement;

typedef NODE_COMPONENT_TYPE_DENSE(GameInput)
{
    CF_V2 move;
}
GameInput;

typedef NODE_COMPONENT_TYPE_DENSE(GameWeapon)
{
    int damage;
    float cooldown;
}
GameWeapon;

typedef NODE_COMPONENT_TYPE_DENSE(GameProjectile)
{
    float speed;
    float lifetime;
    int damage;
    float age;
}
GameProjectile;

enum
{
    GAME_SHAPE_BOX = 0,
    GAME_SHAPE_CIRCLE,
    GAME_SHAPE_DIAMOND,
    GAME_SHAPE_CHEVRON,
    GAME_SHAPE_STROKED_CHEVRON,
    GAME_SHAPE_CAPSULE,
    GAME_SHAPE_TRIANGLE,
    GAME_SHAPE_STROKED_TRIANGLE,
};

typedef NODE_COMPONENT_TYPE_DENSE(GameAppearance)
{
    float width;
    float height;
    unsigned color;
    int shape;
    float border_width;
    unsigned border_color;
    float glow_radius;
    unsigned glow_color;
    float stroke_width;
    float antialias;
    int layer;
}
GameAppearance;

/* Radians from gameplay +X toward +Y: atan2(direction.y, direction.x).
 * The 2D canvas flips Y at presentation; 3D maps gameplay Y to world Z. */
typedef NODE_COMPONENT_TYPE_DENSE(GameOrientation)
{
    float radians;
}
GameOrientation;

typedef NODE_COMPONENT_TYPE_DENSE(GameSdfMotion)
{
    float amplitude;
    float angular_frequency;
    float temporal_frequency;
    float time;
    float pluck_amplitude;
    float pluck_origin;
    float pluck_age;
    float pluck_speed;
}
GameSdfMotion;

/* Planar source: watts per unit depth, aperture width, angular spread in radians. */
typedef NODE_COMPONENT_TYPE_DENSE(GameLight2D)
{
    unsigned color;
    float power;
    float width;
    float spread;
}
GameLight2D;

/* Dielectric IOR > 1, or opaque mirror with IOR = 1. Beer absorption per world unit. */
typedef NODE_COMPONENT_TYPE_DENSE(GameOpticalSurface)
{
    float ior;
    float mirror;
    CF_V3 absorption;
}
GameOpticalSurface;

/* Fixed per-frame optical budget. No temporal accumulation of primary paths. */
typedef NODE_COMPONENT_TYPE_DENSE(GameOptics2D)
{
    int max_size;
    int rays;
    int events;
}
GameOptics2D;

typedef NODE_COMPONENT_TYPE_DENSE(GameMaterial)
{
    unsigned scene;
    unsigned albedo;
    unsigned filter;
    unsigned emission_color;
    float emission_intensity;
    float emission_multiplier;
    float absorption;
    unsigned border_scene;
    unsigned border_emission_color;
    unsigned border_albedo;
    unsigned border_filter;
    /* Received-light gain near silhouettes. Zero disables; no unlit outline. */
    float edge_light;
}
GameMaterial;

typedef NODE_COMPONENT_TYPE_DENSE(GameShader)
{
    const char *material;
    const char *shape;
    const char *parameters;
    float bounds_x;
    float bounds_y;
}
GameShader;

enum
{
    GAME_PRIMITIVE_BOX,
    GAME_PRIMITIVE_SPHERE,
    GAME_PRIMITIVE_CAPSULE,
    GAME_PRIMITIVE_CONE,
    GAME_PRIMITIVE_TRIANGLE_PRISM,
    GAME_PRIMITIVE_OCTAHEDRON,
    GAME_PRIMITIVE_TETRAHEDRON,
    GAME_PRIMITIVE_CHEVRON_PYRAMID,
    GAME_PRIMITIVE_BOX_FRAME,
    GAME_PRIMITIVE_TRIANGLE_OUTLINE,
    GAME_PRIMITIVE_TRIANGLE,
    GAME_PRIMITIVE_COUNT,
};

/* Full visual extents. Gameplay x/y maps to world x/z; elevation is center Y. */
typedef NODE_COMPONENT_TYPE_DENSE(GameGeometry3D)
{
    int primitive;
    float width;
    float depth;
    float height;
    float elevation;
}
GameGeometry3D;

typedef NODE_COMPONENT_TYPE_DENSE(GameAim)
{
    CF_V2 direction;
    bool firing;
    bool manual;
}
GameAim;

typedef NODE_COMPONENT_TYPE_DENSE(GameDash)
{
    float speed;
    float duration;
    float cooldown;
    int damage;
    float remaining;
    float cooldown_remaining;
    CF_V2 direction;
}
GameDash;

enum
{
    GAME_ENCOUNTER_ACTIVE,
    GAME_ENCOUNTER_CLEARED,
    GAME_ENCOUNTER_DEFEATED,
};
enum
{
    GAME_ENCOUNTER_PLAYER,
    GAME_ENCOUNTER_ENEMY,
};

typedef NODE_COMPONENT_TYPE_DENSE(GameSpawnRequest)
{
    const char *prototype;
    float delay;
}
GameSpawnRequest;

/* One encounter owns the current arena. Behaviors update these counters. */
typedef NODE_COMPONENT_TYPE_DENSE(GameEncounter)
{
    int status;
    int total;
    int spawned;
    int alive;
}
GameEncounter;

typedef NODE_COMPONENT_TYPE_DENSE(GameEncounterMember)
{
    int role;
}
GameEncounterMember;

/* Scene-wide display settings. Exposure is in stops; bloom radius is a fraction
 * of viewport height. Zero strength disables bloom. */
typedef NODE_COMPONENT_TYPE_DENSE(GameEnvironment)
{
    float exposure;
    float bloom_strength;
    float bloom_threshold;
    float bloom_radius;
}
GameEnvironment;

/* Local scene-linear light. Radius and center elevation use project units;
 * color is 0xRRGGBBAA, matching other authored color fields. */
typedef NODE_COMPONENT_TYPE_DENSE(GamePointLight)
{
    unsigned color;
    float intensity;
    float radius;
    float elevation;
}
GamePointLight;

/* A colored activity signal for material effects, independent of lighting. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSurfaceInfluence)
{
    unsigned color;
    float strength;
    float radius;
    float linger;
    float inner_radius;
}
GameSurfaceInfluence;

/* Shared simulation rhythm. Pulse width is measured in seconds. */
typedef NODE_COMPONENT_TYPE_DENSE(GameRhythm)
{
    float bpm;
    float pulse_width;
}
GameRhythm;

/* Visual-only local rotations in radians; gameplay heading remains independent. */
typedef NODE_COMPONENT_TYPE_DENSE(GameVisualTransform)
{
    float pitch;
    float yaw;
    float roll;
    float scale;
}
GameVisualTransform;

/* Optional native visual integration. Rotation axes are pitch/yaw/roll in
   radians; angular velocity is radians/second. Physics heading stays separate. */
typedef NODE_COMPONENT_TYPE_DENSE(GameVisualMotion)
{
    CF_V3 rotation;
    CF_V3 angular_velocity;
    float speed;
    float rest_elevation;
    CF_V3 reaction_rotation;
    float reaction_lift;
    float reaction_age;
    float reaction_duration;
}
GameVisualMotion;

typedef NODE_COMPONENT_TYPE_DENSE(GameMeshParts)
{
    const char *parts;
}
GameMeshParts;

/* Independent random endpoints; exponent zero holds the sampled start value. */
typedef NODE_TYPE(GameParticleCurve)
{
    CF_V2 start;
    CF_V2 end;
    float exponent;
}
GameParticleCurve;

/* A parent is optional. Each child emitter owns an independent seed and rate. */
typedef NODE_COMPONENT_TYPE_DENSE(GameParticleEmitter)
{
    int shape;
    unsigned color;
    unsigned end_color;
    float rate;
    int burst_count;
    CF_V2 lifetime;
    CF_V2 speed;
    GameParticleCurve size;
    float spread;
    GameParticleCurve drag;
    GameParticleCurve emission;
    GameParticleCurve antialias;
    float absorption;
    int metaball_group;
    float metaball_blend;
    bool velocity_align;
    CF_V2 stretch;
    float velocity_inherit;
    CF_V3 offset;
    CF_V3 direction;
    uint64_t parent NODE_HANDLE(ecs.entity);
    bool active;
    bool planar;
    bool heading_only;
    bool fluid;
    int fluid_kind;
    float fluid_mass;
}
GameParticleEmitter;

static inline GameParticleEmitter game_particle_emitter_defaults()
{
    return (GameParticleEmitter){ .color = 0xffffffffu,
                                  .end_color = 0xffffff00u,
                                  .rate = 90,
                                  .lifetime = { .3f, .3f },
                                  .speed = { 70, 70 },
                                  .size = { { 2, 2 }, { 0, 0 }, .8f },
                                  .spread = .3f,
                                  .drag = { { 1, 1 }, { 0, 0 }, 0 },
                                  .emission = { { .7f, .7f }, { 0, 0 }, 0 },
                                  .antialias = { { 1.5f, 1.5f }, { 0, 0 }, 0 },
                                  .absorption = .01f,
                                  .velocity_inherit = 1,
                                  .direction = { -1, 0, 0 },
                                  .active = true };
}

typedef NODE_COMPONENT_TYPE_DENSE(GameTrail)
{
    unsigned color;
    float width;
    float lifetime;
    float emission;
}
GameTrail;

typedef NODE_COMPONENT_TYPE_DENSE(GameExperience)
{
    int current;
}
GameExperience;

typedef NODE_COMPONENT_TYPE_DENSE(GamePickup)
{
    int value;
    float lifetime;
    float age;
    int state;
    float collection_age;
    uint64_t target NODE_HANDLE(ecs.entity);
    CF_V2 origin;
}
GamePickup;

/* An optional camera follows its entity's Transform. The authored span is
 * expanded for the viewport aspect; offsets and spans use project units. */
typedef NODE_COMPONENT_TYPE_DENSE(GameCamera)
{
    float width;
    float height;
    CF_V2 offset;
    bool disabled;
}
GameCamera;

typedef enum
{
    GAME_CONTROL_NONE,
    GAME_CONTROL_MOUSE_LEFT,
    GAME_CONTROL_MOUSE_RIGHT,
    GAME_CONTROL_SPACE,
    GAME_CONTROL_SHIFT,
    GAME_CONTROL_TAB,
    GAME_CONTROL_ENTER,
    GAME_CONTROL_A,
    GAME_CONTROL_B,
    GAME_CONTROL_C,
    GAME_CONTROL_D,
    GAME_CONTROL_E,
    GAME_CONTROL_F,
    GAME_CONTROL_G,
    GAME_CONTROL_H,
    GAME_CONTROL_I,
    GAME_CONTROL_J,
    GAME_CONTROL_K,
    GAME_CONTROL_L,
    GAME_CONTROL_M,
    GAME_CONTROL_N,
    GAME_CONTROL_O,
    GAME_CONTROL_P,
    GAME_CONTROL_Q,
    GAME_CONTROL_R,
    GAME_CONTROL_S,
    GAME_CONTROL_T,
    GAME_CONTROL_U,
    GAME_CONTROL_V,
    GAME_CONTROL_W,
    GAME_CONTROL_X,
    GAME_CONTROL_Y,
    GAME_CONTROL_Z,
    GAME_CONTROL_PAD_A,
    GAME_CONTROL_PAD_B,
    GAME_CONTROL_PAD_X,
    GAME_CONTROL_PAD_Y,
    GAME_CONTROL_PAD_LEFT_SHOULDER,
    GAME_CONTROL_PAD_RIGHT_SHOULDER,
    GAME_CONTROL_PAD_LEFT_TRIGGER,
    GAME_CONTROL_PAD_RIGHT_TRIGGER,
    GAME_CONTROL_AIM_STICK,
    GAME_CONTROL_COUNT,
} GameControl;

/* Multiple sources for the same action are combined before dispatch. Actions
 * target GameInput owners; firing also controls the continuous Aim state. */
typedef NODE_COMPONENT_TYPE_DENSE(GameActionBinding)
{
    int source;
    const char *action;
    bool firing;
    bool disabled;
    bool pressed;
}
GameActionBinding;

enum
{
    GAME_WIDGET_LABEL,
    GAME_WIDGET_PANEL,
    GAME_WIDGET_BUTTON,
    GAME_WIDGET_PROGRESS,
    GAME_WIDGET_SLIDER,
    GAME_WIDGET_SLOT,
    GAME_WIDGET_MAP,
};

/* UI offsets and sizes are logical pixels at a 720-pixel viewport height.
 * Anchors and pivots are fractions. Behavior owns contents, layout and rules.
 * An interactive widget sends its named event (Source: entity, Value: float).
 * Matching nonempty drop groups permit transfers; behaviors decide validity. */
typedef NODE_COMPONENT_TYPE_DENSE(GameWidget)
{
    int kind;
    const char *text;
    const char *tooltip;
    const char *event;
    const char *drop_group;
    CF_V2 anchor;
    CF_V2 pivot;
    CF_V2 offset;
    CF_V2 size;
    int order;
    unsigned color;
    unsigned background;
    unsigned accent;
    float font_size;
    float rounding;
    float value;
    float minimum;
    float maximum;
    bool center_text;
    bool hidden;
    bool disabled;
    bool draggable;
    bool block_input;
}
GameWidget;

/* One optional finite visibility grid. The main view conceals unseen space;
 * map widgets display explored cells and remembered obstacle occupancy. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFog)
{
    CF_V2 origin;
    int columns;
    int rows;
    float cell_size;
    float drift; /* Zero disables ambient fog motion. */
    unsigned color;
    bool disabled;
}
GameFog;

/* Vision sources share current sight. Obstacles in occluders block sight.
 * A positive duration shrinks a temporary clearing to zero over its lifetime;
 * duration <= 0 is permanent. Only remembering sources update terrain memory. */
typedef NODE_COMPONENT_TYPE_DENSE(GameVision)
{
    float radius;
    unsigned occluders;
    float duration;
    float age;
    bool remember;
    bool disabled;
}
GameVision;

/* A sensor observation owns its last detected position, independently of the
 * observed actor. Behaviors decide what it detects and when it refreshes. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSensorContact)
{
    CF_V2 position;
    float remaining;
    unsigned color;
}
GameSensorContact;

typedef NODE_COMPONENT_TYPE_DENSE(GameRenderState)
{
    bool hidden;
    bool visibility_culled;
}
GameRenderState;

/* Optional 2D radiance composition; values are nonnegative scene-linear gains. */
/* Cell size, radius, density and brightness for each procedural star layer. */
typedef NODE_COMPONENT_TYPE_DENSE(GameStarfield2D)
{
    CF_V4 far_layer, middle_layer, near_layer;
    CF_V3 parallax;
    unsigned color_a, color_b, color_c, color_d;
}
GameStarfield2D;

typedef NODE_COMPONENT_TYPE_DENSE(GameHRC2D)
{
    /* Zero follows the display; otherwise bound both transport and light history. */
    int max_light_size;
    float scale;
    float min_scale;
    float max_scale;
    bool adaptive;
    float target_fps;
    float fps_tolerance;
    float ambient;
    float intensity;
    float fog;
    float shadow_floor;
    float falloff;
    float bounce_intensity;
    float bounce_hysteresis;
    float radiance_hysteresis;
    int upscale_mode;
    float upscale_threshold;
    int debug_view;
    float particle_focal_length;
    int trace_levels;
    bool feedback;
}
GameHRC2D;

static inline GameHRC2D game_hrc2d_defaults()
{
    return (GameHRC2D){ .scale = .976883f,
                        .min_scale = .1f,
                        .max_scale = 1,
                        .adaptive = true,
                        .target_fps = 60,
                        .fps_tolerance = .1f,
                        .intensity = 7.5f,
                        .fog = 1,
                        .falloff = 1,
                        .bounce_intensity = 1,
                        .bounce_hysteresis = .8f,
                        .radiance_hysteresis = .8f,
                        .upscale_mode = 1,
                        .upscale_threshold = .1f,
                        .particle_focal_length = 300,
                        .trace_levels = 3,
                        .feedback = true };
}

/* Stored map positions for landmarks and objectives. Hidden until explored,
 * unless always_visible; movement and discovery rules belong to behaviors. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMapMarker)
{
    CF_V2 position;
    const char *label;
    unsigned color;
    float radius;
    bool always_visible;
    bool disabled;
}
GameMapMarker;
/* Count Matching Entities combines these conditions with AND. Presence tests
 * use "has" or "missing"; scalar fields use ==, !=, <, <=, > or >=. */
typedef NODE_TYPE(EntityCondition)
{
    const char *component;
    const char *field;
    const char *test;
    double value;
}
EntityCondition;

#endif
