#ifndef CHIBI_GAME_COMPONENTS_H
#define CHIBI_GAME_COMPONENTS_H

#include "graph/model/annotate.h"

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
    /* Fraction of the scene's gravity a dynamic body feels. Ships held by a
     * Drive use zero; loose debris uses one. */
    float gravity_scale;
    /* Dynamic bodies turn under contacts and write Orientation; otherwise
     * their rotation stays locked. */
    bool free_rotation;
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

typedef enum
{
    GAME_FOLLOW_ATTACH,
    GAME_FOLLOW_CHAIN
} GameFollowMode;

/* Keeps this entity on target after each physics step. The anchor is the
 * target's Transform plus offset turned by the target's Orientation. Attach
 * pins the entity at the anchor (turrets, boss parts); Chain keeps it distance
 * from the anchor along the line between them (worm segments, tails). With
 * inherit_heading, Attach copies the target's Orientation and Chain turns to
 * face the anchor at stiffness per second, or at once when zero. Velocity
 * copies the target's, so bodies move with it through contacts. A destroyed
 * target clears target and the entity stays where it is. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFollow)
{
    uint64_t target NODE_HANDLE(ecs.entity);
    GameFollowMode mode;
    CF_V2 offset;
    float distance;
    float stiffness;
    bool inherit_heading;
}
GameFollow;

typedef enum
{
    GAME_SCATTER_AIR,
    GAME_SCATTER_FLOOR,
    GAME_SCATTER_CEILING,
    GAME_SCATTER_WALL,
    GAME_SCATTER_EMBEDDED
} GameScatterPlacement;

/* Places prototype on a grid of cell-sized squares over [min, max] as cameras
 * come within radius of it. Each cell tries one jittered point once per run,
 * chosen by the run seed, salt and the cell, and spawns there with probability
 * chance when the point fits terrain, the MaterialTerrain entity whose
 * GameGlobalBinding name this is. Air needs clearance of open space; Floor,
 * Ceiling and Wall need rock within clearance below (+y), above or beside, and
 * snap onto it with Orientation facing out of the rock; Embedded needs rock
 * clearance deep. Spawns are never despawned or replaced. */
typedef NODE_COMPONENT_TYPE_DENSE(GameScatter)
{
    const char *prototype;
    const char *terrain;
    CF_V2 min;
    CF_V2 max;
    float cell;
    float chance;
    GameScatterPlacement placement;
    float clearance;
    int salt;
    float radius;
}
GameScatter;

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

/* Optional material world. The camera activates nearby chunks; moving bodies
 * wake additional local regions. Population limits bound retained resources. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialFluidWorld)
{
    float chunk_size;
    float buffer;
    float kernel;
    int particle_limit;
    /* Optional gas flow; a zero cell size leaves particles independent. */
    float gas_cell_size;
    float gas_vorticity;
    float gas_drag;
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
    /* Burning ignitions push gas, bodies and debris within blast_radius (at
     * most 1024) and damage what they reach. */
    float blast_radius;
    float blast_speed;
    float blast_damage;
    /* Fraction of the temperature difference with touching matter exchanged
     * per second. */
    float heat_conduction;
    /* Hot matter glows from half this temperature and fully at it, shifting
     * toward glow_color and adding glow_emission. Zero disables the glow. */
    float glow_temperature;
    unsigned glow_color;
    float glow_emission;
}
MaterialFluidWorld;

typedef enum
{
    MATERIAL_PHASE_GAS,
    MATERIAL_PHASE_LIQUID,
    MATERIAL_PHASE_POWDER,
    MATERIAL_PHASE_ELASTIC
} MaterialPhase;

/* A kind of matter, named by its prototype. Heat is a gameplay temperature:
 * zero is ambient, and matter cools toward it by `cooling` per second. A
 * product names another substance or a MaterialTerrain.deposit. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialSubstance)
{
    MaterialPhase phase;
    /* Mass per area; one matches rigid bodies. */
    float density;
    /* Liquid, powder and elastic matter, as fractions 0..1 of what the solver
     * keeps stable: resistance to compression or deformation, and liquid
     * viscosity. */
    float stiffness;
    float viscosity;
    /* Share 0..1 of tension a liquid keeps, holding it together. */
    float cohesion;
    /* 0..1: a powder's friction angle up to 45 degrees, and its grip on
     * solids for every dense phase. */
    float friction;
    /* Gas only: the fraction of gravity that lifts it, times 1 + temperature.
     * Negative gas sinks. */
    float buoyancy;
    unsigned color;
    float emission;
    float absorption;
    /* Camera-only minimum visibility; contributes no radiance or bounce. */
    float visibility;
    /* Screen-space liquid distortion in pixels; zero disables refraction. */
    float refraction;
    /* Temperature it forms at. */
    float temperature;
    float cooling;
    /* Seconds it lasts; zero is persistent. */
    float lifetime;
    /* Positive ignite_at makes it flammable. At that temperature it starts
     * burning: it stays at least burn_heat for 1 / burn_rate seconds, then
     * becomes burns_to, or vanishes when that is empty. Each ignition adds
     * blast to its neighborhood's explosion. */
    float ignite_at;
    float burn_rate;
    float burn_heat;
    const char *burns_to;
    float blast;
    /* At or above boil_at it becomes boils_to; empty never boils. */
    float boil_at;
    const char *boils_to;
    /* At or below freeze_at it becomes freezes_to; empty never freezes. */
    float freeze_at;
    const char *freezes_to;
    /* Damage per second to the solids it touches. */
    float corrosion;
    /* Carries electric arcs. */
    bool conductive;
}
MaterialSubstance;

