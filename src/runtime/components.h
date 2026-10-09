#ifndef CHIBI_GAME_COMPONENTS_H
#define CHIBI_GAME_COMPONENTS_H

#include "types/annotate.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <stdbool.h>
#include <stdint.h>

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameTransform;
typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 GameVelocity;

/* Publishes this entity as a named global entity value. Behaviors can resolve
 * it without knowing how the entity was instantiated. */
typedef NODE_COMPONENT_TYPE_DENSE(GameGlobalBinding)
{
    const char *name;
}
GameGlobalBinding;

typedef enum
{
    GAME_BODY_STATIC,
    GAME_BODY_DYNAMIC,
    GAME_BODY_KINEMATIC
} GameBodyType;
typedef enum
{
    GAME_COLLIDER_CIRCLE,
    GAME_COLLIDER_BOX
} GameColliderShape;

/* All spatial values use project units. There are 32 project units per meter. */
#define GAME_UNITS_PER_METER 32.0f

typedef NODE_COMPONENT_TYPE_DENSE(GameBody)
{
    GameBodyType type;
    bool continuous;
    float angular_velocity; /* Kinematic radians/second; requires Orientation. */
    bool allow_rotation;
}
GameBody;

typedef NODE_COMPONENT_TYPE_DENSE(GameCollider)
{
    float radius;
    unsigned category;
    unsigned mask;
    GameColliderShape shape;
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

/* A Scene-owned reference to a standalone System document. */
typedef NODE_COMPONENT_TYPE_DENSE(EngineSystem)
{
    const char *asset;
    int phase;
    int order;
}
EngineSystem;

typedef enum
{
    MATERIAL_SOLVER_DDR,
    MATERIAL_SOLVER_MPM
} MaterialSolver;

typedef enum
{
    MATERIAL_VIEW_MATERIAL,
    MATERIAL_VIEW_HEAT,
    MATERIAL_VIEW_SPEED,
    MATERIAL_VIEW_STRAIN
} MaterialDebugView;

/* Optional material world. The camera activates nearby chunks; moving bodies
 * wake additional local regions. Population limits bound retained resources. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialFluidWorld)
{
    float chunk_size;
    float buffer;
    float kernel;
    int particle_limit;
    MaterialSolver solver;
    float mpm_cell_size;
    int mpm_max_substeps;
    CF_V2 gravity;
    MaterialDebugView debug_view;
    /* Optional gas flow; a zero cell size leaves particles independent. */
    float gas_cell_size;
    float gas_vorticity;
    float gas_drag;
    float gas_buoyancy;
    float gas_ambient_heat;
    /* Optional transient visual responses to fast fluid/body contacts. Zero
     * count disables that response; particles never add matter to the solver. */
    float gas_wake_speed; /* Minimum normal closing speed, world units/sec. */
    float gas_wake_strength; /* Scales puff speed and response intensity. */
    int gas_wake_count;      /* Maximum visual particles per body/patch/tick. */
    float gas_wake_lifetime; /* Maximum visual particle lifetime, seconds. */
    float liquid_splash_speed; /* Minimum normal closing speed, world units/sec. */
    float liquid_splash_strength; /* Scales droplet speed and response intensity. */
    int liquid_splash_count; /* Maximum visual particles per body/patch/tick. */
    float liquid_splash_lifetime; /* Maximum visual particle lifetime, seconds. */
}
MaterialFluidWorld;

typedef NODE_COMPONENT_TYPE_DENSE(MaterialDestructionWorld)
{
    int fragment_limit;
    /* Small rock chunks thrown from craters; zero disables them. */
    int debris_limit;
}
MaterialDestructionWorld;

