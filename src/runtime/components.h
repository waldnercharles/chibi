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
    uint64_t ignore_entity NODE_HANDLE(ecs.entity);
}
GameCollider;

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

/* Local emitter pose follows the rendered source; emitted particles retain a world-space tail. */
typedef NODE_COMPONENT_TYPE_DENSE(GameParticleEmitter)
{
    int shape;
    unsigned color;
    unsigned end_color;
    float rate;
    float lifetime;
    float speed;
    float size;
    float spread;
    float drag;
    float emission;
    float size_exponent;
    float velocity_inherit;
    CF_V3 offset;
    CF_V3 direction;
    bool heading_only; /* Ignore visual pitch/yaw/roll for the emitter's local pose. */
}
GameParticleEmitter;

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
    GameControl source;
    const char *action;
    bool firing;
    bool disabled;
}
GameActionBinding;

typedef enum
{
    GAME_WIDGET_LABEL,
    GAME_WIDGET_PANEL,
    GAME_WIDGET_BUTTON,
    GAME_WIDGET_PROGRESS,
    GAME_WIDGET_SLIDER,
    GAME_WIDGET_SLOT,
    GAME_WIDGET_MAP,
} GameWidgetKind;

/* UI offsets and sizes are logical pixels at a 720-pixel viewport height.
 * Anchors and pivots are fractions. Behavior owns contents, layout and rules.
 * An interactive widget sends its named event (Source: entity, Value: float).
 * Matching nonempty drop groups permit transfers; behaviors decide validity. */
typedef NODE_COMPONENT_TYPE_DENSE(GameWidget)
{
    GameWidgetKind kind;
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
typedef NODE_COMPONENT_TYPE_DENSE(GameLighting2D)
{
    float ambient;
    float emission;
    float fog;
    float shadow_floor;
}
GameLighting2D;

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

#endif