/* Where substance `a` touches `b` (a substance, or Burning for anything on
 * fire), each pair reacts with probability `rate` per second: a becomes
 * a_becomes and b becomes b_becomes (empty keeps it), and both gain heat. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialReaction)
{
    const char *a;
    const char *b;
    float rate;
    const char *a_becomes;
    const char *b_becomes;
    float heat;
}
MaterialReaction;

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
    unsigned dust_color;
    /* Hot strikes add spark_density sparks per crumbled cell per unit of the
     * striker's heat. Sparks start in the striker's light color, cool through
     * spark_color and fade. */
    float spark_density;
    unsigned spark_color;
    float spark_emission;
    /* Rock chunks thrown per crater: one per chunk_cells crumbled cells, at
     * most chunk_limit. Sizes are world-unit radii. */
    int chunk_limit;
    float chunk_cells;
    float chunk_size_min, chunk_size_max;
    float chunk_speed_min, chunk_speed_max;
    float chunk_lifetime;
    /* Parcels of smoke_substance released per crater; zero disables. */
    int smoke_count;
    const char *smoke_substance;
    unsigned smoke_color;
    float smoke_lifetime;
    /* Outward drift speed of the released gas in world units per second. */
    float smoke_speed;
}
MaterialBreakProfile;

/* Heat a body or emitter carries into matter: positive heats and ignites,
 * negative chills. Heat is a gameplay temperature, not kelvin. */
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
    /* Fraction of corrosion it shrugs off: zero dissolves, one is immune. */
    float corrosion_resistance;
    /* Matter at least this hot melts touching cells into melts_to; zero
     * never melts. */
    float melts_at;
    const char *melts_to;
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
     * cameras; zero width and height then allow unbounded terrain. Generation
     * is limited to stream_budget tiles per boundary. */
    float stream_radius;
    int stream_budget;
    /* Solid islands and air pockets smaller than this many cells that lie
     * wholly inside a tile are removed at generation; zero keeps them. */
    int island_cells;
    /* Mixes the run seed into seed, so every run generates new terrain and a
     * pinned run seed generates the same terrain again. */
    bool seed_from_run;
    /* Matter turning into this name adds cells here; empty receives none. */
    const char *deposit;
}
MaterialTerrain;

/* A one-time, elliptical fill of a substance in local space; its matter then
 * lives freely. Nonzero look fields replace the substance's for this fill. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFluidVolume)
{
    const char *substance;
    float width;
    float height;
    CF_V2 velocity;
    unsigned color;
    float absorption;
    float emission;
    float visibility;
    float refraction;
    /* The fill belongs to this entity: Matter Centroid and Push Matter find
     * it, and Damage Material on the entity carves it. */
    bool owned;
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