typedef NODE_COMPONENT_TYPE_DENSE(MaterialBreakProfile)
{
    float chip_speed, fragment_speed, blast_speed, release_speed;
    /* Grit sprays from each crater around its outward surface normal: at
     * most dust_limit grains, one per crumbled cell. */
    int dust_limit;
    float dust_speed_min, dust_speed_max;
    float dust_lifetime_min, dust_lifetime_max;
    float dust_size_min, dust_size_max;
    /* Grit color; zero uses the source appearance color. */
    unsigned dust_color NODE_COLOR;
    /* Hot strikes add spark_density sparks per crumbled cell per unit of the
     * striker's heat. Sparks start in the striker's light color, cool through
     * spark_color and fade. */
    float spark_density;
    unsigned spark_color NODE_COLOR;
    float spark_emission;
    /* Rock chunks thrown per crater: one per chunk_cells crumbled cells, at
     * most chunk_limit. Sizes are world-unit radii. */
    int chunk_limit;
    float chunk_cells;
    float chunk_size_min, chunk_size_max;
    float chunk_speed_min, chunk_speed_max;
    float chunk_lifetime;
    /* Gas parcels released into the fluid world per crater; zero disables. */
    int smoke_count;
    unsigned smoke_color NODE_COLOR;
    float smoke_lifetime;
    /* Outward drift speed of the released gas in world units per second. */
    float smoke_speed;
}
MaterialBreakProfile;

/* Optional local combustion. Heat is a gameplay quantity, not kelvin. */
typedef NODE_COMPONENT_TYPE_DENSE(GameCombustion)
{
    float ignition;
    float cooling;
    float burn_rate;
    float heat_release;
    float smoke_lifetime;
    unsigned smoke_color NODE_COLOR;
    float smoke_absorption;
    unsigned flame_color NODE_COLOR;
    float flame_emission;
    /* World units a burst of ignitions pushes gas, bodies and debris; at most
     * 1024. */
    float blast_radius;
    float blast_speed;
    float blast_damage;
    /* Heat below this stops burning without consuming the remaining fuel.
     * Zero disables extinguishing; positive values must be below ignition. */
    float extinction;
}
GameCombustion;

/* Optional fluid heat exchange. Capacity is specific heat per unit mass;
 * conductivity is the local exchange rate. Zero conductivity is insulating. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialThermal)
{
    float capacity;
    float conductivity;
}
MaterialThermal;

/* MPM stress response. Rest area is particle mass / density. Positive shear
 * makes an elastic material; zero shear leaves a compressible liquid. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialConstitutive)
{
    float density;
    float bulk_modulus;
    float shear_modulus;
}
MaterialConstitutive;

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
    /* Detached fragments may be passed through by continuous sensors. */
    bool pierceable_fragments;
}
GameDestructible;

/* Detached material keeps its collision but gradually loses emitted light. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFragmentGlow)
{
    float duration;
    float flicker;
}
GameFragmentGlow;

/* Where an entity's local frame sat when its material was laid out. Fragments
 * keep their source's frame, so world-space material patterns stay attached to
 * a broken piece as it moves and rotates. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMaterialFrame)
{
    float x;
    float y;
    float radians;
}
GameMaterialFrame;

/* Authored, axis-aligned procedural terrain. Appearance and Material on the
 * source style its chunks; collision and destruction use the generated cells. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialTerrain)
{
    unsigned seed;
    float width;
    float height;
    float cell_size;
    const char *field;
    const char *parameters;
    int connectivity;
    int border_cells;
    unsigned category;
    unsigned mask;
    int min_fragment_cells;
    /* Zero keeps a bounded terrain resident. Positive radius streams around
     * cameras; zero width and height then allow unbounded terrain. Prefetch
     * bakes stream_budget slices of 16 tile rows per boundary, so no step
     * bakes a whole tile unless it is already visible. */
    float stream_radius;
    int stream_budget;
    /* Solid islands and air pockets smaller than this many cells that lie
     * wholly inside a tile are removed at generation; zero keeps them. */
    int island_cells;
}
MaterialTerrain;

