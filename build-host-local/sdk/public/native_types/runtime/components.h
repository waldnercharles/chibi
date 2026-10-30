#ifndef CHIBI_COMPONENTS_H
#define CHIBI_COMPONENTS_H

#include "types/annotate.h"

#include <cute_math.h>
#include <cute_math3d.h>
#include <stdbool.h>
#include <stdint.h>

typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 Transform;
typedef NODE_COMPONENT_ALIAS(CF_V2) CF_V2 Velocity;

/* Publishes this entity as a named global entity value. Behaviors can resolve
 * it without knowing how the entity was instantiated. */
typedef NODE_COMPONENT_TYPE_DENSE(GlobalBinding)
{
    const char *name;
}
GlobalBinding;

typedef enum
{
    BODY_STATIC,
    BODY_DYNAMIC,
    BODY_KINEMATIC
} BodyType;
typedef enum
{
    COLLIDER_CIRCLE,
    COLLIDER_BOX
} ColliderShape;

/* All spatial values use project units. There are 32 project units per meter. */
#define UNITS_PER_METER 32.0f

typedef NODE_COMPONENT_TYPE_DENSE(Body)
{
    BodyType type;
    bool continuous;
    float angular_velocity NODE_UNIT("rad/s"); /* Kinematic radians/second; requires Orientation. */
    bool allow_rotation;
    /* A dynamic body's mass, 1 kg per square meter of its collider and the
     * solid colliders it carries; physics writes it after each step. */
    float mass NODE_UNIT("kg");
}
Body;

/* The scene's 2D gravity in units/second^2, on at most one entity. It pulls
 * dynamic bodies and material fluids; without one, nothing falls. */
typedef NODE_COMPONENT_TYPE_DENSE(Gravity)
{
    CF_V2 acceleration;
}
Gravity;

typedef NODE_COMPONENT_TYPE_DENSE(Collider)
{
    float radius NODE_UNIT("u");
    unsigned category;
    unsigned mask;
    ColliderShape shape;
    float width;
    float height;
    bool sensor;
    bool pierce_fragments; /* Continuous sensors report debris hits without stopping. */
    uint64_t ignore_entity NODE_HANDLE(ecs.entity);
    float restitution NODE_RANGE(0, 1);
    float friction NODE_RANGE_HINT(0, 1);
}
Collider;

/* Target velocity with finite acceleration; physics owns actual velocity. */
typedef NODE_COMPONENT_TYPE_DENSE(Drive)
{
    CF_V2 target;
    float acceleration NODE_UNIT("u/s²");
}
Drive;

typedef enum
{
    JOINT_HINGE,
    JOINT_SLIDER,
    JOINT_WELD,
    JOINT_DISTANCE
} JointKind;

/* Joins the body at or above the entity, the nearest one with a Body or a
 * static collider of its own, to other's (zero: the world), at the entity's
 * position. A hinge turns about it, a slider moves along the entity's local
 * Y axis and a weld holds; a dynamic body turns only with allow_rotation.
 * Each holds the bodies as they were when it was made, and is made again
 * when its fields change or its entity moves on its body. A distance joint
 * keeps the entity at its first distance from other's origin. limit keeps a
 * hinge's angle (within 0.99 pi either way), a slider's offset or a distance
 * (at least zero) between lower and upper. motor drives a hinge, slider or
 * distance at speed with at most strength. spring pulls a hinge, slider or
 * weld back to where it was made at hertz and damping, and makes a distance
 * joint elastic: its limit and motor act only then. collide lets the two
 * bodies touch and report contacts. */
typedef NODE_COMPONENT_TYPE_DENSE(Joint)
{
    JointKind kind;
    uint64_t other NODE_HANDLE(ecs.entity);
    bool collide;
    bool limit;
    float lower;
    float upper;
    bool motor;
    float speed;
    float strength;
    bool spring;
    float hertz NODE_UNIT("Hz");
    float damping;
}
Joint;

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
    MaterialDebugView debug_view;
    /* Optional gas flow; a zero cell size leaves particles independent. */
    float gas_cell_size;
    float gas_vorticity;
    float gas_drag;
    float gas_buoyancy;
    float gas_ambient_heat;
    /* Optional transient visual responses to fast fluid/body contacts. Zero
     * count disables that response; particles never add matter to the solver. */
    float gas_wake_speed NODE_UNIT("u/s"); /* Minimum normal closing speed, world units/sec. */
    float gas_wake_strength; /* Scales puff speed and response intensity. */
    int gas_wake_count;      /* Maximum visual particles per body/patch/tick. */
    float gas_wake_lifetime NODE_UNIT("s"); /* Maximum visual particle lifetime, seconds. */
    float liquid_splash_speed NODE_UNIT("u/s"); /* Minimum normal closing speed, world units/sec. */
    float liquid_splash_strength; /* Scales droplet speed and response intensity. */
    int liquid_splash_count; /* Maximum visual particles per body/patch/tick. */
    float liquid_splash_lifetime NODE_UNIT("s"); /* Maximum visual particle lifetime, seconds. */
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
    float chunk_lifetime NODE_UNIT("s");
    /* Gas parcels released into the fluid world per crater; zero disables. */
    int smoke_count;
    unsigned smoke_color NODE_COLOR;
    float smoke_lifetime NODE_UNIT("s");
    /* Outward drift speed of the released gas in world units per second. */
    float smoke_speed NODE_UNIT("u/s");
}
MaterialBreakProfile;

/* Optional local combustion. Heat is a gameplay quantity, not kelvin. */
typedef NODE_COMPONENT_TYPE_DENSE(Combustion)
{
    float ignition;
    float cooling;
    float burn_rate;
    float heat_release;
    float smoke_lifetime NODE_UNIT("s");
    unsigned smoke_color NODE_COLOR;
    float smoke_absorption;
    unsigned flame_color NODE_COLOR;
    float flame_emission;
    /* World units a burst of ignitions pushes gas, bodies and debris; at most
     * 1024. */
    float blast_radius NODE_RANGE(0, 1024) NODE_UNIT("u");
    float blast_speed;
    float blast_damage;
    /* Heat below this stops burning without consuming the remaining fuel.
     * Zero disables extinguishing; positive values must be below ignition. */
    float extinction;
}
Combustion;

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

typedef NODE_COMPONENT_TYPE_DENSE(Fuel)
{
    float amount;
}
Fuel;

typedef NODE_COMPONENT_TYPE_DENSE(Igniter)
{
    float heat;
}
Igniter;

/* Damage per surviving cell. Optical properties remain in Material. */
typedef NODE_COMPONENT_TYPE_DENSE(Substance)
{
    float hardness;
    float blast_multiplier;
    bool retain_chips;
}
Substance;