/* Shot state that a weapon writes and the shot's own behaviors read. */
typedef NODE_COMPONENT_TYPE_DENSE(GameProjectile)
{
    float speed;
    float lifetime;
    int damage;
    float age;
    /* Radians per second the shot turns toward its target. */
    float homing;
    /* Speed gained per second; negative slows the shot. */
    float acceleration;
    /* Bodies the shot passes through before it stops. */
    int pierce;
    /* Wall reflections left. */
    int bounce;
    /* Shards released when the shot ends. */
    int split;
    /* Authored element code the target's behaviors interpret. */
    int element;
    /* Blast radius where the shot ends; zero for none. */
    float blast;
    /* First module the source casts where the shot triggers; -1 for none. */
    int payload;
    /* Authored trigger code: when the payload is cast. */
    int trigger;
    /* Seconds after launch a timed trigger fires. */
    float delay;
    float knockback;
    /* The entity whose weapon fired the shot. */
    uint64_t source NODE_HANDLE(ecs.entity);
    /* Which of the source's weapons fired the shot. */
    int hardpoint;
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
    unsigned color;
    GameShape shape;
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
    /* Emits into the material world as this substance, keeping the
     * emitter's look; empty emits ordinary particles. */
    const char *fluid_substance;
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

/* Trauma-driven shake for a GameCamera's view. Add Camera Trauma raises
 * trauma (0 to 1) and it falls by decay per real second. The view moves up to
 * max_offset units along smooth noise, frequency cells per second, scaled by
 * trauma squared; the camera entity itself stays put. */
typedef NODE_COMPONENT_TYPE_DENSE(GameCameraShake)
{
    float trauma;
    float decay;
    float max_offset;
    float frequency;
}
GameCameraShake;

/* Behaviors on this entity tick in real time: Tick's Delta, Delta Time and
 * Delay ignore Set Time Scale and Hit Stop, so menus keep running while the
 * game is paused at scale 0. The clock switches when the entity's behaviors
 * have no waiting runs. Disabled follows the run's time scale again. */
typedef NODE_COMPONENT_TYPE_DENSE(GameUnscaledTime)
{
    bool disabled;
}
GameUnscaledTime;

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
    GAME_CONTROL_DIGIT_1,
    GAME_CONTROL_DIGIT_2,
    GAME_CONTROL_DIGIT_3,
    GAME_CONTROL_DIGIT_4,
    /* Held for the frames the wheel turns: one press per notch frame. */
    GAME_CONTROL_WHEEL_UP,
    GAME_CONTROL_WHEEL_DOWN,
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

/* Optional grading and lens effects for tone-mapped 2D scenes
 * (GameHRC2D.tone_mapping). Zero leaves every field neutral. */
typedef NODE_COMPONENT_TYPE_DENSE(GamePost2D)
{
    /* Darkening toward the corners, 0 to 1. */
    float vignette;
    /* Red and blue split at the screen corners, in output pixels. */
    float chromatic_aberration;
    /* Film grain amplitude in display units; 0.04 is subtle. */
    float grain;
    /* Added to one: 0.2 raises contrast by a fifth, -1 flattens it. */
    float contrast;
    /* Added to one: -1 is grayscale. */
    float saturation;
    /* White balance from cool (-1) to warm (1). */
    float temperature;
    /* White balance from green (-1) to magenta (1). */
    float tint;
    /* Display color added to the shadows. */
    CF_V3 lift;
    /* Added to one as a per-channel display multiplier. */
    CF_V3 gain;
    /* Output pixels of shimmer over burning fluid. */
    float heat_haze;
}
GamePost2D;

/* Tints an entity's drawn material toward color by amount and adds that color
 * as emission, so a hit lights its surroundings. Amount falls by decay per
 * second. */
typedef NODE_COMPONENT_TYPE_DENSE(GameFlash)
{
    unsigned color;
    float amount;
    float decay;
}
GameFlash;

/* Shimmers the frame behind an entity, as heat does above fire. */
typedef NODE_COMPONENT_TYPE_DENSE(GameHeatHaze)
{
    /* World units from the entity's center. */
    float radius;
    /* Output pixels of displacement at the center. */
    float strength;
}
GameHeatHaze;

/* A material graph filling the screen behind the 2D scene, over the starfield.
 * Its Position is the offset from the camera center and its World Position
 * follows the camera by parallax: 0 pins the layer to the camera, 1 moves it
 * with the world. Layers draw in ascending layer order. Base Color and
 * Emission are scaled by intensity; lit multiplies Base Color by blurred
 * received light, so nearby lights pick the layer out of the dark. */
typedef NODE_COMPONENT_TYPE_DENSE(GameBackdrop2D)
{
    const char *material;
    float parallax;
    int layer;
    float intensity;
    bool lit;
}
GameBackdrop2D;

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
    int upscale_mode;
    /* Absorption difference that upscale modes 1 and 2 treat as an edge. */
    float upscale_threshold;
    /* 0 draws the lit frame; 1 scene, 2 emission, 3 transmittance, 4 albedo,
     * 5 received light, 6 normals, 7 surface light, 8 dominant light
     * direction, 9 shading normals. */
    int debug_view;
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
    /* Sideways reach of nearby light relative to its height above the plane;
     * larger values deepen normal-mapped relief. */
    float surface_relief;
    /* Outward tilt of solid normals near silhouettes, which catches rim light. */
    float surface_bevel;
    /* Gain of glossy highlights on solids with low material roughness. */
    float specular;
    /* 0 clamps the composite to the display range. 1 keeps it in HDR and
     * presents it with bloom, exposure and AgX tone mapping. */
    int tone_mapping;
    /* Exposure in EV before tone mapping. */
    float exposure;
    /* Fraction of the blurred image mixed into the frame, in [0, 1]. */
    float bloom;
    /* Strength of caustics: light that rippling liquid focuses into bright
     * lines. Zero disables them; 0.3 is typical. Liquid refraction scales it
     * per volume. Caustics light the surroundings through feedback. */
    float caustics;
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
                        .upscale_mode = 1,
                        .upscale_threshold = .1f,
                        .particle_focal_length = 300,
                        .trace_levels = 3,
                        .feedback = true,
                        .surface_radius = 24,
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