typedef enum
{
    GAME_FLUID_GAS,
    GAME_FLUID_LIQUID,
} GameFluidKind;

/* A one-time, elliptical fill in local space; its matter then lives freely. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFluidVolume)
{
    GameFluidKind kind;
    float width;
    float height;
    float spacing;
    float mass;
    unsigned color NODE_COLOR;
    float absorption;
    float emission;
    /* Camera-only minimum visibility; contributes no radiance or bounce. */
    float visibility;
    /* Screen-space liquid distortion in pixels; zero disables refraction. */
    float refraction;
    /* Positive draws individual grains at this world-space radius; zero uses
     * the continuous fluid surface. Does not change collision support. */
    float grain_radius;
    CF_V2 velocity;
}
GameFluidVolume;

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

typedef enum
{
    GAME_SHAPE_BOX = 0,
    GAME_SHAPE_CIRCLE,
    GAME_SHAPE_DIAMOND,
    GAME_SHAPE_CHEVRON,
    GAME_SHAPE_STROKED_CHEVRON,
    GAME_SHAPE_CAPSULE,
    GAME_SHAPE_TRIANGLE,
    GAME_SHAPE_STROKED_TRIANGLE,
} GameShape;

typedef NODE_COMPONENT_TYPE_DENSE(GameAppearance)
{
    float width;
    float height;
    unsigned color NODE_COLOR;
    GameShape shape;
    float border_width;
    unsigned border_color NODE_COLOR;
    float glow_radius;
    unsigned glow_color NODE_COLOR;
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

typedef NODE_COMPONENT_TYPE_DENSE(GameMaterial)
{
    unsigned scene NODE_COLOR;
    unsigned albedo NODE_COLOR;
    unsigned filter NODE_COLOR;
    unsigned emission_color NODE_COLOR;
    float emission_intensity;
    float emission_multiplier;
    float absorption;
    unsigned border_scene NODE_COLOR;
    unsigned border_emission_color NODE_COLOR;
    unsigned border_albedo NODE_COLOR;
    unsigned border_filter NODE_COLOR;
    /* Gain of light received near the surface of a solid, reaching
     * GameHRC2D.surface_radius into it. Zero disables; no unlit outline. */
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

/* Display transform from scene-linear HDR. AgX is filmic and desaturates
 * highlights; Neutral keeps base colors close to their authored values. */
typedef enum
{
    GAME_TONE_MAPPING_AGX NODE_NAME("AgX"),
    GAME_TONE_MAPPING_NEUTRAL NODE_NAME("Neutral"),
} GameToneMapping;

/* Scene-wide display settings. Exposure is in stops; bloom radius is a fraction
 * of viewport height. Zero strength disables bloom. The 3D ambient light blends
 * from the ground color below to the sky color above; colors are 0xRRGGBBAA in
 * Material's linear convention and the intensity scales both. Auto exposure
 * meters the mean log luminance of the frame's lit pixels to middle gray, then
 * applies exposure as compensation; it adapts at adaptation_speed per scene
 * second, and zero adapts every frame. */
typedef NODE_COMPONENT_TYPE_DENSE(GameEnvironment)
{
    float exposure;
    float bloom_strength;
    float bloom_threshold;
    float bloom_radius;
    unsigned ambient_sky NODE_COLOR;
    unsigned ambient_ground NODE_COLOR;
    float ambient_intensity;
    GameToneMapping tone_mapping;
    bool auto_exposure;
    float adaptation_speed;
}
GameEnvironment;

static inline GameEnvironment game_environment_defaults()
{
    return (GameEnvironment){ .bloom_strength = .18f,
                              .bloom_threshold = 1,
                              .bloom_radius = .012f,
                              .ambient_sky = 0x2b3345ffu,
                              .ambient_ground = 0x0f121affu,
                              .ambient_intensity = 1,
                              .adaptation_speed = 2 };
}

/* Light from an infinitely distant source, such as the sun. Direction is the
 * world-space direction the light travels; it need not be normalized. Color
 * is 0xRRGGBBAA in Material's linear convention. */
typedef NODE_COMPONENT_TYPE_DENSE(GameDirectionalLight)
{
    unsigned color NODE_COLOR;
    float intensity;
    CF_V3 direction;
}
GameDirectionalLight;

static inline GameDirectionalLight game_directional_light_defaults()
{
    return (GameDirectionalLight){ .color = 0xffffffffu,
                                   .intensity = 2.8f,
                                   .direction = { .45f, -.85f, -.4f } };
}

/* Local scene-linear light. Radius and center elevation use project units;
 * color is 0xRRGGBBAA, matching other authored color fields. */
typedef NODE_COMPONENT_TYPE_DENSE(GamePointLight)
{
    unsigned color NODE_COLOR;
    float intensity;
    float radius;
    float elevation;
}
GamePointLight;

/* A colored activity signal for material effects, independent of lighting. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSurfaceInfluence)
{
    unsigned color NODE_COLOR;
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

/* Opts a 3D visual into the planar material renderer; collision remains 2D. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMeshSurface2D)
{
    bool enabled;
}
GameMeshSurface2D;

/* Bounded 3D distance graph evaluated in authored units; lighting stays planar. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSdfSurface3D)
{
    bool enabled;
    /* Optional simpler Shape graph below the projected pixel span. Both
     * shapes must fit the same Geometry3D bounds and parameter bindings. */
    const char *low_detail_shape;
    float low_detail_pixels;
}
GameSdfSurface3D;

typedef enum
{
    GAME_SURFACE_SUPERSAMPLED,
    GAME_SURFACE_FULL,
} GameSurfaceResolution;

typedef enum
{
    GAME_SDF_NORMAL_CENTRAL,
    GAME_SDF_NORMAL_TETRAHEDRAL,
    GAME_SDF_NORMAL_ANALYTIC,
} GameSdfNormalMode;

/* Optional scene surface quality. Zero initialization preserves the reference
 * renderer. Place one on the camera or another scene settings entity. */
typedef NODE_COMPONENT_TYPE_DENSE(GameSurfaceQuality)
{
    /* Supersampled uses twice each canvas dimension; Full uses one sample. */
    GameSurfaceResolution resolution;
    /* Projected pixel tolerance for refined SDF tracing; zero uses the
     * original distance-bound tolerance. Supported range is zero to one. */
    float sdf_pixel_error;
    /* Central uses six field samples; Tetrahedral uses four. */
    GameSdfNormalMode sdf_normal_mode;
    /* Reference comparison retaining each object's full-detail graph. */
    bool disable_sdf_lod;
}
GameSurfaceQuality;

/* Optional imported triangle geometry; Surface graphs read its exported UVs. */
typedef NODE_COMPONENT_TYPE_DENSE(GameMeshAsset)
{
    const char *source;
}
GameMeshAsset;

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
typedef enum
{
    GAME_PARTICLE_ROUND,
    GAME_PARTICLE_TRIANGLE,
    GAME_PARTICLE_TRIANGLE_OUTLINE,
    GAME_PARTICLE_RING,
    GAME_PARTICLE_STREAK,
} GameParticleShape;
#define GAME_PARTICLE_SHAPE_COUNT (GAME_PARTICLE_STREAK + 1)

typedef NODE_COMPONENT_TYPE_DENSE(GameParticleEmitter)
{
    GameParticleShape shape;
    unsigned color NODE_COLOR;
    unsigned end_color NODE_COLOR;
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
    GameFluidKind fluid_kind;
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
    unsigned color NODE_COLOR;
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
    /* Zero is orthographic; positive distance places the eye along the
     * camera's depth axis from its target. */
    float focal_distance;
    /* Radians. Yaw turns around world +Y; pitch tilts away from the default
     * top-down view. Transform and offset give the target's gameplay position. */
    float yaw;
    float pitch;
    /* Turns the image right/down axes around the view direction. */
    float roll;
    float target_elevation;
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
    GameControl source;
    const char *action;
    bool firing;
    bool disabled;
    bool pressed;
}
GameActionBinding;

/* Shows the ui asset content/ui/<document>.rml over the game view, above
 * documents with a lower order. An element's emit('Event') binding, as in
 * data-event-click="emit('Resume')", sends that event to this entity; an
 * emitting input's value is read with Get UI Value and `#id`. UI nodes set
 * text, classes and values on the elements an RCSS selector matches. */
typedef NODE_COMPONENT_TYPE_DENSE(GameUi)
{
    const char *document;
    int order;
    bool hidden;
}
GameUi;

/* One optional finite visibility grid. The main view conceals unseen space. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFog)
{
    CF_V2 origin;
    int columns;
    int rows;
    float cell_size;
    float drift; /* Zero disables ambient fog motion. */
    unsigned color NODE_COLOR;
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
    unsigned color NODE_COLOR;
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
    unsigned color_a NODE_COLOR;
    unsigned color_b NODE_COLOR;
    unsigned color_c NODE_COLOR;
    unsigned color_d NODE_COLOR;
}
GameStarfield2D;