/* Optional circular void in a destructible mask, for finite sealed pockets. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialCavity)
{
    float radius NODE_UNIT("u");
}
MaterialCavity;

/* Stylized planar reflection; angle is the local mirror tangent in radians. */
typedef NODE_COMPONENT_TYPE_DENSE(Reflection)
{
    float strength;
    float roughness;
    float angle;
    float offset;
}
Reflection;

typedef NODE_COMPONENT_TYPE_DENSE(Destructible)
{
    float cell_size;
    int min_fragment_cells;
    /* Release control below this fraction of the original hull; zero disables. */
    float release_fraction;
    /* Detached fragments may be passed through by continuous sensors. */
    bool pierceable_fragments;
}
Destructible;

/* Detached material keeps its collision but gradually loses emitted light. */
typedef NODE_COMPONENT_TYPE_DENSE(FragmentGlow)
{
    float duration;
    float flicker;
}
FragmentGlow;

/* Where an entity's local frame sat when its material was laid out. Fragments
 * keep their source's frame, so world-space material patterns stay attached to
 * a broken piece as it moves and rotates. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialFrame)
{
    float x;
    float y;
    float radians NODE_UNIT("rad");
}
MaterialFrame;

/* Optional actual 3D surface derived from a live destructible material mask.
 * The mask remains authoritative in the gameplay plane. Geometry uses its
 * contour; elevation and depth are along world Y. Spacing selects in-plane
 * tessellation and must be at least the mask cell size. Relief is sampled in
 * the retained MaterialFrame. */
typedef NODE_COMPONENT_TYPE_DENSE(MaterialSurface3D)
{
    bool enabled;
    /* Consume the bound material's static Surface Height as mesh displacement,
     * sampled at PartPosition=(source X, 0, source Z). Unsupported dynamic
     * coordinates fail explicitly. Shared channels should use source X/Z. */
    bool material_height;
    float elevation NODE_UNIT("u");
    float depth NODE_UNIT("u");
    float bevel_width NODE_UNIT("u");
    float relief NODE_UNIT("u");
    float relief_scale NODE_UNIT("u");
    float spacing NODE_UNIT("u");
    unsigned seed;
    /* Cutaway cave interior. Empty cells within recess_width of the contour
     * gain rock falling up to recess_depth further from the camera, so a
     * side-on view sees cave walls receding behind the gameplay plane. It is
     * measured from published occupancy; terrain halos widen to cover it and
     * detached fragments drop it. Zero width keeps the vertical contour wall. */
    float recess_width NODE_UNIT("u");
    float recess_depth NODE_UNIT("u");
    /* Solid thinner than twice this width, such as lavacicles, still renders
     * but hangs in front of the recess instead of shaping it. */
    float recess_smoothing NODE_UNIT("u");
}
MaterialSurface3D;

/* Optional rock behind a MaterialTerrain's play plane, derived from the same
 * field sampled in 3D: Field Z is depth behind the plane, so the terrain mask
 * is the field's Z = 0 slice. The volume opens a cone wherever the live mask
 * lost rock, so cuts, fragments and restored saves agree with it; the mask
 * stays the only gameplay and saved state. Voxel is the sample spacing, depth
 * how far rock extends behind the plane, and elevation the plane's world Y.
 * scene3d draws it with the terrain's Shader material. Editing any setting
 * but elevation and front_material regenerates the terrain, dropping its
 * cuts. Unstreamed only. */
typedef NODE_COMPONENT_TYPE_DENSE(TerrainVolume)
{
    bool enabled;
    float voxel NODE_UNIT("u");
    float depth NODE_UNIT("u");
    float elevation NODE_UNIT("u");
    /* The field is searched for surfaces every voxel * refine and sampled
     * every voxel only near them, so fine detail costs memory by surface area
     * rather than volume. One samples everything every voxel. */
    int refine NODE_RANGE(1, 8);
    /* Leaves air the field has at the deepest layer open, so the sky shows
     * through; closed otherwise. */
    bool open_back;
    /* Rock ahead of the plane: it casts shadows and blocks indirect light, so
     * cutaway openings do not leak. It is drawn only with front_material. */
    float front NODE_UNIT("u");
    /* Surface material that draws the rock ahead of the plane, with its own
     * default parameters. A masked material that opens around a Surface
     * Influence cuts the foreground away near the player. */
    const char *front_material;
    /* How far cuts carve on each side of the plane. Rock beyond it never
     * changes, so a cut re-meshes only nearby slab chunks. Must be positive. */
    float slab NODE_UNIT("u");
}
TerrainVolume;

/* Optional indirect-ray proxy policy. Absent keeps built-in solids and imported
 * mesh bounds. Disable for hollow imported scenery whose bounds enclose air;
 * actual mesh surface rendering and direct shadows remain enabled. Enabled
 * live mask surfaces derive opening-preserving proxies from published occupancy. */
typedef NODE_COMPONENT_TYPE_DENSE(IndirectOccluder)
{
    bool enabled;
}
IndirectOccluder;

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
    FLUID_KIND_GAS,
    FLUID_KIND_LIQUID,
} FluidKind;

/* A one-time, elliptical fill in local space; its matter then lives freely. */
typedef NODE_COMPONENT_TYPE_DENSE(FluidVolume)
{
    FluidKind kind;
    float width;
    float height;
    float spacing;
    float mass;
    unsigned color NODE_COLOR;
    float absorption NODE_RANGE_HINT(0, 1);
    float emission;
    /* Camera-only minimum visibility; contributes no radiance or bounce. */
    float visibility;
    /* Screen-space liquid distortion in pixels; zero disables refraction. */
    float refraction NODE_UNIT("px");
    /* Positive draws individual grains at this world-space radius; zero uses
     * the continuous fluid surface. Does not change collision support. */
    float grain_radius NODE_UNIT("u");
    CF_V2 velocity;
}
FluidVolume;

typedef NODE_COMPONENT_TYPE_DENSE(Health)
{
    int current;
    int maximum;
}
Health;

typedef NODE_COMPONENT_TYPE_DENSE(Movement)
{
    float speed NODE_UNIT("u/s");
}
Movement;

typedef NODE_COMPONENT_TYPE_DENSE(Input)
{
    CF_V2 move;
}
Input;

typedef NODE_COMPONENT_TYPE_DENSE(Weapon)
{
    int damage;
    float cooldown NODE_UNIT("s");
}
Weapon;

typedef NODE_COMPONENT_TYPE_DENSE(Projectile)
{
    float speed NODE_UNIT("u/s");
    float lifetime NODE_UNIT("s");
    int damage;
    float age;
}
Projectile;