/* Color detail of HRC lighting relative to its resolution. */
typedef enum
{
    GAME_HRC_COLOR_FULL,
    GAME_HRC_COLOR_HALF,
    GAME_HRC_COLOR_QUARTER,
    GAME_HRC_COLOR_EIGHTH,
} GameHRCColor;

/* Resolution of HRC's received-light image relative to the canvas. */
typedef enum
{
    GAME_HRC_GATHER_FULL,
    GAME_HRC_GATHER_HALF,
    GAME_HRC_GATHER_AUTO,
} GameHRCGather;

typedef enum
{
    GAME_HRC_UPSCALE_PLAIN_BILINEAR NODE_NAME("Plain bilinear"),
    GAME_HRC_UPSCALE_EDGE_AWARE NODE_NAME("Edge aware"),
    GAME_HRC_UPSCALE_JOINT_BILATERAL NODE_NAME("Joint bilateral"),
    GAME_HRC_UPSCALE_FIVE_TAP_CROSS NODE_NAME("Five tap cross"),
} GameHRCUpscale;

typedef enum
{
    GAME_HRC_DEBUG_NORMAL,
    GAME_HRC_DEBUG_SCENE,
    GAME_HRC_DEBUG_EMISSION,
    GAME_HRC_DEBUG_ABSORPTION,
    GAME_HRC_DEBUG_ALBEDO,
    GAME_HRC_DEBUG_FLUENCE,
    GAME_HRC_DEBUG_NORMALS,
    GAME_HRC_DEBUG_SURFACE_LIGHT NODE_NAME("Surface light"),
    GAME_HRC_DEBUG_LIGHT_DIRECTION NODE_NAME("Light direction"),
    GAME_HRC_DEBUG_SHADING_NORMALS NODE_NAME("Shading normals"),
} GameHRCDebugView;

typedef enum
{
    GAME_HRC_TONE_CLAMP,
    GAME_HRC_TONE_AGX NODE_NAME("AgX"),
} GameHRCToneMapping;

typedef enum
{
    GAME_HRC_SHADOW_REFERENCE,
    GAME_HRC_SHADOW_HORIZON4,
    GAME_HRC_SHADOW_HORIZON8,
} GameHRCShadowMode;

typedef enum
{
    GAME_HRC_SURFACE_BLUR,
    GAME_HRC_SURFACE_GATHER,
    GAME_HRC_SURFACE_DEPTH NODE_NAME("Depth thickness"),
} GameHRCSurfaceMode;

/* Maximum source tiles per axis before bounded child refinement. */
typedef enum
{
    GAME_HRC_DEPTH_LOW NODE_NAME("Low"),
    GAME_HRC_DEPTH_BALANCED NODE_NAME("Balanced"),
    GAME_HRC_DEPTH_HIGH NODE_NAME("High"),
} GameHRCDepthQuality;

typedef enum
{
    GAME_HRC_DEPTH_VISIBILITY_MASK NODE_NAME("Angular masks"),
    GAME_HRC_DEPTH_VISIBILITY_SEGMENTS NODE_NAME("Bounded segments"),
} GameHRCDepthVisibility;