typedef enum
{
    SHAPE_BOX = 0,
    SHAPE_CIRCLE,
    SHAPE_DIAMOND,
    SHAPE_CHEVRON,
    SHAPE_STROKED_CHEVRON,
    SHAPE_CAPSULE,
    SHAPE_TRIANGLE,
    SHAPE_STROKED_TRIANGLE,
} Shape;

typedef NODE_COMPONENT_TYPE_DENSE(Appearance)
{
    float width;
    float height;
    unsigned color NODE_COLOR;
    Shape shape;
    float border_width;
    unsigned border_color NODE_COLOR;
    float glow_radius;
    unsigned glow_color NODE_COLOR;
    float stroke_width;
    float antialias NODE_RANGE_HINT(0, 1);
    int layer;
    /* World units the shape stands above the plane where its scene color
     * covers, as a Sprite's relief; a material graph's Height adds to it. In
     * Depth thickness lighting, a light standing higher passes over lower
     * shapes and reaches raised surfaces. */
    float relief NODE_UNIT("u");
}
Appearance;

/* Draws one frame of an image instead of an Appearance, one world unit per
 * texel, centred on the Transform. Frames are frame_width by frame_height
 * cells numbered left to right, then top to bottom; zero sizes use the whole
 * image. normal_map holds tangent-space normals laid out like the image, red
 * right and green up, and each texel stands relief world units above the
 * plane, times the red of height_map when there is one. 2D lighting shades
 * them through surface light, which a Material's edge_light scales; without a
 * Material, a sprite with a map takes it in full. */
typedef NODE_COMPONENT_TYPE_DENSE(Sprite)
{
    const char *image;
    int frame;
    int frame_width;
    int frame_height;
    bool flip_x;
    int layer;
    const char *normal_map;
    const char *height_map;
    float relief NODE_UNIT("u");
}
Sprite;

/* One atlas-backed grid, anchored at Transform's local top-left corner.
 * Cells are row-major whitespace-separated frame indices; -1 is empty.
 * Frame dimensions are both atlas texels and world units. normal_map,
 * height_map and relief shade and raise the tiles as they do a Sprite. */
typedef NODE_COMPONENT_TYPE_DENSE(Tilemap)
{
    const char *image;
    const char *cells;
    int columns;
    int rows;
    int frame_width;
    int frame_height;
    int layer;
    const char *normal_map;
    const char *height_map;
    float relief NODE_UNIT("u");
}
Tilemap;

/* Radians from gameplay +X toward +Y: atan2(direction.y, direction.x).
 * The 2D canvas flips Y at presentation; 3D maps gameplay Y to world Z. */
typedef NODE_COMPONENT_TYPE_DENSE(Orientation)
{
    float radians NODE_UNIT("rad");
}
Orientation;

typedef NODE_COMPONENT_TYPE_DENSE(Material)
{
    unsigned scene NODE_COLOR;
    unsigned albedo NODE_COLOR;
    unsigned filter NODE_COLOR;
    unsigned emission_color NODE_COLOR;
    float emission_intensity NODE_RANGE_HINT(0, 4);
    float emission_multiplier;
    float absorption NODE_RANGE_HINT(0, 1);
    unsigned border_scene NODE_COLOR;
    unsigned border_emission_color NODE_COLOR;
    unsigned border_albedo NODE_COLOR;
    unsigned border_filter NODE_COLOR;
    /* Gain of light received near the surface of a solid, reaching
     * HRC2D.surface_radius into it. Zero disables; no unlit outline.
     * With Depth thickness, a solid that absorbs nothing instead takes its
     * HRC light shaded by its facing and by heights in the way, this much. */
    float edge_light NODE_RANGE_HINT(0, 1);
}
Material;

typedef NODE_COMPONENT_TYPE_DENSE(Shader)
{
    const char *material;
    const char *shape;
    const char *parameters;
    float bounds_x;
    float bounds_y;
}
Shader;

enum
{
    PRIMITIVE_BOX,
    PRIMITIVE_SPHERE,
    PRIMITIVE_CAPSULE,
    PRIMITIVE_CONE,
    PRIMITIVE_TRIANGLE_PRISM,
    PRIMITIVE_OCTAHEDRON,
    PRIMITIVE_TETRAHEDRON,
    PRIMITIVE_CHEVRON_PYRAMID,
    PRIMITIVE_BOX_FRAME,
    PRIMITIVE_TRIANGLE_OUTLINE,
    PRIMITIVE_TRIANGLE,
    PRIMITIVE_COUNT,
};

/* Full visual extents. Gameplay x/y is world x/z; elevation is center Y without a Transform3D. */
typedef NODE_COMPONENT_TYPE_DENSE(Geometry3D)
{
    int primitive;
    float width;
    float depth;
    float height;
    float elevation;
}
Geometry3D;

typedef NODE_COMPONENT_TYPE_DENSE(Aim)
{
    CF_V2 direction;
    bool firing;
    bool manual;
}
Aim;

typedef NODE_COMPONENT_TYPE_DENSE(Dash)
{
    float speed NODE_UNIT("u/s");
    float duration NODE_UNIT("s");
    float cooldown NODE_UNIT("s");
    int damage;
    float remaining;
    float cooldown_remaining;
    CF_V2 direction;
}
Dash;

enum
{
    ENCOUNTER_ACTIVE,
    ENCOUNTER_CLEARED,
    ENCOUNTER_DEFEATED,
};
enum
{
    ENCOUNTER_PLAYER,
    ENCOUNTER_ENEMY,
};

typedef NODE_COMPONENT_TYPE_DENSE(SpawnRequest)
{
    const char *prototype;
    float delay NODE_UNIT("s");
}
SpawnRequest;

/* One encounter owns the current arena. Behaviors update these counters. */
typedef NODE_COMPONENT_TYPE_DENSE(Encounter)
{
    int status;
    int total;
    int spawned;
    int alive;
}
Encounter;

typedef NODE_COMPONENT_TYPE_DENSE(EncounterMember)
{
    int role;
}
EncounterMember;

/* Display transform from scene-linear HDR. AgX is filmic and desaturates
 * highlights; Neutral keeps base colors close to their authored values. */
typedef enum
{
    TONE_MAPPING_AGX NODE_NAME("AgX"),
    TONE_MAPPING_NEUTRAL NODE_NAME("Neutral"),
} ToneMapping;

/* How finely screen-space ambient occlusion searches the 3D view. Low and
 * Medium work at half resolution; High works at full resolution with the
 * most samples. */
typedef enum
{
    OCCLUSION_OFF NODE_NAME("Off"),
    OCCLUSION_LOW NODE_NAME("Low"),
    OCCLUSION_MEDIUM NODE_NAME("Medium"),
    OCCLUSION_HIGH NODE_NAME("High"),
} Occlusion;

/* Screen light gathers the light that visible surfaces and emitters send each
 * other by radiance cascades over probes on the visible surfaces, so glowing
 * particles light the rock around them. Screen traces rays through the
 * camera's depth and the last frame's color; World traces them through
 * terrain volumes and the frame's emitting particles, so light from off the
 * screen and rock the camera cannot see count. It needs occlusion on. */
typedef enum
{
    SCREEN_LIGHT_OFF NODE_NAME("Off"),
    SCREEN_LIGHT_SCREEN NODE_NAME("Screen"),
    SCREEN_LIGHT_WORLD NODE_NAME("World"),
} ScreenLight;

/* How many probes and rays global illumination spends on the 3D scene's
 * bounced light. Off lights it with the ambient colors or sky alone. */
typedef enum
{
    GLOBAL_ILLUMINATION_OFF NODE_NAME("Off"),
    GLOBAL_ILLUMINATION_LOW NODE_NAME("Low"),
    GLOBAL_ILLUMINATION_MEDIUM NODE_NAME("Medium"),
    GLOBAL_ILLUMINATION_HIGH NODE_NAME("High"),
} GlobalIllumination;

/* Scene-wide display settings. Exposure is in stops; bloom radius is a fraction
 * of viewport height. Zero strength disables bloom. The 3D ambient light blends
 * from the ground color below to the sky color above; colors are 0xRRGGBBAA in
 * Material's linear convention and the intensity scales both. Auto exposure
 * meters the mean log luminance of the frame's lit pixels to middle gray, then
 * applies exposure as compensation; it adapts at adaptation_speed per scene
 * second, from 0.1 to 100, and zero adapts every frame. Sky is an
 * equirectangular Radiance .hdr image, such as content/skies/field.hdr, drawn
 * behind the 3D scene; it lights the scene in place of the ambient colors. Sky
 * rotation turns it and its light about the up axis in degrees,
 * counterclockwise seen from above; sky exposure brightens both in stops, from
 * -20 to 20. A sky material instead draws a surface material as the sky: its
 * Emission, plus Base Color when Unlit, seen along each direction, with
 * World Position, Object Position and Part Position the unit direction looked
 * along, Normal and View Direction pointing back, UV its equirectangular
 * coordinates and Time the scene's. It lights the scene as a sky image does,
 * from its look at time zero, and cannot read images. Occlusion darkens the
 * ambient and sky light where nearby 3D surfaces hide the sky from each other,
 * such as creases and the ground beneath objects, searching up to occlusion
 * radius world units, which must be positive while it is on; direct light is
 * left alone. Global illumination replaces the ambient and sky light on 3D
 * surfaces with the light that reaches them from the scene's opaque solids,
 * lights, emissive surfaces and sky, bounced between surfaces. */
typedef NODE_COMPONENT_TYPE_DENSE(Environment)
{
    float exposure NODE_UNIT("EV");
    float bloom_strength;
    float bloom_threshold;
    float bloom_radius;
    unsigned ambient_sky NODE_COLOR;
    unsigned ambient_ground NODE_COLOR;
    float ambient_intensity;
    ToneMapping tone_mapping;
    bool auto_exposure;
    float adaptation_speed;
    const char *sky;
    const char *sky_material;
    float sky_rotation;
    float sky_exposure NODE_UNIT("EV");
    Occlusion occlusion;
    float occlusion_radius NODE_UNIT("u");
    GlobalIllumination global_illumination;
    ScreenLight screen_light;
    /* Homogeneous scene3d medium. Zero density disables all fog work. Start
     * and distance are measured along the camera ray from its near plane.
     * Color is scattering albedo, not an ambient source. Steps and resolution
     * independently bound integration and pixel cost. */
    float fog_density NODE_UNIT("1/u") NODE_RANGE(0, 1);
    float fog_start NODE_UNIT("u") NODE_RANGE_HINT(0, 1000);
    float fog_distance NODE_UNIT("u") NODE_RANGE_HINT(1, 2000);
    unsigned fog_color NODE_COLOR;
    float fog_anisotropy NODE_RANGE(-0.9, 0.9);
    int fog_steps NODE_RANGE(4, 64);
    float fog_resolution NODE_RANGE(0.25, 1);
}
Environment;

static inline Environment environment_defaults()
{
    return (Environment){ .bloom_strength = .18f,
                          .bloom_threshold = 1,
                          .bloom_radius = .012f,
                          .ambient_sky = 0x2b3345ffu,
                          .ambient_ground = 0x0f121affu,
                          .ambient_intensity = 1,
                          .adaptation_speed = 2,
                          .occlusion = OCCLUSION_MEDIUM,
                          .occlusion_radius = 1,
                          .fog_distance = 1000,
                          .fog_color = 0xc0c8d0ffu,
                          .fog_steps = 32,
                          .fog_resolution = .5f };
}

/* Light from an infinitely distant source, such as the sun. Direction is the
 * world-space direction the light travels; it need not be normalized. Color
 * is 0xRRGGBBAA in Material's linear convention. Shadows lets opaque solids
 * block it. Angular diameter is the light's apparent size in degrees, 0 to
 * 20: its shadows soften with distance from their caster, and zero keeps
 * them hard. */
typedef NODE_COMPONENT_TYPE_DENSE(DirectionalLight)
{
    unsigned color NODE_COLOR;
    float intensity;
    CF_V3 direction;
    bool shadows;
    float angular_diameter;
}
DirectionalLight;

static inline DirectionalLight directional_light_defaults()
{
    return (DirectionalLight){ .color = 0xffffffffu,
                               .intensity = 2.8f,
                               .direction = { .45f, -.85f, -.4f },
                               .shadows = true,
                               .angular_diameter = 3 };
}

/* Local scene-linear light. Radius and center elevation use project units;
 * color is 0xRRGGBBAA, matching other authored color fields. Shadows lets
 * opaque solids within its radius block it, for up to four lights a view
 * ranks highest. */
typedef NODE_COMPONENT_TYPE_DENSE(PointLight)
{
    unsigned color NODE_COLOR;
    float intensity;
    float radius NODE_UNIT("u");
    float elevation;
    bool shadows;
    /* Radius of the emitting surface. Larger sources cast softer shadow
     * edges; zero keeps them hard. */
    float size NODE_UNIT("u");
}
PointLight;

/* A cone of local scene-linear light. Radius and center elevation use project
 * units, and color is 0xRRGGBBAA like a Point Light's. Direction is the world
 * direction the cone points; it need not be normalized. Outer angle is the
 * cone's full width in degrees, and the light fades from full strength at the
 * inner angle to none at the outer one. Shadows lets opaque solids within its
 * cone and radius block it. */