typedef enum
{
    GAME_HRC_DEPTH_EMITTERS_VISIBLE NODE_NAME("Visible surfaces"),
    GAME_HRC_DEPTH_EMITTERS_PROXIES NODE_NAME("Uniform material proxies"),
} GameHRCDepthEmitters;

typedef NODE_COMPONENT_TYPE_DENSE(GameHRC2D)
{
    /* Finest light grid as a fraction of the padded canvas; 0.5 solves one
     * probe per 2x2 pixels. Brightness keeps this detail, and most lighting
     * cost scales with its area. 1, 0.5, 0.25 and 0.125 give whole-pixel
     * cells. With adaptive on, the highest resolution it uses. */
    float resolution;
    /* Color detail relative to resolution. Below Full, a brightness-only
     * transport solves at resolution and color on a grid 2, 4 or 8 times
     * coarser; color then blurs where differently colored lights meet. */
    GameHRCColor color_resolution;
    /* Received-light image: gather, temporal filtering and bounce, at the full
     * canvas or half of it. Half saves about 2 ms at 1080p and softens sharp
     * shadow edges. Auto lets adaptive choose, before it lowers resolution. */
    GameHRCGather gather_resolution;
    /* Steps lighting down while the scene's GPU time misses target_fps, first
     * gather when Auto, then resolution by halves down to min_resolution, and
     * back up when there is room. It measures GPU time, so frames limited by
     * the CPU or vsync keep their lighting. Needs Metal's GPU timestamps. */
    bool adaptive;
    /* Frame rate adaptive keeps the scene's GPU work within. */
    float target_fps;
    /* Lowest resolution adaptive may choose. */
    float min_resolution;
    /* Gain of the unlit scene color added to the frame. */
    float ambient;
    /* Brightness of emissive surfaces as drawn; the light they cast is
     * unchanged. */
    float intensity;
    /* Brightness of light seen in open air: the glow around lights. */
    float fog;
    /* Fraction of a solid's albedo that stays visible where no light reaches
     * it. */
    float shadow_floor;
    /* How quickly light dims with distance; zero keeps it at full strength
     * across the view. */
    float falloff;
    /* World-space raster guard around the camera for nearby light transport.
     * The default, zero, leaves offscreen sources and occluders out. */
    float lighting_margin;
    /* Gain of light that lit surfaces reflect back into the transport; needs
     * feedback. */
    float bounce_intensity;
    /* Current-frame weight of bounce light: 1 disables temporal smoothing, not
     * bounce, and lower values retain more history. Camera movement resets it. */
    float bounce_hysteresis;
    /* Current-frame weight of received light: 1 disables temporal smoothing,
     * and lower values retain more history. */
    float radiance_hysteresis;
    /* How received light is upsampled from the transport grid: 0 bilinear,
     * 1 bilinear that turns nearest at absorption edges, 2 joint bilateral on
     * absorption, 3 five-tap cross. Ignored with a luminance transport. */
    GameHRCUpscale upscale_mode;
    /* Absorption difference that upscale modes 1 and 2 treat as an edge. */
    float upscale_threshold;
    /* 0 draws the lit frame; 1 scene, 2 emission, 3 transmittance, 4 albedo,
     * 5 received light, 6 normals, 7 surface light, 8 dominant light
     * direction, 9 shading normals. */
    GameHRCDebugView debug_view;
    /* Perspective distance for particles: raised particles scale by
     * focal / (focal + elevation). */
    float particle_focal_length;
    /* Cascade levels traced through the scene. Higher levels are extended
     * from the ones below, which is cheaper but less exact. */
    int trace_levels;
    /* Feeds light reflected by lit surfaces back into the transport as bounce
     * light. */
    bool feedback;
    /* World units that light received near a solid's surface reaches into it,
     * scaled by GameMaterial.edge_light. Zero disables surface lighting. */
    float surface_radius;
    /* Blur spreads open-space light; Gather uses bounded visibility probes.
     * Depth thickness gathers visible emission with finite-depth occluders. */
    GameHRCSurfaceMode surface_mode;
    /* Screen-plane reach in project units for Depth thickness mode. */
    float surface_depth_radius;
    /* Gain of the visible Depth contribution added to HRC. Zero disables it. */
    float surface_depth_gain;
    /* Source sampling budget; all levels preserve integrated emission. */
    GameHRCDepthQuality surface_depth_quality;
    /* Segments share one bounded visibility query per coarse source tile. */
    GameHRCDepthVisibility surface_depth_visibility;
    /* Independent proxies preserve camera-hidden uniform material emitters;
     * shader-authored emission is excluded in this experimental source mode. */
    GameHRCDepthEmitters surface_depth_emitters;
    /* Refine nearby sources whose directions differ across a source tile. */
    bool surface_depth_near_detail;
    /* Blend visibility bins and reconstruction boundaries, in [0, 1]. */
    float surface_depth_shadow_softness;
    /* Reproject and validate a short history of the Depth contribution. */
    bool surface_depth_temporal;
    /* Maximum previous-frame weight; zero selects the default of 0.65. */
    float surface_depth_history;
    /* Assumed camera-depth thickness of visible surfaces in project units. */
    float surface_thickness;
    /* Sideways reach of nearby light relative to its height above the plane;
     * larger values deepen normal-mapped relief. */
    float surface_relief;
    /* Local heightfield visibility; horizons store four or eight azimuths. */
    GameHRCShadowMode surface_shadow_mode;
    /* Independent horizon experiments. False keeps the reference submission. */
    bool horizon_fuse_relief;
    bool horizon_cache;
    bool horizon_reproject;
    bool horizon_hierarchy;
    /* Outward tilt of solid normals near silhouettes, which catches rim light. */
    float surface_bevel;
    /* Gain of glossy highlights on solids with low material roughness. */
    float specular;
    /* 0 clamps the composite to the display range. 1 keeps it in HDR and
     * presents it with bloom, exposure and AgX tone mapping. */
    GameHRCToneMapping tone_mapping;
    /* Exposure in EV before tone mapping. */
    float exposure;
    /* Fraction of the blurred image mixed into the frame, in [0, 1]. */
    float bloom;
}
GameHRC2D;

static inline GameHRC2D game_hrc2d_defaults()
{
    return (GameHRC2D){ .resolution = 1,
                        .adaptive = true,
                        .target_fps = 60,
                        .min_resolution = .25f,
                        .intensity = 7.5f,
                        .fog = 1,
                        .falloff = 1,
                        .bounce_intensity = 1,
                        .bounce_hysteresis = .8f,
                        .radiance_hysteresis = .8f,
                        .upscale_mode = GAME_HRC_UPSCALE_EDGE_AWARE,
                        .upscale_threshold = .1f,
                        .particle_focal_length = 300,
                        .trace_levels = 3,
                        .feedback = true,
                        .surface_radius = 24,
                        .surface_depth_radius = 128,
                        .surface_depth_gain = 1,
                        .surface_depth_quality = GAME_HRC_DEPTH_BALANCED,
                        .surface_depth_history = .65f,
                        .surface_thickness = 12,
                        .surface_relief = 1.5f,
                        .surface_bevel = 1,
                        .specular = 1 };
}

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