typedef NODE_COMPONENT_TYPE_DENSE(SpotLight)
{
    unsigned color NODE_COLOR;
    float intensity;
    float radius NODE_UNIT("u");
    float elevation;
    CF_V3 direction;
    float inner_angle;
    float outer_angle;
    bool shadows;
    /* Radius of the emitting surface; see Point Light. */
    float size NODE_UNIT("u");
}
SpotLight;

static inline SpotLight spot_light_defaults()
{
    return (SpotLight){ .color = 0xffffffffu,
                        .intensity = 6,
                        .radius = 400,
                        .elevation = 160,
                        .direction = { 0, -1, 0 },
                        .inner_angle = 40,
                        .outer_angle = 60,
                        .shadows = true };
}

/* A colored activity signal for material effects, independent of lighting. */
typedef NODE_COMPONENT_TYPE_DENSE(SurfaceInfluence)
{
    unsigned color NODE_COLOR;
    float strength;
    float radius NODE_UNIT("u");
    float linger;
    float inner_radius;
}
SurfaceInfluence;

/* Visual-only local rotations in radians; gameplay heading remains independent. */
typedef NODE_COMPONENT_TYPE_DENSE(VisualTransform)
{
    float pitch;
    float yaw;
    float roll;
    float scale;
}
VisualTransform;

/* Opts a 3D visual into the planar material renderer; collision remains 2D. */
typedef NODE_COMPONENT_TYPE_DENSE(MeshSurface2D)
{
    bool enabled;
}
MeshSurface2D;

/* Bounded 3D distance graph evaluated in authored units; lighting stays planar. */
typedef NODE_COMPONENT_TYPE_DENSE(SdfSurface3D)
{
    bool enabled;
    /* Optional simpler Shape graph below the projected pixel span. Both
     * shapes must fit the same Geometry3D bounds and parameter bindings. */
    const char *low_detail_shape;
    float low_detail_pixels;
}
SdfSurface3D;

typedef enum
{
    SURFACE_SUPERSAMPLED,
    SURFACE_FULL,
} SurfaceResolution;

typedef enum
{
    SDF_NORMAL_CENTRAL,
    SDF_NORMAL_TETRAHEDRAL,
    SDF_NORMAL_ANALYTIC,
} SdfNormalMode;

/* Optional scene surface quality. Zero initialization preserves the reference
 * renderer. Place one on the camera or another scene settings entity. */
typedef NODE_COMPONENT_TYPE_DENSE(SurfaceQuality)
{
    /* Supersampled uses twice each canvas dimension; Full uses one sample. */
    SurfaceResolution resolution;
    /* Projected pixel tolerance for refined SDF tracing; zero uses the
     * original distance-bound tolerance. Supported range is zero to one. */
    float sdf_pixel_error NODE_RANGE(0, 1);
    /* Central uses six field samples; Tetrahedral uses four. */
    SdfNormalMode sdf_normal_mode;
    /* Reference comparison retaining each object's full-detail graph. */
    bool disable_sdf_lod;
}
SurfaceQuality;

/* Optional imported triangle geometry; Surface graphs read its exported UVs. */
typedef NODE_COMPONENT_TYPE_DENSE(MeshAsset)
{
    const char *source;
}
MeshAsset;

typedef NODE_COMPONENT_TYPE_DENSE(MeshParts)
{
    const char *parts;
}
MeshParts;

/* Independent random endpoints; exponent zero holds the sampled start value. */
typedef NODE_TYPE(ParticleCurve)
{
    CF_V2 start;
    CF_V2 end;
    float exponent;
}
ParticleCurve;

/* A parent is optional. Each child emitter owns an independent seed and rate. */
typedef enum
{
    PARTICLE_ROUND,
    PARTICLE_TRIANGLE,
    PARTICLE_TRIANGLE_OUTLINE,
    PARTICLE_RING,
    PARTICLE_STREAK,
} ParticleShape;
#define PARTICLE_SHAPE_COUNT (PARTICLE_STREAK + 1)

typedef NODE_COMPONENT_TYPE_DENSE(ParticleEmitter)
{
    ParticleShape shape;
    unsigned color NODE_COLOR;
    unsigned end_color NODE_COLOR;
    float rate;
    int burst_count;
    CF_V2 lifetime;
    CF_V2 speed;
    ParticleCurve size;
    float spread;
    ParticleCurve drag;
    ParticleCurve emission;
    ParticleCurve antialias;
    float absorption NODE_RANGE_HINT(0, 1);
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
    FluidKind fluid_kind;
    float fluid_mass;
}
ParticleEmitter;

static inline ParticleEmitter particle_emitter_defaults()
{
    return (ParticleEmitter){ .color = 0xffffffffu,
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

/* Instanced 3D particles evaluated by Shader.material's pure shader function.
 * The function owns position, size and HDR color. Geometry3D supplies one
 * tetrahedron or octahedron; no particle owns an entity or simulation state. */
typedef NODE_COMPONENT_TYPE_DENSE(ParticleField3D)
{
    int count NODE_RANGE(0, 1048576);
    int seed;
    /* An authored clock lets a field pause independently of scene time. */
    float time NODE_UNIT("s");
    /* Additive fields test solid depth without writing depth or sorting. */
    bool additive;
}
ParticleField3D;

typedef NODE_COMPONENT_TYPE_DENSE(Trail)
{
    unsigned color NODE_COLOR;
    float width;
    float lifetime NODE_UNIT("s");
    float emission;
}
Trail;

typedef NODE_COMPONENT_TYPE_DENSE(Experience)
{
    int current;
}
Experience;

typedef NODE_COMPONENT_TYPE_DENSE(Pickup)
{
    int value;
    float lifetime NODE_UNIT("s");
    float age;
    int state;
    float collection_age;
    uint64_t target NODE_HANDLE(ecs.entity);
    CF_V2 origin;
}
Pickup;

/* An optional camera follows its entity's Transform, or its Transform3D. The
 * authored span is expanded for the viewport aspect; offsets and spans use
 * project units. */
typedef NODE_COMPONENT_TYPE_DENSE(Camera)
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
    float yaw NODE_UNIT("rad");
    float pitch NODE_UNIT("rad");
    /* Turns the image right/down axes around the view direction. */
    float roll NODE_UNIT("rad");
    float target_elevation;
    /* 3D scenes. A camera with a Transform3D looks along its local -Z with +Y
     * up, from its position moved by the offset (gameplay y is world z); one
     * without frames its span from a fixed overhead angle. Field of view is
     * vertical, in radians, from 0.00001 degrees (as in Unity) to below pi,
     * and zero picks 35 degrees. Set both clip distances or neither, with
     * far at most a million times near; zero clips scale with the span's
     * height. */
    float fov NODE_UNIT("rad");
    /* Full-3D orthographic projection using width/height rather than fov.
     * False preserves the perspective lens. Transform3D still places the eye. */
    bool orthographic;
    float near_clip NODE_UNIT("u");
    float far_clip NODE_UNIT("u");
}
Camera;

/* Sources are grouped by device: mouse, keyboard, then controller. Escape is
 * not a source: hosts reserve it to pause play. */
typedef enum
{
    CONTROL_NONE,
    CONTROL_MOUSE_LEFT,
    CONTROL_MOUSE_RIGHT,
    CONTROL_MOUSE_MIDDLE,
    CONTROL_SPACE,
    CONTROL_SHIFT,
    CONTROL_CONTROL,
    CONTROL_ALT,
    CONTROL_TAB,
    CONTROL_ENTER,
    CONTROL_BACKSPACE,
    CONTROL_ARROW_UP,
    CONTROL_ARROW_DOWN,
    CONTROL_ARROW_LEFT,
    CONTROL_ARROW_RIGHT,
    CONTROL_A,
    CONTROL_B,
    CONTROL_C,
    CONTROL_D,
    CONTROL_E,
    CONTROL_F,
    CONTROL_G,
    CONTROL_H,
    CONTROL_I,
    CONTROL_J,
    CONTROL_K,
    CONTROL_L,
    CONTROL_M,
    CONTROL_N,
    CONTROL_O,
    CONTROL_P,
    CONTROL_Q,
    CONTROL_R,
    CONTROL_S,
    CONTROL_T,
    CONTROL_U,
    CONTROL_V,
    CONTROL_W,
    CONTROL_X,
    CONTROL_Y,
    CONTROL_Z,
    CONTROL_0,
    CONTROL_1,
    CONTROL_2,
    CONTROL_3,
    CONTROL_4,
    CONTROL_5,
    CONTROL_6,
    CONTROL_7,
    CONTROL_8,
    CONTROL_9,
    CONTROL_PAD_A,
    CONTROL_PAD_B,
    CONTROL_PAD_X,
    CONTROL_PAD_Y,
    CONTROL_PAD_LEFT_SHOULDER,
    CONTROL_PAD_RIGHT_SHOULDER,
    CONTROL_PAD_LEFT_TRIGGER,
    CONTROL_PAD_RIGHT_TRIGGER,
    CONTROL_AIM_STICK,
    CONTROL_COUNT,
} Control;

/* Per-controller action sources. Independent of the shared keyboard bitset. */
typedef enum
{
    PAD_NONE,
    PAD_A,
    PAD_B,
    PAD_X,
    PAD_Y,
    PAD_BACK,
    PAD_START,
    PAD_LEFT_SHOULDER,
    PAD_RIGHT_SHOULDER,
    PAD_DPAD_UP,
    PAD_DPAD_DOWN,
    PAD_DPAD_LEFT,
    PAD_DPAD_RIGHT,
    PAD_LEFT_STICK_UP,
    PAD_LEFT_STICK_DOWN,
    PAD_LEFT_STICK_LEFT,
    PAD_LEFT_STICK_RIGHT,
    PAD_LEFT_TRIGGER,
    PAD_RIGHT_TRIGGER,
    PAD_COUNT,
} PadControl;

/* Shows the ui asset content/ui/<document>.rml over the game view, above
 * documents with a lower order. An element's emit('Event') binding, as in
 * data-event-click="emit('Resume')", sends that event to this entity; an
 * emitting input's value is read with Get UI Value and `#id`. UI nodes set
 * text, classes and values on the elements an RCSS selector matches. */
typedef NODE_COMPONENT_TYPE_DENSE(Ui)
{
    const char *document;
    int order;
    bool hidden;
}
Ui;

/* One optional finite visibility grid. The main view conceals unseen space. */
typedef NODE_COMPONENT_TYPE_DENSE(Fog)
{
    CF_V2 origin;
    int columns;
    int rows;
    float cell_size;
    float drift; /* Zero disables ambient fog motion. */
    unsigned color NODE_COLOR;
    bool disabled;
}
Fog;

/* Vision sources share current sight. Obstacles in occluders block sight.
 * A positive duration shrinks a temporary clearing to zero over its lifetime;
 * duration <= 0 is permanent. Only remembering sources update terrain memory. */
typedef NODE_COMPONENT_TYPE_DENSE(Vision)
{
    float radius NODE_UNIT("u");
    unsigned occluders;
    float duration NODE_UNIT("s");
    float age;
    bool remember;
    bool disabled;
}
Vision;

/* A sensor observation owns its last detected position, independently of the
 * observed actor. Behaviors decide what it detects and when it refreshes. */
typedef NODE_COMPONENT_TYPE_DENSE(SensorContact)
{
    CF_V2 position;
    float remaining;
    unsigned color NODE_COLOR;
}
SensorContact;

typedef NODE_COMPONENT_TYPE_DENSE(RenderState)
{
    bool hidden;
    bool visibility_culled;
}
RenderState;

/* Optional 2D radiance composition; values are nonnegative scene-linear gains. */
/* Cell size, radius, density and brightness for each procedural star layer. */
typedef NODE_COMPONENT_TYPE_DENSE(Starfield2D)
{
    CF_V4 far_layer, middle_layer, near_layer;
    CF_V3 parallax;
    unsigned color_a NODE_COLOR;
    unsigned color_b NODE_COLOR;
    unsigned color_c NODE_COLOR;
    unsigned color_d NODE_COLOR;
}
Starfield2D;

typedef enum
{
    HRC_TONE_CLAMP,
    HRC_TONE_AGX NODE_NAME("AgX"),
} HRCToneMapping;

typedef enum
{
    HRC_SHADOW_REFERENCE,
    HRC_SHADOW_HORIZON4,
    HRC_SHADOW_HORIZON8,
} HRCShadowMode;

typedef enum
{
    HRC_SURFACE_BLUR,
    HRC_SURFACE_GATHER,
    HRC_SURFACE_DEPTH NODE_NAME("Depth thickness"),
} HRCSurfaceMode;

/* Maximum source tiles per axis before bounded child refinement. */
typedef enum
{
    HRC_DEPTH_LOW NODE_NAME("Low"),
    HRC_DEPTH_BALANCED NODE_NAME("Balanced"),
    HRC_DEPTH_HIGH NODE_NAME("High"),
} HRCDepthQuality;

typedef enum
{
    HRC_DEPTH_VISIBILITY_MASK NODE_NAME("Angular masks"),
    HRC_DEPTH_VISIBILITY_SEGMENTS NODE_NAME("Bounded segments"),
} HRCDepthVisibility;

/* 2D lighting by holographic radiance cascades: light from emissive
 * materials, with shadows, bounce and fog. The first entity's settings light
 * the scene; a scene without one draws its materials unlit. HRC's
 * experiments and internal tuning are not authored here; tests set them. */
typedef NODE_COMPONENT_TYPE_DENSE(HRC2D)
{
    /* Finest light grid as a fraction of the padded canvas; 0.5 solves one
     * probe per 2x2 pixels. Brightness keeps this detail, and most lighting
     * cost scales with its area. 1, 0.5, 0.25 and 0.125 give whole-pixel
     * cells. With adaptive on, the highest resolution it uses. */
    float resolution NODE_RANGE(0.1, 1) NODE_RANGE_DEFAULT(.5);
    /* Steps lighting down while the scene's GPU time misses target_fps, first
     * color detail, then resolution by halves down to 0.25, and back up when
     * there is room. Where the GPU reports its time (Metal, and
     * WebGL with timer queries), frames limited by the CPU or vsync keep their
     * lighting; elsewhere it judges the whole frame's interval. */
    bool adaptive;
    /* Frame rate adaptive keeps the scene's GPU work within. */
    float target_fps NODE_UNIT("fps");
    /* Gain of the unlit scene color added to the frame. 1 shows materials as
     * drawn, and lower values darken what light does not reach. In Depth
     * thickness, taller surfaces nearby hide part of it, as they would hide
     * the sky. */
    float ambient;
    /* Light from the sky above a side view: open ground receives this color.
     * Solids that absorb light block it like any light, so overhangs shade
     * the ground beneath them and caves stay dark; falloff never dims it.
     * In Depth thickness, heights nearby hide part of it, as for ambient.
     * Black, the default, has no sky. */
    unsigned sky NODE_COLOR;
    /* Brightness of emissive surfaces as drawn; the light they cast is
     * unchanged. */
    float intensity;
    /* Brightness of light seen in open air: the glow around lights. */
    float fog;
    /* Extinction/scattering coefficient for direct Spot Light beams, in
     * inverse world units. Zero disables volume lighting. Eight view-depth
     * samples integrate a homogeneous medium against visible depth slabs;
     * this does not add hidden geometry or feed the HRC bounce solver. */
    float volume_density NODE_RANGE_HINT(0, 1) NODE_UNIT("u^-1");
    /* Fraction of a solid's albedo that stays visible where no light reaches
     * it. Depth thickness hides part of it like ambient. */
    float shadow_floor;
    /* How quickly light dims with distance; zero keeps it at full strength
     * across the view. */
    float falloff;
    /* World-space raster guard around the camera for nearby light transport.
     * The default, zero, leaves offscreen sources and occluders out. */
    float lighting_margin NODE_UNIT("u");
    /* Gain of light that lit surfaces reflect back into the transport; needs
     * feedback. */
    float bounce_intensity;
    /* Current-frame weight of bounce light: 1 disables temporal smoothing, not
     * bounce, and lower values retain more history. Camera movement resets it. */
    float bounce_hysteresis NODE_RANGE(0, 1) NODE_RANGE_DEFAULT(.8);
    /* Current-frame weight of received light: 1 disables temporal smoothing,
     * and lower values retain more history. */
    float radiance_hysteresis NODE_RANGE(0, 1) NODE_RANGE_DEFAULT(.8);
    /* Feeds light reflected by lit surfaces back into the transport as bounce
     * light. */
    bool feedback;
    /* World units that light received near a solid's surface reaches into it,
     * scaled by Material.edge_light. Zero disables surface lighting. */
    float surface_radius NODE_UNIT("u");
    /* Blur spreads open-space light; Gather uses bounded visibility probes.
     * Depth thickness is the 2.5D mode: it gathers visible emission past
     * anything with height, and on solids that absorb nothing shades the HRC
     * light by their facing and those heights, counting each light as much
     * as HRC brings it. Solids that absorb light, where HRC's light stops, take
     * the light HRC brings open ground from the lights they see above them,
     * so a side view's tiles catch the light of lanterns in front of them.
     * Light passes over a prop that absorbs nothing where the prop is lower
     * than the light. Heights within the depth radius also hide ambient
     * light, darkening the foot of a wall and the floor around a prop. */
    HRCSurfaceMode surface_mode;
    /* Screen-plane reach in project units for Depth thickness mode. Solids
     * that absorb light take light only from the lights within reach, and a
     * solid that absorbs nothing shades the light HRC brings it from them,
     * and all its HRC light where they bring at least half of it, so it
     * should reach the lights that matter. */
    float surface_depth_radius;
    /* Gain of the Depth light solids that absorb light take, which at 1
     * matches HRC's light on open ground, and of Depth highlights. Zero
     * disables Depth thickness. */
    float surface_depth_gain;
    /* Source sampling budget; all levels preserve integrated emission. */
    HRCDepthQuality surface_depth_quality;
    /* Segments share one bounded visibility query per coarse source tile. */
    HRCDepthVisibility surface_depth_visibility;
    /* Blend visibility bins and reconstruction boundaries, in [0, 1]. */
    float surface_depth_shadow_softness NODE_RANGE(0, 1);
    /* Reproject and validate a short history of the Depth contribution. */
    bool surface_depth_temporal;
    /* Maximum previous-frame weight; zero selects the default of 0.65. */
    float surface_depth_history;
    /* How far below its visible top a surface blocks Depth light and ambient
     * light, in project units. A light that absorbs nothing never blocks. */
    float surface_thickness;
    /* Sideways reach of nearby light relative to its height above the plane;
     * larger values deepen normal-mapped relief. */
    float surface_relief;
    /* Local heightfield visibility; horizons store four or eight azimuths. */
    HRCShadowMode surface_shadow_mode;
    /* Outward tilt of solid normals near silhouettes, which catches rim light. */
    float surface_bevel;
    /* Gain of glossy highlights on solids with low material roughness. */
    float specular;
    /* 0 clamps the composite to the display range. 1 keeps it in HDR and
     * presents it with bloom, exposure and AgX tone mapping. */
    HRCToneMapping tone_mapping;
    /* Exposure in EV before tone mapping. */
    float exposure NODE_UNIT("EV");
    /* Fraction of the blurred image mixed into the frame, in [0, 1]. */
    float bloom NODE_RANGE(0, 1);
}
HRC2D;

static inline HRC2D hrc2d_defaults()
{
    return (HRC2D){ .resolution = .5f,
                    .adaptive = true,
                    .target_fps = 60,
                    .ambient = 1,
                    .intensity = 1,
                    .fog = 1,
                    .falloff = 1,
                    .bounce_intensity = 1,
                    .bounce_hysteresis = .8f,
                    .radiance_hysteresis = .8f,
                    .feedback = true,
                    .surface_radius = 24,
                    .surface_depth_radius = 128,
                    .surface_depth_gain = 1,
                    .surface_depth_quality = HRC_DEPTH_BALANCED,
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

/* Transform hierarchy. Transform with Orientation, and Transform3D, place an
 * entity relative to its ChildOf parent, or to the world for a root. Transform
 * propagation computes the global transforms from them, parents first, after
 * physics, after presentation and before each draw. Renderers and cameras
 * read the global transforms, and scripts read them and write the local ones.
 * Physics, vision and aim compute the same world placement on demand. */

/* Local 3D placement in project units. rotation is a unit quaternion; a zero
 * one reads as identity. KDL loads omitted fields as zero, so author scale. */
typedef NODE_COMPONENT_TYPE_DENSE(Transform3D)
{
    CF_V3 position;
    CF_Quat rotation;
    CF_V3 scale;
}
Transform3D;

/* World placement of a Transform3D: the columns of an affine matrix. */
typedef NODE_DERIVED_COMPONENT(GlobalTransform3D, Transform3D)
{
    CF_V3 x_axis;
    CF_V3 y_axis;
    CF_V3 z_axis;
    CF_V3 position;
}
GlobalTransform3D;

/* World placement of a Transform and its Orientation. */
typedef NODE_DERIVED_COMPONENT(GlobalTransform2D, Transform)
{
    CF_V2 position;
    float radians;
}
GlobalTransform2D;

/* 3D physics. Entities with Transform3D and Collider3D collide in one Box3D
 * world per ECS world, created when the first one appears. Lengths are project
 * units and masses kilograms. A Body3D makes the collider kinematic or dynamic;
 * without one it is static. A dynamic body must be placed in the world, and
 * physics writes its position, rotation and velocities back. Any other body
 * under a ChildOf parent with a Transform3D follows it and ignores its own
 * velocities. When the top of its hierarchy has a body, its collider becomes
 * part of that body: it moves with it through each step, adds its mass (a
 * sensor adds none) and passes its hits on to it. Otherwise it teleports with
 * its parents. */

typedef enum
{
    COLLIDER3D_BOX,
    COLLIDER3D_SPHERE,
    COLLIDER3D_CAPSULE,
    COLLIDER3D_HULL,
    COLLIDER3D_MESH
} Collider3DShape;

/* A shape centered on the entity and scaled by its world scale. A box uses
 * size, its full extents; a sphere uses radius; a capsule uses radius and
 * height, its full length along local Y, at least twice the radius. A hull
 * wraps the vertices of mesh, a prepared content .mesh, in at most 64 of
 * them; a mesh is that mesh's triangles, on a static collider placed in the
 * world, and touches only other bodies' convex shapes. Two colliders touch
 * when each one's category shares a bit with the other's mask, so zero masks
 * touch nothing. Sensors report contacts without pushing. Friction and
 * restitution of zero are frictionless and inelastic. */
typedef NODE_COMPONENT_TYPE_DENSE(Collider3D)
{
    Collider3DShape shape;
    CF_V3 size;
    float radius NODE_UNIT("u");
    float height NODE_UNIT("u");
    const char *mesh;
    unsigned category;
    unsigned mask;
    bool sensor;
    float friction NODE_RANGE_HINT(0, 1);
    float restitution NODE_RANGE_HINT(0, 1);
}
Collider3D;

/* World-space velocities: linear in units/second, angular in radians/second.
 * Kinematic bodies move at them. Dynamic bodies start from them, and physics
 * writes the solved values back each step, with the body's mass: 1000 kg per
 * cubic meter of its collider and the solid colliders it carries. */
typedef NODE_COMPONENT_TYPE_DENSE(Body3D)
{
    BodyType type;
    CF_V3 linear_velocity;
    CF_V3 angular_velocity;
    float mass NODE_UNIT("kg");
}
Body3D;

/* The scene's 3D gravity in units/second^2, on at most one entity. Without
 * one, gravity is 9.81 meters/second^2 along -Y. */
typedef NODE_COMPONENT_TYPE_DENSE(Gravity3D)
{
    CF_V3 acceleration;
}
Gravity3D;

/* Makes a kinematic body with a solid capsule collider, at least 2 cm in
 * radius, a character that slides. Each step, physics moves it by its Body3D
 * linear velocity until it meets a solid collider its collider collides with,
 * slides along what it meets, and drops the part of the velocity that pushes
 * into it, so gravity a script adds does not build up while it stands. on_floor
 * says whether it ended the step on a surface within 45 degrees of up, against
 * gravity, and floor_normal is that surface's normal. Physics writes both. It
 * rides the translation and rotation of its supporting collider's body, with
 * carry swept against other solids. Carry does not add to linear_velocity;
 * moving away from the floor releases it without inherited momentum. It stops
 * at dynamic bodies and other characters as at walls, and carries only sensor
 * colliders under it. It turns by its Transform3D; platform rotation moves its
 * position without turning it, and physics keeps angular velocity at zero. */
typedef NODE_COMPONENT_TYPE_DENSE(Character3D)
{
    bool on_floor;
    CF_V3 floor_normal;
}
Character3D;

typedef enum
{
    JOINT3D_HINGE,
    JOINT3D_SLIDER,
    JOINT3D_BALL,
    JOINT3D_WELD,
    JOINT3D_DISTANCE
} Joint3DKind;

/* Joins the body at or above the entity, the nearest one with a Body3D or a
 * static collider of its own, to other's (zero: the world), at the entity's
 * position. A hinge turns about the entity's local Y axis, a slider moves
 * along it, a ball turns freely and a weld holds. Each holds the bodies as
 * they were when it was made, and is made again when its fields change or
 * its entity moves on its body. A distance joint keeps the entity at its first
 * distance from other's origin. limit keeps a hinge's angle, a slider's offset
 * or a distance between lower and upper; a ball's angle from its axis stays
 * within upper. motor drives a hinge, slider or distance at speed with at most
 * strength. spring pulls a hinge, slider, ball or weld back to where it was
 * made at hertz and damping, and makes a distance joint elastic: its limit
 * and motor act only then. collide lets the two bodies touch. */
typedef NODE_COMPONENT_TYPE_DENSE(Joint3D)
{
    Joint3DKind kind;
    uint64_t other NODE_HANDLE(ecs.entity);
    bool collide;
    bool limit;
    float lower;
    float upper;
    bool motor;
    float speed;
    float strength;
    bool spring;
    float hertz NODE_UNIT("Hz");
    float damping;
}
Joint3D;

#endif
