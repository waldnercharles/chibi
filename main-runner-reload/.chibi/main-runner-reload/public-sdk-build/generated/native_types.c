/* generated native declarations and execution capabilities */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "runtime/components.h"
#include "types/type.h"
#include "types/text.h"
#include "types/catalog.h"
#include "execution/service.h"
#include "ecs/ecs.h"

const type_field Transform_fields[] = {
    { "x", PIN_F32, offsetof(Transform, x), sizeof(float), "float", "The x component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(Transform, y), sizeof(float), "float", "The y component.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Transform = {
    .name = "Transform",
    .size = sizeof(Transform),
    .fields = Transform_fields,
    .field_count = 2,
    .alias_of = "vec2",
};

const type_field Velocity_fields[] = {
    { "x", PIN_F32, offsetof(Velocity, x), sizeof(float), "float", "The x component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(Velocity, y), sizeof(float), "float", "The y component.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Velocity = {
    .name = "Velocity",
    .size = sizeof(Velocity),
    .fields = Velocity_fields,
    .field_count = 2,
    .alias_of = "vec2",
};

const type_field GlobalBinding_fields[] = {
    { "name", PIN_STR, offsetof(GlobalBinding, name), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_GlobalBinding = {
    .name = "GlobalBinding",
    .size = sizeof(GlobalBinding),
    .fields = GlobalBinding_fields,
    .field_count = 1,
};

const type_field Body_fields[] = {
    { "type", PIN_ENUM, offsetof(Body, type), sizeof(BodyType), "BodyType", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "continuous", PIN_BOOL, offsetof(Body, continuous), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "angular_velocity", PIN_F32, offsetof(Body, angular_velocity), sizeof(float), "float", "Kinematic radians/second; requires Orientation.", false, false, "rad/s", 0, 0, false, 0, NULL },
    { "allow_rotation", PIN_BOOL, offsetof(Body, allow_rotation), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mass", PIN_F32, offsetof(Body, mass), sizeof(float), "float", "A dynamic body's mass, 1 kg per square meter of its collider and the solid colliders it carries; physics writes it after each step.", false, false, "kg", 0, 0, false, 0, NULL },
};

const type_desc T_Body = {
    .name = "Body",
    .size = sizeof(Body),
    .fields = Body_fields,
    .field_count = 5,
};

const type_field Gravity_fields[] = {
    { "acceleration", PIN_STRUCT, offsetof(Gravity, acceleration), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Gravity = {
    .name = "Gravity",
    .size = sizeof(Gravity),
    .fields = Gravity_fields,
    .field_count = 1,
};

const type_field Collider_fields[] = {
    { "radius", PIN_F32, offsetof(Collider, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "category", PIN_U32, offsetof(Collider, category), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mask", PIN_U32, offsetof(Collider, mask), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shape", PIN_ENUM, offsetof(Collider, shape), sizeof(ColliderShape), "ColliderShape", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "width", PIN_F32, offsetof(Collider, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(Collider, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sensor", PIN_BOOL, offsetof(Collider, sensor), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "pierce_fragments", PIN_BOOL, offsetof(Collider, pierce_fragments), sizeof(_Bool), "_Bool", "Continuous sensors report debris hits without stopping.", false, false, NULL, 0, 0, false, 0, NULL },
    { "ignore_entity", PIN_HANDLE, offsetof(Collider, ignore_entity), sizeof(uint64_t), "ecs.entity", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "restitution", PIN_F32, offsetof(Collider, restitution), sizeof(float), "float", NULL, false, false, NULL, 0, 1, true, 0, NULL },
    { "friction", PIN_F32, offsetof(Collider, friction), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
};

const type_desc T_Collider = {
    .name = "Collider",
    .size = sizeof(Collider),
    .fields = Collider_fields,
    .field_count = 11,
};

const type_field Drive_fields[] = {
    { "target", PIN_STRUCT, offsetof(Drive, target), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "acceleration", PIN_F32, offsetof(Drive, acceleration), sizeof(float), "float", NULL, false, false, "u/s\302\262", 0, 0, false, 0, NULL },
};

const type_desc T_Drive = {
    .name = "Drive",
    .size = sizeof(Drive),
    .fields = Drive_fields,
    .field_count = 2,
};

const type_field Joint_fields[] = {
    { "kind", PIN_ENUM, offsetof(Joint, kind), sizeof(JointKind), "JointKind", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "other", PIN_HANDLE, offsetof(Joint, other), sizeof(uint64_t), "ecs.entity", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "collide", PIN_BOOL, offsetof(Joint, collide), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "limit", PIN_BOOL, offsetof(Joint, limit), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "lower", PIN_F32, offsetof(Joint, lower), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "upper", PIN_F32, offsetof(Joint, upper), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "motor", PIN_BOOL, offsetof(Joint, motor), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "speed", PIN_F32, offsetof(Joint, speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "strength", PIN_F32, offsetof(Joint, strength), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "spring", PIN_BOOL, offsetof(Joint, spring), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "hertz", PIN_F32, offsetof(Joint, hertz), sizeof(float), "float", NULL, false, false, "Hz", 0, 0, false, 0, NULL },
    { "damping", PIN_F32, offsetof(Joint, damping), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Joint = {
    .name = "Joint",
    .size = sizeof(Joint),
    .fields = Joint_fields,
    .field_count = 12,
};

const type_field EngineSystem_fields[] = {
    { "asset", PIN_STR, offsetof(EngineSystem, asset), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "phase", PIN_I32, offsetof(EngineSystem, phase), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "order", PIN_I32, offsetof(EngineSystem, order), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_EngineSystem = {
    .name = "EngineSystem",
    .size = sizeof(EngineSystem),
    .fields = EngineSystem_fields,
    .field_count = 3,
};

const type_field MaterialFluidWorld_fields[] = {
    { "chunk_size", PIN_F32, offsetof(MaterialFluidWorld, chunk_size), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "buffer", PIN_F32, offsetof(MaterialFluidWorld, buffer), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "kernel", PIN_F32, offsetof(MaterialFluidWorld, kernel), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "particle_limit", PIN_I32, offsetof(MaterialFluidWorld, particle_limit), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "solver", PIN_ENUM, offsetof(MaterialFluidWorld, solver), sizeof(MaterialSolver), "MaterialSolver", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mpm_cell_size", PIN_F32, offsetof(MaterialFluidWorld, mpm_cell_size), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mpm_max_substeps", PIN_I32, offsetof(MaterialFluidWorld, mpm_max_substeps), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "debug_view", PIN_ENUM, offsetof(MaterialFluidWorld, debug_view), sizeof(MaterialDebugView), "MaterialDebugView", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_cell_size", PIN_F32, offsetof(MaterialFluidWorld, gas_cell_size), sizeof(float), "float", "Optional gas flow; a zero cell size leaves particles independent.", false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_vorticity", PIN_F32, offsetof(MaterialFluidWorld, gas_vorticity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_drag", PIN_F32, offsetof(MaterialFluidWorld, gas_drag), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_buoyancy", PIN_F32, offsetof(MaterialFluidWorld, gas_buoyancy), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_ambient_heat", PIN_F32, offsetof(MaterialFluidWorld, gas_ambient_heat), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_wake_speed", PIN_F32, offsetof(MaterialFluidWorld, gas_wake_speed), sizeof(float), "float", "Minimum normal closing speed, world units/sec.", false, false, "u/s", 0, 0, false, 0, NULL },
    { "gas_wake_strength", PIN_F32, offsetof(MaterialFluidWorld, gas_wake_strength), sizeof(float), "float", "Scales puff speed and response intensity.", false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_wake_count", PIN_I32, offsetof(MaterialFluidWorld, gas_wake_count), sizeof(int), "int", "Maximum visual particles per body/patch/tick.", false, false, NULL, 0, 0, false, 0, NULL },
    { "gas_wake_lifetime", PIN_F32, offsetof(MaterialFluidWorld, gas_wake_lifetime), sizeof(float), "float", "Maximum visual particle lifetime, seconds.", false, false, "s", 0, 0, false, 0, NULL },
    { "liquid_splash_speed", PIN_F32, offsetof(MaterialFluidWorld, liquid_splash_speed), sizeof(float), "float", "Minimum normal closing speed, world units/sec.", false, false, "u/s", 0, 0, false, 0, NULL },
    { "liquid_splash_strength", PIN_F32, offsetof(MaterialFluidWorld, liquid_splash_strength), sizeof(float), "float", "Scales droplet speed and response intensity.", false, false, NULL, 0, 0, false, 0, NULL },
    { "liquid_splash_count", PIN_I32, offsetof(MaterialFluidWorld, liquid_splash_count), sizeof(int), "int", "Maximum visual particles per body/patch/tick.", false, false, NULL, 0, 0, false, 0, NULL },
    { "liquid_splash_lifetime", PIN_F32, offsetof(MaterialFluidWorld, liquid_splash_lifetime), sizeof(float), "float", "Maximum visual particle lifetime, seconds.", false, false, "s", 0, 0, false, 0, NULL },
};

const type_desc T_MaterialFluidWorld = {
    .name = "MaterialFluidWorld",
    .size = sizeof(MaterialFluidWorld),
    .fields = MaterialFluidWorld_fields,
    .field_count = 21,
};

const type_field MaterialDestructionWorld_fields[] = {
    { "fragment_limit", PIN_I32, offsetof(MaterialDestructionWorld, fragment_limit), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "debris_limit", PIN_I32, offsetof(MaterialDestructionWorld, debris_limit), sizeof(int), "int", "Small rock chunks thrown from craters; zero disables them.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MaterialDestructionWorld = {
    .name = "MaterialDestructionWorld",
    .size = sizeof(MaterialDestructionWorld),
    .fields = MaterialDestructionWorld_fields,
    .field_count = 2,
};

const type_field MaterialBreakProfile_fields[] = {
    { "chip_speed", PIN_F32, offsetof(MaterialBreakProfile, chip_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fragment_speed", PIN_F32, offsetof(MaterialBreakProfile, fragment_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "blast_speed", PIN_F32, offsetof(MaterialBreakProfile, blast_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "release_speed", PIN_F32, offsetof(MaterialBreakProfile, release_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_limit", PIN_I32, offsetof(MaterialBreakProfile, dust_limit), sizeof(int), "int", "Grit sprays from each crater around its outward surface normal: at most dust_limit grains, one per crumbled cell.", false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_speed_min", PIN_F32, offsetof(MaterialBreakProfile, dust_speed_min), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_speed_max", PIN_F32, offsetof(MaterialBreakProfile, dust_speed_max), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_lifetime_min", PIN_F32, offsetof(MaterialBreakProfile, dust_lifetime_min), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_lifetime_max", PIN_F32, offsetof(MaterialBreakProfile, dust_lifetime_max), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_size_min", PIN_F32, offsetof(MaterialBreakProfile, dust_size_min), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_size_max", PIN_F32, offsetof(MaterialBreakProfile, dust_size_max), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "dust_color", PIN_U32, offsetof(MaterialBreakProfile, dust_color), sizeof(unsigned int), "unsigned int", "Grit color; zero uses the source appearance color.", true, false, NULL, 0, 0, false, 0, NULL },
    { "spark_density", PIN_F32, offsetof(MaterialBreakProfile, spark_density), sizeof(float), "float", "Hot strikes add spark_density sparks per crumbled cell per unit of the striker's heat. Sparks start in the striker's light color, cool through spark_color and fade.", false, false, NULL, 0, 0, false, 0, NULL },
    { "spark_color", PIN_U32, offsetof(MaterialBreakProfile, spark_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "spark_emission", PIN_F32, offsetof(MaterialBreakProfile, spark_emission), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_limit", PIN_I32, offsetof(MaterialBreakProfile, chunk_limit), sizeof(int), "int", "Rock chunks thrown per crater: one per chunk_cells crumbled cells, at most chunk_limit. Sizes are world-unit radii.", false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_cells", PIN_F32, offsetof(MaterialBreakProfile, chunk_cells), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_size_min", PIN_F32, offsetof(MaterialBreakProfile, chunk_size_min), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_size_max", PIN_F32, offsetof(MaterialBreakProfile, chunk_size_max), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_speed_min", PIN_F32, offsetof(MaterialBreakProfile, chunk_speed_min), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_speed_max", PIN_F32, offsetof(MaterialBreakProfile, chunk_speed_max), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "chunk_lifetime", PIN_F32, offsetof(MaterialBreakProfile, chunk_lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "smoke_count", PIN_I32, offsetof(MaterialBreakProfile, smoke_count), sizeof(int), "int", "Gas parcels released into the fluid world per crater; zero disables.", false, false, NULL, 0, 0, false, 0, NULL },
    { "smoke_color", PIN_U32, offsetof(MaterialBreakProfile, smoke_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "smoke_lifetime", PIN_F32, offsetof(MaterialBreakProfile, smoke_lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "smoke_speed", PIN_F32, offsetof(MaterialBreakProfile, smoke_speed), sizeof(float), "float", "Outward drift speed of the released gas in world units per second.", false, false, "u/s", 0, 0, false, 0, NULL },
};

const type_desc T_MaterialBreakProfile = {
    .name = "MaterialBreakProfile",
    .size = sizeof(MaterialBreakProfile),
    .fields = MaterialBreakProfile_fields,
    .field_count = 26,
};

const type_field Combustion_fields[] = {
    { "ignition", PIN_F32, offsetof(Combustion, ignition), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cooling", PIN_F32, offsetof(Combustion, cooling), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "burn_rate", PIN_F32, offsetof(Combustion, burn_rate), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "heat_release", PIN_F32, offsetof(Combustion, heat_release), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "smoke_lifetime", PIN_F32, offsetof(Combustion, smoke_lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "smoke_color", PIN_U32, offsetof(Combustion, smoke_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "smoke_absorption", PIN_F32, offsetof(Combustion, smoke_absorption), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "flame_color", PIN_U32, offsetof(Combustion, flame_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "flame_emission", PIN_F32, offsetof(Combustion, flame_emission), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "blast_radius", PIN_F32, offsetof(Combustion, blast_radius), sizeof(float), "float", "World units a burst of ignitions pushes gas, bodies and debris; at most 1024.", false, false, "u", 0, 1024, true, 0, NULL },
    { "blast_speed", PIN_F32, offsetof(Combustion, blast_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "blast_damage", PIN_F32, offsetof(Combustion, blast_damage), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "extinction", PIN_F32, offsetof(Combustion, extinction), sizeof(float), "float", "Heat below this stops burning without consuming the remaining fuel. Zero disables extinguishing; positive values must be below ignition.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Combustion = {
    .name = "Combustion",
    .size = sizeof(Combustion),
    .fields = Combustion_fields,
    .field_count = 13,
};

const type_field MaterialThermal_fields[] = {
    { "capacity", PIN_F32, offsetof(MaterialThermal, capacity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "conductivity", PIN_F32, offsetof(MaterialThermal, conductivity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MaterialThermal = {
    .name = "MaterialThermal",
    .size = sizeof(MaterialThermal),
    .fields = MaterialThermal_fields,
    .field_count = 2,
};

const type_field MaterialConstitutive_fields[] = {
    { "density", PIN_F32, offsetof(MaterialConstitutive, density), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "bulk_modulus", PIN_F32, offsetof(MaterialConstitutive, bulk_modulus), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shear_modulus", PIN_F32, offsetof(MaterialConstitutive, shear_modulus), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MaterialConstitutive = {
    .name = "MaterialConstitutive",
    .size = sizeof(MaterialConstitutive),
    .fields = MaterialConstitutive_fields,
    .field_count = 3,
};

const type_field Fuel_fields[] = {
    { "amount", PIN_F32, offsetof(Fuel, amount), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Fuel = {
    .name = "Fuel",
    .size = sizeof(Fuel),
    .fields = Fuel_fields,
    .field_count = 1,
};

const type_field Igniter_fields[] = {
    { "heat", PIN_F32, offsetof(Igniter, heat), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Igniter = {
    .name = "Igniter",
    .size = sizeof(Igniter),
    .fields = Igniter_fields,
    .field_count = 1,
};

const type_field Substance_fields[] = {
    { "hardness", PIN_F32, offsetof(Substance, hardness), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "blast_multiplier", PIN_F32, offsetof(Substance, blast_multiplier), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "retain_chips", PIN_BOOL, offsetof(Substance, retain_chips), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Substance = {
    .name = "Substance",
    .size = sizeof(Substance),
    .fields = Substance_fields,
    .field_count = 3,
};

const type_field MaterialCavity_fields[] = {
    { "radius", PIN_F32, offsetof(MaterialCavity, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_MaterialCavity = {
    .name = "MaterialCavity",
    .size = sizeof(MaterialCavity),
    .fields = MaterialCavity_fields,
    .field_count = 1,
};

const type_field Reflection_fields[] = {
    { "strength", PIN_F32, offsetof(Reflection, strength), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "roughness", PIN_F32, offsetof(Reflection, roughness), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "angle", PIN_F32, offsetof(Reflection, angle), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "offset", PIN_F32, offsetof(Reflection, offset), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Reflection = {
    .name = "Reflection",
    .size = sizeof(Reflection),
    .fields = Reflection_fields,
    .field_count = 4,
};

const type_field Destructible_fields[] = {
    { "cell_size", PIN_F32, offsetof(Destructible, cell_size), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "min_fragment_cells", PIN_I32, offsetof(Destructible, min_fragment_cells), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "release_fraction", PIN_F32, offsetof(Destructible, release_fraction), sizeof(float), "float", "Release control below this fraction of the original hull; zero disables.", false, false, NULL, 0, 0, false, 0, NULL },
    { "pierceable_fragments", PIN_BOOL, offsetof(Destructible, pierceable_fragments), sizeof(_Bool), "_Bool", "Detached fragments may be passed through by continuous sensors.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Destructible = {
    .name = "Destructible",
    .size = sizeof(Destructible),
    .fields = Destructible_fields,
    .field_count = 4,
};

const type_field FragmentGlow_fields[] = {
    { "duration", PIN_F32, offsetof(FragmentGlow, duration), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "flicker", PIN_F32, offsetof(FragmentGlow, flicker), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_FragmentGlow = {
    .name = "FragmentGlow",
    .size = sizeof(FragmentGlow),
    .fields = FragmentGlow_fields,
    .field_count = 2,
};

const type_field MaterialFrame_fields[] = {
    { "x", PIN_F32, offsetof(MaterialFrame, x), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(MaterialFrame, y), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radians", PIN_F32, offsetof(MaterialFrame, radians), sizeof(float), "float", NULL, false, false, "rad", 0, 0, false, 0, NULL },
};

const type_desc T_MaterialFrame = {
    .name = "MaterialFrame",
    .size = sizeof(MaterialFrame),
    .fields = MaterialFrame_fields,
    .field_count = 3,
};

const type_field MaterialSurface3D_fields[] = {
    { "enabled", PIN_BOOL, offsetof(MaterialSurface3D, enabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "material_height", PIN_BOOL, offsetof(MaterialSurface3D, material_height), sizeof(_Bool), "_Bool", "Consume the bound material's static Surface Height as mesh displacement, sampled at PartPosition=(source X, 0, source Z). Unsupported dynamic coordinates fail explicitly. Shared channels should use source X/Z.", false, false, NULL, 0, 0, false, 0, NULL },
    { "elevation", PIN_F32, offsetof(MaterialSurface3D, elevation), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "depth", PIN_F32, offsetof(MaterialSurface3D, depth), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "bevel_width", PIN_F32, offsetof(MaterialSurface3D, bevel_width), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "relief", PIN_F32, offsetof(MaterialSurface3D, relief), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "relief_scale", PIN_F32, offsetof(MaterialSurface3D, relief_scale), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "spacing", PIN_F32, offsetof(MaterialSurface3D, spacing), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "seed", PIN_U32, offsetof(MaterialSurface3D, seed), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "recess_width", PIN_F32, offsetof(MaterialSurface3D, recess_width), sizeof(float), "float", "Cutaway cave interior. Empty cells within recess_width of the contour gain rock falling up to recess_depth further from the camera, so a side-on view sees cave walls receding behind the gameplay plane. It is measured from published occupancy; terrain halos widen to cover it and detached fragments drop it. Zero width keeps the vertical contour wall.", false, false, "u", 0, 0, false, 0, NULL },
    { "recess_depth", PIN_F32, offsetof(MaterialSurface3D, recess_depth), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "recess_smoothing", PIN_F32, offsetof(MaterialSurface3D, recess_smoothing), sizeof(float), "float", "Solid thinner than twice this width, such as lavacicles, still renders but hangs in front of the recess instead of shaping it.", false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_MaterialSurface3D = {
    .name = "MaterialSurface3D",
    .size = sizeof(MaterialSurface3D),
    .fields = MaterialSurface3D_fields,
    .field_count = 12,
};

const type_field TerrainVolume_fields[] = {
    { "enabled", PIN_BOOL, offsetof(TerrainVolume, enabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "voxel", PIN_F32, offsetof(TerrainVolume, voxel), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "depth", PIN_F32, offsetof(TerrainVolume, depth), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "elevation", PIN_F32, offsetof(TerrainVolume, elevation), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "refine", PIN_I32, offsetof(TerrainVolume, refine), sizeof(int), "int", "The field is searched for surfaces every voxel * refine and sampled every voxel only near them, so fine detail costs memory by surface area rather than volume. One samples everything every voxel.", false, false, NULL, 1, 8, true, 0, NULL },
    { "open_back", PIN_BOOL, offsetof(TerrainVolume, open_back), sizeof(_Bool), "_Bool", "Leaves air the field has at the deepest layer open, so the sky shows through; closed otherwise.", false, false, NULL, 0, 0, false, 0, NULL },
    { "front", PIN_F32, offsetof(TerrainVolume, front), sizeof(float), "float", "Rock ahead of the plane: it casts shadows and blocks indirect light, so cutaway openings do not leak. It is drawn only with front_material.", false, false, "u", 0, 0, false, 0, NULL },
    { "front_material", PIN_STR, offsetof(TerrainVolume, front_material), sizeof(const char *), "const char *", "Surface material that draws the rock ahead of the plane, with its own default parameters. A masked material that opens around a Surface Influence cuts the foreground away near the player.", false, false, NULL, 0, 0, false, 0, NULL },
    { "slab", PIN_F32, offsetof(TerrainVolume, slab), sizeof(float), "float", "How far cuts carve on each side of the plane. Rock beyond it never changes, so a cut re-meshes only nearby slab chunks. Must be positive.", false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_TerrainVolume = {
    .name = "TerrainVolume",
    .size = sizeof(TerrainVolume),
    .fields = TerrainVolume_fields,
    .field_count = 9,
};

const type_field IndirectOccluder_fields[] = {
    { "enabled", PIN_BOOL, offsetof(IndirectOccluder, enabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_IndirectOccluder = {
    .name = "IndirectOccluder",
    .size = sizeof(IndirectOccluder),
    .fields = IndirectOccluder_fields,
    .field_count = 1,
};

const type_field MaterialTerrain_fields[] = {
    { "seed", PIN_U32, offsetof(MaterialTerrain, seed), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "width", PIN_F32, offsetof(MaterialTerrain, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(MaterialTerrain, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cell_size", PIN_F32, offsetof(MaterialTerrain, cell_size), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "field", PIN_STR, offsetof(MaterialTerrain, field), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "parameters", PIN_STR, offsetof(MaterialTerrain, parameters), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "connectivity", PIN_I32, offsetof(MaterialTerrain, connectivity), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "border_cells", PIN_I32, offsetof(MaterialTerrain, border_cells), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "category", PIN_U32, offsetof(MaterialTerrain, category), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mask", PIN_U32, offsetof(MaterialTerrain, mask), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "min_fragment_cells", PIN_I32, offsetof(MaterialTerrain, min_fragment_cells), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "stream_radius", PIN_F32, offsetof(MaterialTerrain, stream_radius), sizeof(float), "float", "Zero keeps a bounded terrain resident. Positive radius streams around cameras; zero width and height then allow unbounded terrain. Prefetch bakes stream_budget slices of 16 tile rows per boundary, so no step bakes a whole tile unless it is already visible.", false, false, NULL, 0, 0, false, 0, NULL },
    { "stream_budget", PIN_I32, offsetof(MaterialTerrain, stream_budget), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "island_cells", PIN_I32, offsetof(MaterialTerrain, island_cells), sizeof(int), "int", "Solid islands and air pockets smaller than this many cells that lie wholly inside a tile are removed at generation; zero keeps them.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MaterialTerrain = {
    .name = "MaterialTerrain",
    .size = sizeof(MaterialTerrain),
    .fields = MaterialTerrain_fields,
    .field_count = 14,
};

const type_field FluidVolume_fields[] = {
    { "kind", PIN_ENUM, offsetof(FluidVolume, kind), sizeof(FluidKind), "FluidKind", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "width", PIN_F32, offsetof(FluidVolume, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(FluidVolume, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "spacing", PIN_F32, offsetof(FluidVolume, spacing), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mass", PIN_F32, offsetof(FluidVolume, mass), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "color", PIN_U32, offsetof(FluidVolume, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "absorption", PIN_F32, offsetof(FluidVolume, absorption), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
    { "emission", PIN_F32, offsetof(FluidVolume, emission), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "visibility", PIN_F32, offsetof(FluidVolume, visibility), sizeof(float), "float", "Camera-only minimum visibility; contributes no radiance or bounce.", false, false, NULL, 0, 0, false, 0, NULL },
    { "refraction", PIN_F32, offsetof(FluidVolume, refraction), sizeof(float), "float", "Screen-space liquid distortion in pixels; zero disables refraction.", false, false, "px", 0, 0, false, 0, NULL },
    { "grain_radius", PIN_F32, offsetof(FluidVolume, grain_radius), sizeof(float), "float", "Positive draws individual grains at this world-space radius; zero uses the continuous fluid surface. Does not change collision support.", false, false, "u", 0, 0, false, 0, NULL },
    { "velocity", PIN_STRUCT, offsetof(FluidVolume, velocity), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_FluidVolume = {
    .name = "FluidVolume",
    .size = sizeof(FluidVolume),
    .fields = FluidVolume_fields,
    .field_count = 12,
};

const type_field Health_fields[] = {
    { "current", PIN_I32, offsetof(Health, current), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "maximum", PIN_I32, offsetof(Health, maximum), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Health = {
    .name = "Health",
    .size = sizeof(Health),
    .fields = Health_fields,
    .field_count = 2,
};

const type_field Movement_fields[] = {
    { "speed", PIN_F32, offsetof(Movement, speed), sizeof(float), "float", NULL, false, false, "u/s", 0, 0, false, 0, NULL },
};

const type_desc T_Movement = {
    .name = "Movement",
    .size = sizeof(Movement),
    .fields = Movement_fields,
    .field_count = 1,
};

const type_field Input_fields[] = {
    { "move", PIN_STRUCT, offsetof(Input, move), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Input = {
    .name = "Input",
    .size = sizeof(Input),
    .fields = Input_fields,
    .field_count = 1,
};

const type_field Weapon_fields[] = {
    { "damage", PIN_I32, offsetof(Weapon, damage), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cooldown", PIN_F32, offsetof(Weapon, cooldown), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
};

const type_desc T_Weapon = {
    .name = "Weapon",
    .size = sizeof(Weapon),
    .fields = Weapon_fields,
    .field_count = 2,
};

const type_field Projectile_fields[] = {
    { "speed", PIN_F32, offsetof(Projectile, speed), sizeof(float), "float", NULL, false, false, "u/s", 0, 0, false, 0, NULL },
    { "lifetime", PIN_F32, offsetof(Projectile, lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "damage", PIN_I32, offsetof(Projectile, damage), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "age", PIN_F32, offsetof(Projectile, age), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Projectile = {
    .name = "Projectile",
    .size = sizeof(Projectile),
    .fields = Projectile_fields,
    .field_count = 4,
};

const type_field Appearance_fields[] = {
    { "width", PIN_F32, offsetof(Appearance, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(Appearance, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "color", PIN_U32, offsetof(Appearance, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "shape", PIN_ENUM, offsetof(Appearance, shape), sizeof(Shape), "Shape", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "border_width", PIN_F32, offsetof(Appearance, border_width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "border_color", PIN_U32, offsetof(Appearance, border_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "glow_radius", PIN_F32, offsetof(Appearance, glow_radius), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "glow_color", PIN_U32, offsetof(Appearance, glow_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "stroke_width", PIN_F32, offsetof(Appearance, stroke_width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "antialias", PIN_F32, offsetof(Appearance, antialias), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
    { "layer", PIN_I32, offsetof(Appearance, layer), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "relief", PIN_F32, offsetof(Appearance, relief), sizeof(float), "float", "World units the shape stands above the plane where its scene color covers, as a Sprite's relief; a material graph's Height adds to it. In Depth thickness lighting, a light standing higher passes over lower shapes and reaches raised surfaces.", false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_Appearance = {
    .name = "Appearance",
    .size = sizeof(Appearance),
    .fields = Appearance_fields,
    .field_count = 12,
};

const type_field Sprite_fields[] = {
    { "image", PIN_STR, offsetof(Sprite, image), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "frame", PIN_I32, offsetof(Sprite, frame), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "frame_width", PIN_I32, offsetof(Sprite, frame_width), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "frame_height", PIN_I32, offsetof(Sprite, frame_height), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "flip_x", PIN_BOOL, offsetof(Sprite, flip_x), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "layer", PIN_I32, offsetof(Sprite, layer), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "normal_map", PIN_STR, offsetof(Sprite, normal_map), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height_map", PIN_STR, offsetof(Sprite, height_map), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "relief", PIN_F32, offsetof(Sprite, relief), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_Sprite = {
    .name = "Sprite",
    .size = sizeof(Sprite),
    .fields = Sprite_fields,
    .field_count = 9,
};

const type_field Tilemap_fields[] = {
    { "image", PIN_STR, offsetof(Tilemap, image), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cells", PIN_STR, offsetof(Tilemap, cells), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "columns", PIN_I32, offsetof(Tilemap, columns), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "rows", PIN_I32, offsetof(Tilemap, rows), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "frame_width", PIN_I32, offsetof(Tilemap, frame_width), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "frame_height", PIN_I32, offsetof(Tilemap, frame_height), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "layer", PIN_I32, offsetof(Tilemap, layer), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "normal_map", PIN_STR, offsetof(Tilemap, normal_map), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height_map", PIN_STR, offsetof(Tilemap, height_map), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "relief", PIN_F32, offsetof(Tilemap, relief), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_Tilemap = {
    .name = "Tilemap",
    .size = sizeof(Tilemap),
    .fields = Tilemap_fields,
    .field_count = 10,
};

const type_field Orientation_fields[] = {
    { "radians", PIN_F32, offsetof(Orientation, radians), sizeof(float), "float", NULL, false, false, "rad", 0, 0, false, 0, NULL },
};

const type_desc T_Orientation = {
    .name = "Orientation",
    .size = sizeof(Orientation),
    .fields = Orientation_fields,
    .field_count = 1,
};

const type_field Material_fields[] = {
    { "scene", PIN_U32, offsetof(Material, scene), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "albedo", PIN_U32, offsetof(Material, albedo), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "filter", PIN_U32, offsetof(Material, filter), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "emission_color", PIN_U32, offsetof(Material, emission_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "emission_intensity", PIN_F32, offsetof(Material, emission_intensity), sizeof(float), "float", NULL, false, false, NULL, 0, 4, false, 0, NULL },
    { "emission_multiplier", PIN_F32, offsetof(Material, emission_multiplier), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "absorption", PIN_F32, offsetof(Material, absorption), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
    { "border_scene", PIN_U32, offsetof(Material, border_scene), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "border_emission_color", PIN_U32, offsetof(Material, border_emission_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "border_albedo", PIN_U32, offsetof(Material, border_albedo), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "border_filter", PIN_U32, offsetof(Material, border_filter), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "edge_light", PIN_F32, offsetof(Material, edge_light), sizeof(float), "float", "Gain of light received near the surface of a solid, reaching HRC2D.surface_radius into it. Zero disables; no unlit outline. With Depth thickness, a solid that absorbs nothing instead takes its HRC light shaded by its facing and by heights in the way, this much.", false, false, NULL, 0, 1, false, 0, NULL },
};

const type_desc T_Material = {
    .name = "Material",
    .size = sizeof(Material),
    .fields = Material_fields,
    .field_count = 12,
};

const type_field Shader_fields[] = {
    { "material", PIN_STR, offsetof(Shader, material), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shape", PIN_STR, offsetof(Shader, shape), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "parameters", PIN_STR, offsetof(Shader, parameters), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "bounds_x", PIN_F32, offsetof(Shader, bounds_x), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "bounds_y", PIN_F32, offsetof(Shader, bounds_y), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Shader = {
    .name = "Shader",
    .size = sizeof(Shader),
    .fields = Shader_fields,
    .field_count = 5,
};

const type_field Geometry3D_fields[] = {
    { "primitive", PIN_I32, offsetof(Geometry3D, primitive), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "width", PIN_F32, offsetof(Geometry3D, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "depth", PIN_F32, offsetof(Geometry3D, depth), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(Geometry3D, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "elevation", PIN_F32, offsetof(Geometry3D, elevation), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Geometry3D = {
    .name = "Geometry3D",
    .size = sizeof(Geometry3D),
    .fields = Geometry3D_fields,
    .field_count = 5,
};

const type_field Aim_fields[] = {
    { "direction", PIN_STRUCT, offsetof(Aim, direction), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "firing", PIN_BOOL, offsetof(Aim, firing), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "manual", PIN_BOOL, offsetof(Aim, manual), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Aim = {
    .name = "Aim",
    .size = sizeof(Aim),
    .fields = Aim_fields,
    .field_count = 3,
};

const type_field Dash_fields[] = {
    { "speed", PIN_F32, offsetof(Dash, speed), sizeof(float), "float", NULL, false, false, "u/s", 0, 0, false, 0, NULL },
    { "duration", PIN_F32, offsetof(Dash, duration), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "cooldown", PIN_F32, offsetof(Dash, cooldown), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "damage", PIN_I32, offsetof(Dash, damage), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "remaining", PIN_F32, offsetof(Dash, remaining), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cooldown_remaining", PIN_F32, offsetof(Dash, cooldown_remaining), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "direction", PIN_STRUCT, offsetof(Dash, direction), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Dash = {
    .name = "Dash",
    .size = sizeof(Dash),
    .fields = Dash_fields,
    .field_count = 7,
};

const type_field SpawnRequest_fields[] = {
    { "prototype", PIN_STR, offsetof(SpawnRequest, prototype), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "delay", PIN_F32, offsetof(SpawnRequest, delay), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
};

const type_desc T_SpawnRequest = {
    .name = "SpawnRequest",
    .size = sizeof(SpawnRequest),
    .fields = SpawnRequest_fields,
    .field_count = 2,
};

const type_field Encounter_fields[] = {
    { "status", PIN_I32, offsetof(Encounter, status), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "total", PIN_I32, offsetof(Encounter, total), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "spawned", PIN_I32, offsetof(Encounter, spawned), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "alive", PIN_I32, offsetof(Encounter, alive), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Encounter = {
    .name = "Encounter",
    .size = sizeof(Encounter),
    .fields = Encounter_fields,
    .field_count = 4,
};

const type_field EncounterMember_fields[] = {
    { "role", PIN_I32, offsetof(EncounterMember, role), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_EncounterMember = {
    .name = "EncounterMember",
    .size = sizeof(EncounterMember),
    .fields = EncounterMember_fields,
    .field_count = 1,
};

const type_field Environment_fields[] = {
    { "exposure", PIN_F32, offsetof(Environment, exposure), sizeof(float), "float", NULL, false, false, "EV", 0, 0, false, 0, NULL },
    { "bloom_strength", PIN_F32, offsetof(Environment, bloom_strength), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "bloom_threshold", PIN_F32, offsetof(Environment, bloom_threshold), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "bloom_radius", PIN_F32, offsetof(Environment, bloom_radius), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "ambient_sky", PIN_U32, offsetof(Environment, ambient_sky), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "ambient_ground", PIN_U32, offsetof(Environment, ambient_ground), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "ambient_intensity", PIN_F32, offsetof(Environment, ambient_intensity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "tone_mapping", PIN_ENUM, offsetof(Environment, tone_mapping), sizeof(ToneMapping), "ToneMapping", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "auto_exposure", PIN_BOOL, offsetof(Environment, auto_exposure), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "adaptation_speed", PIN_F32, offsetof(Environment, adaptation_speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sky", PIN_STR, offsetof(Environment, sky), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sky_material", PIN_STR, offsetof(Environment, sky_material), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sky_rotation", PIN_F32, offsetof(Environment, sky_rotation), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sky_exposure", PIN_F32, offsetof(Environment, sky_exposure), sizeof(float), "float", NULL, false, false, "EV", 0, 0, false, 0, NULL },
    { "occlusion", PIN_ENUM, offsetof(Environment, occlusion), sizeof(Occlusion), "Occlusion", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "occlusion_radius", PIN_F32, offsetof(Environment, occlusion_radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "global_illumination", PIN_ENUM, offsetof(Environment, global_illumination), sizeof(GlobalIllumination), "GlobalIllumination", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "screen_light", PIN_ENUM, offsetof(Environment, screen_light), sizeof(ScreenLight), "ScreenLight", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fog_density", PIN_F32, offsetof(Environment, fog_density), sizeof(float), "float", "Homogeneous scene3d medium. Zero density disables all fog work. Start and distance are measured along the camera ray from its near plane. Color is scattering albedo, not an ambient source. Steps and resolution independently bound integration and pixel cost.", false, false, "1/u", 0, 1, true, 0, NULL },
    { "fog_start", PIN_F32, offsetof(Environment, fog_start), sizeof(float), "float", NULL, false, false, "u", 0, 1000, false, 0, NULL },
    { "fog_distance", PIN_F32, offsetof(Environment, fog_distance), sizeof(float), "float", NULL, false, false, "u", 1, 2000, false, 0, NULL },
    { "fog_color", PIN_U32, offsetof(Environment, fog_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "fog_anisotropy", PIN_F32, offsetof(Environment, fog_anisotropy), sizeof(float), "float", NULL, false, false, NULL, -0.90000000000000002, 0.90000000000000002, true, 0, NULL },
    { "fog_steps", PIN_I32, offsetof(Environment, fog_steps), sizeof(int), "int", NULL, false, false, NULL, 4, 64, true, 0, NULL },
    { "fog_resolution", PIN_F32, offsetof(Environment, fog_resolution), sizeof(float), "float", NULL, false, false, NULL, 0.25, 1, true, 0, NULL },
};

const type_desc T_Environment = {
    .name = "Environment",
    .size = sizeof(Environment),
    .fields = Environment_fields,
    .field_count = 25,
};

const type_field DirectionalLight_fields[] = {
    { "color", PIN_U32, offsetof(DirectionalLight, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "intensity", PIN_F32, offsetof(DirectionalLight, intensity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "direction", PIN_STRUCT, offsetof(DirectionalLight, direction), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shadows", PIN_BOOL, offsetof(DirectionalLight, shadows), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "angular_diameter", PIN_F32, offsetof(DirectionalLight, angular_diameter), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_DirectionalLight = {
    .name = "DirectionalLight",
    .size = sizeof(DirectionalLight),
    .fields = DirectionalLight_fields,
    .field_count = 5,
};

const type_field PointLight_fields[] = {
    { "color", PIN_U32, offsetof(PointLight, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "intensity", PIN_F32, offsetof(PointLight, intensity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radius", PIN_F32, offsetof(PointLight, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "elevation", PIN_F32, offsetof(PointLight, elevation), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shadows", PIN_BOOL, offsetof(PointLight, shadows), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "size", PIN_F32, offsetof(PointLight, size), sizeof(float), "float", "Radius of the emitting surface. Larger sources cast softer shadow edges; zero keeps them hard.", false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_PointLight = {
    .name = "PointLight",
    .size = sizeof(PointLight),
    .fields = PointLight_fields,
    .field_count = 6,
};

const type_field SpotLight_fields[] = {
    { "color", PIN_U32, offsetof(SpotLight, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "intensity", PIN_F32, offsetof(SpotLight, intensity), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radius", PIN_F32, offsetof(SpotLight, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "elevation", PIN_F32, offsetof(SpotLight, elevation), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "direction", PIN_STRUCT, offsetof(SpotLight, direction), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "inner_angle", PIN_F32, offsetof(SpotLight, inner_angle), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "outer_angle", PIN_F32, offsetof(SpotLight, outer_angle), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "shadows", PIN_BOOL, offsetof(SpotLight, shadows), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "size", PIN_F32, offsetof(SpotLight, size), sizeof(float), "float", "Radius of the emitting surface; see Point Light.", false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_SpotLight = {
    .name = "SpotLight",
    .size = sizeof(SpotLight),
    .fields = SpotLight_fields,
    .field_count = 9,
};

const type_field SurfaceInfluence_fields[] = {
    { "color", PIN_U32, offsetof(SurfaceInfluence, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "strength", PIN_F32, offsetof(SurfaceInfluence, strength), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radius", PIN_F32, offsetof(SurfaceInfluence, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "linger", PIN_F32, offsetof(SurfaceInfluence, linger), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "inner_radius", PIN_F32, offsetof(SurfaceInfluence, inner_radius), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_SurfaceInfluence = {
    .name = "SurfaceInfluence",
    .size = sizeof(SurfaceInfluence),
    .fields = SurfaceInfluence_fields,
    .field_count = 5,
};

const type_field VisualTransform_fields[] = {
    { "pitch", PIN_F32, offsetof(VisualTransform, pitch), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "yaw", PIN_F32, offsetof(VisualTransform, yaw), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "roll", PIN_F32, offsetof(VisualTransform, roll), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "scale", PIN_F32, offsetof(VisualTransform, scale), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_VisualTransform = {
    .name = "VisualTransform",
    .size = sizeof(VisualTransform),
    .fields = VisualTransform_fields,
    .field_count = 4,
};

const type_field MeshSurface2D_fields[] = {
    { "enabled", PIN_BOOL, offsetof(MeshSurface2D, enabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MeshSurface2D = {
    .name = "MeshSurface2D",
    .size = sizeof(MeshSurface2D),
    .fields = MeshSurface2D_fields,
    .field_count = 1,
};

const type_field SdfSurface3D_fields[] = {
    { "enabled", PIN_BOOL, offsetof(SdfSurface3D, enabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "low_detail_shape", PIN_STR, offsetof(SdfSurface3D, low_detail_shape), sizeof(const char *), "const char *", "Optional simpler Shape graph below the projected pixel span. Both shapes must fit the same Geometry3D bounds and parameter bindings.", false, false, NULL, 0, 0, false, 0, NULL },
    { "low_detail_pixels", PIN_F32, offsetof(SdfSurface3D, low_detail_pixels), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_SdfSurface3D = {
    .name = "SdfSurface3D",
    .size = sizeof(SdfSurface3D),
    .fields = SdfSurface3D_fields,
    .field_count = 3,
};

const type_field SurfaceQuality_fields[] = {
    { "resolution", PIN_ENUM, offsetof(SurfaceQuality, resolution), sizeof(SurfaceResolution), "SurfaceResolution", "Supersampled uses twice each canvas dimension; Full uses one sample.", false, false, NULL, 0, 0, false, 0, NULL },
    { "sdf_pixel_error", PIN_F32, offsetof(SurfaceQuality, sdf_pixel_error), sizeof(float), "float", "Projected pixel tolerance for refined SDF tracing; zero uses the original distance-bound tolerance. Supported range is zero to one.", false, false, NULL, 0, 1, true, 0, NULL },
    { "sdf_normal_mode", PIN_ENUM, offsetof(SurfaceQuality, sdf_normal_mode), sizeof(SdfNormalMode), "SdfNormalMode", "Central uses six field samples; Tetrahedral uses four.", false, false, NULL, 0, 0, false, 0, NULL },
    { "disable_sdf_lod", PIN_BOOL, offsetof(SurfaceQuality, disable_sdf_lod), sizeof(_Bool), "_Bool", "Reference comparison retaining each object's full-detail graph.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_SurfaceQuality = {
    .name = "SurfaceQuality",
    .size = sizeof(SurfaceQuality),
    .fields = SurfaceQuality_fields,
    .field_count = 4,
};

const type_field MeshAsset_fields[] = {
    { "source", PIN_STR, offsetof(MeshAsset, source), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MeshAsset = {
    .name = "MeshAsset",
    .size = sizeof(MeshAsset),
    .fields = MeshAsset_fields,
    .field_count = 1,
};

const type_field MeshParts_fields[] = {
    { "parts", PIN_STR, offsetof(MeshParts, parts), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_MeshParts = {
    .name = "MeshParts",
    .size = sizeof(MeshParts),
    .fields = MeshParts_fields,
    .field_count = 1,
};

const type_field ParticleCurve_fields[] = {
    { "start", PIN_STRUCT, offsetof(ParticleCurve, start), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "end", PIN_STRUCT, offsetof(ParticleCurve, end), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "exponent", PIN_F32, offsetof(ParticleCurve, exponent), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_ParticleCurve = {
    .name = "ParticleCurve",
    .size = sizeof(ParticleCurve),
    .fields = ParticleCurve_fields,
    .field_count = 3,
};

const type_field ParticleEmitter_fields[] = {
    { "shape", PIN_ENUM, offsetof(ParticleEmitter, shape), sizeof(ParticleShape), "ParticleShape", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "color", PIN_U32, offsetof(ParticleEmitter, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "end_color", PIN_U32, offsetof(ParticleEmitter, end_color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "rate", PIN_F32, offsetof(ParticleEmitter, rate), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "burst_count", PIN_I32, offsetof(ParticleEmitter, burst_count), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "lifetime", PIN_STRUCT, offsetof(ParticleEmitter, lifetime), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "speed", PIN_STRUCT, offsetof(ParticleEmitter, speed), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "size", PIN_STRUCT, offsetof(ParticleEmitter, size), sizeof(ParticleCurve), "ParticleCurve", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "spread", PIN_F32, offsetof(ParticleEmitter, spread), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "drag", PIN_STRUCT, offsetof(ParticleEmitter, drag), sizeof(ParticleCurve), "ParticleCurve", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "emission", PIN_STRUCT, offsetof(ParticleEmitter, emission), sizeof(ParticleCurve), "ParticleCurve", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "antialias", PIN_STRUCT, offsetof(ParticleEmitter, antialias), sizeof(ParticleCurve), "ParticleCurve", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "absorption", PIN_F32, offsetof(ParticleEmitter, absorption), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
    { "metaball_group", PIN_I32, offsetof(ParticleEmitter, metaball_group), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "metaball_blend", PIN_F32, offsetof(ParticleEmitter, metaball_blend), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "velocity_align", PIN_BOOL, offsetof(ParticleEmitter, velocity_align), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "stretch", PIN_STRUCT, offsetof(ParticleEmitter, stretch), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "velocity_inherit", PIN_F32, offsetof(ParticleEmitter, velocity_inherit), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "offset", PIN_STRUCT, offsetof(ParticleEmitter, offset), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "direction", PIN_STRUCT, offsetof(ParticleEmitter, direction), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "parent", PIN_HANDLE, offsetof(ParticleEmitter, parent), sizeof(uint64_t), "ecs.entity", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "active", PIN_BOOL, offsetof(ParticleEmitter, active), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "planar", PIN_BOOL, offsetof(ParticleEmitter, planar), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "heading_only", PIN_BOOL, offsetof(ParticleEmitter, heading_only), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fluid", PIN_BOOL, offsetof(ParticleEmitter, fluid), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fluid_kind", PIN_ENUM, offsetof(ParticleEmitter, fluid_kind), sizeof(FluidKind), "FluidKind", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fluid_mass", PIN_F32, offsetof(ParticleEmitter, fluid_mass), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_ParticleEmitter = {
    .name = "ParticleEmitter",
    .size = sizeof(ParticleEmitter),
    .fields = ParticleEmitter_fields,
    .field_count = 27,
};

const type_field ParticleField3D_fields[] = {
    { "count", PIN_I32, offsetof(ParticleField3D, count), sizeof(int), "int", NULL, false, false, NULL, 0, 1048576, true, 0, NULL },
    { "seed", PIN_I32, offsetof(ParticleField3D, seed), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "time", PIN_F32, offsetof(ParticleField3D, time), sizeof(float), "float", "An authored clock lets a field pause independently of scene time.", false, false, "s", 0, 0, false, 0, NULL },
    { "additive", PIN_BOOL, offsetof(ParticleField3D, additive), sizeof(_Bool), "_Bool", "Additive fields test solid depth without writing depth or sorting.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_ParticleField3D = {
    .name = "ParticleField3D",
    .size = sizeof(ParticleField3D),
    .fields = ParticleField3D_fields,
    .field_count = 4,
};

const type_field Trail_fields[] = {
    { "color", PIN_U32, offsetof(Trail, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "width", PIN_F32, offsetof(Trail, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "lifetime", PIN_F32, offsetof(Trail, lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "emission", PIN_F32, offsetof(Trail, emission), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Trail = {
    .name = "Trail",
    .size = sizeof(Trail),
    .fields = Trail_fields,
    .field_count = 4,
};

const type_field Experience_fields[] = {
    { "current", PIN_I32, offsetof(Experience, current), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Experience = {
    .name = "Experience",
    .size = sizeof(Experience),
    .fields = Experience_fields,
    .field_count = 1,
};

const type_field Pickup_fields[] = {
    { "value", PIN_I32, offsetof(Pickup, value), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "lifetime", PIN_F32, offsetof(Pickup, lifetime), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "age", PIN_F32, offsetof(Pickup, age), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "state", PIN_I32, offsetof(Pickup, state), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "collection_age", PIN_F32, offsetof(Pickup, collection_age), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "target", PIN_HANDLE, offsetof(Pickup, target), sizeof(uint64_t), "ecs.entity", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "origin", PIN_STRUCT, offsetof(Pickup, origin), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Pickup = {
    .name = "Pickup",
    .size = sizeof(Pickup),
    .fields = Pickup_fields,
    .field_count = 7,
};

const type_field Camera_fields[] = {
    { "width", PIN_F32, offsetof(Camera, width), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(Camera, height), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "offset", PIN_STRUCT, offsetof(Camera, offset), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "disabled", PIN_BOOL, offsetof(Camera, disabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "focal_distance", PIN_F32, offsetof(Camera, focal_distance), sizeof(float), "float", "Zero is orthographic; positive distance places the eye along the camera's depth axis from its target.", false, false, NULL, 0, 0, false, 0, NULL },
    { "yaw", PIN_F32, offsetof(Camera, yaw), sizeof(float), "float", "Radians. Yaw turns around world +Y; pitch tilts away from the default top-down view. Transform and offset give the target's gameplay position.", false, false, "rad", 0, 0, false, 0, NULL },
    { "pitch", PIN_F32, offsetof(Camera, pitch), sizeof(float), "float", NULL, false, false, "rad", 0, 0, false, 0, NULL },
    { "roll", PIN_F32, offsetof(Camera, roll), sizeof(float), "float", "Turns the image right/down axes around the view direction.", false, false, "rad", 0, 0, false, 0, NULL },
    { "target_elevation", PIN_F32, offsetof(Camera, target_elevation), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "fov", PIN_F32, offsetof(Camera, fov), sizeof(float), "float", "3D scenes. A camera with a Transform3D looks along its local -Z with +Y up, from its position moved by the offset (gameplay y is world z); one without frames its span from a fixed overhead angle. Field of view is vertical, in radians, from 0.00001 degrees (as in Unity) to below pi, and zero picks 35 degrees. Set both clip distances or neither, with far at most a million times near; zero clips scale with the span's height.", false, false, "rad", 0, 0, false, 0, NULL },
    { "orthographic", PIN_BOOL, offsetof(Camera, orthographic), sizeof(_Bool), "_Bool", "Full-3D orthographic projection using width/height rather than fov. False preserves the perspective lens. Transform3D still places the eye.", false, false, NULL, 0, 0, false, 0, NULL },
    { "near_clip", PIN_F32, offsetof(Camera, near_clip), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "far_clip", PIN_F32, offsetof(Camera, far_clip), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
};

const type_desc T_Camera = {
    .name = "Camera",
    .size = sizeof(Camera),
    .fields = Camera_fields,
    .field_count = 13,
};

const type_field Ui_fields[] = {
    { "document", PIN_STR, offsetof(Ui, document), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "order", PIN_I32, offsetof(Ui, order), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "hidden", PIN_BOOL, offsetof(Ui, hidden), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Ui = {
    .name = "Ui",
    .size = sizeof(Ui),
    .fields = Ui_fields,
    .field_count = 3,
};

const type_field Fog_fields[] = {
    { "origin", PIN_STRUCT, offsetof(Fog, origin), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "columns", PIN_I32, offsetof(Fog, columns), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "rows", PIN_I32, offsetof(Fog, rows), sizeof(int), "int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "cell_size", PIN_F32, offsetof(Fog, cell_size), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "drift", PIN_F32, offsetof(Fog, drift), sizeof(float), "float", "Zero disables ambient fog motion.", false, false, NULL, 0, 0, false, 0, NULL },
    { "color", PIN_U32, offsetof(Fog, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "disabled", PIN_BOOL, offsetof(Fog, disabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Fog = {
    .name = "Fog",
    .size = sizeof(Fog),
    .fields = Fog_fields,
    .field_count = 7,
};

const type_field Vision_fields[] = {
    { "radius", PIN_F32, offsetof(Vision, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "occluders", PIN_U32, offsetof(Vision, occluders), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "duration", PIN_F32, offsetof(Vision, duration), sizeof(float), "float", NULL, false, false, "s", 0, 0, false, 0, NULL },
    { "age", PIN_F32, offsetof(Vision, age), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "remember", PIN_BOOL, offsetof(Vision, remember), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "disabled", PIN_BOOL, offsetof(Vision, disabled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Vision = {
    .name = "Vision",
    .size = sizeof(Vision),
    .fields = Vision_fields,
    .field_count = 6,
};

const type_field SensorContact_fields[] = {
    { "position", PIN_STRUCT, offsetof(SensorContact, position), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "remaining", PIN_F32, offsetof(SensorContact, remaining), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "color", PIN_U32, offsetof(SensorContact, color), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_SensorContact = {
    .name = "SensorContact",
    .size = sizeof(SensorContact),
    .fields = SensorContact_fields,
    .field_count = 3,
};

const type_field RenderState_fields[] = {
    { "hidden", PIN_BOOL, offsetof(RenderState, hidden), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "visibility_culled", PIN_BOOL, offsetof(RenderState, visibility_culled), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_RenderState = {
    .name = "RenderState",
    .size = sizeof(RenderState),
    .fields = RenderState_fields,
    .field_count = 2,
};

const type_field Starfield2D_fields[] = {
    { "far_layer", PIN_STRUCT, offsetof(Starfield2D, far_layer), sizeof(CF_V4), "CF_V4", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "middle_layer", PIN_STRUCT, offsetof(Starfield2D, middle_layer), sizeof(CF_V4), "CF_V4", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "near_layer", PIN_STRUCT, offsetof(Starfield2D, near_layer), sizeof(CF_V4), "CF_V4", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "parallax", PIN_STRUCT, offsetof(Starfield2D, parallax), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "color_a", PIN_U32, offsetof(Starfield2D, color_a), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "color_b", PIN_U32, offsetof(Starfield2D, color_b), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "color_c", PIN_U32, offsetof(Starfield2D, color_c), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
    { "color_d", PIN_U32, offsetof(Starfield2D, color_d), sizeof(unsigned int), "unsigned int", NULL, true, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Starfield2D = {
    .name = "Starfield2D",
    .size = sizeof(Starfield2D),
    .fields = Starfield2D_fields,
    .field_count = 8,
};

const type_field HRC2D_fields[] = {
    { "resolution", PIN_F32, offsetof(HRC2D, resolution), sizeof(float), "float", "Finest light grid as a fraction of the padded canvas; 0.5 solves one probe per 2x2 pixels. Brightness keeps this detail, and most lighting cost scales with its area. 1, 0.5, 0.25 and 0.125 give whole-pixel cells. With adaptive on, the highest resolution it uses.", false, false, NULL, 0.10000000000000001, 1, true, 0.5, NULL },
    { "adaptive", PIN_BOOL, offsetof(HRC2D, adaptive), sizeof(_Bool), "_Bool", "Steps lighting down while the scene's GPU time misses target_fps, first color detail, then resolution by halves down to 0.25, and back up when there is room. Where the GPU reports its time (Metal, and WebGL with timer queries), frames limited by the CPU or vsync keep their lighting; elsewhere it judges the whole frame's interval.", false, false, NULL, 0, 0, false, 0, NULL },
    { "target_fps", PIN_F32, offsetof(HRC2D, target_fps), sizeof(float), "float", "Frame rate adaptive keeps the scene's GPU work within.", false, false, "fps", 0, 0, false, 0, NULL },
    { "ambient", PIN_F32, offsetof(HRC2D, ambient), sizeof(float), "float", "Gain of the unlit scene color added to the frame. 1 shows materials as drawn, and lower values darken what light does not reach. In Depth thickness, taller surfaces nearby hide part of it, as they would hide the sky.", false, false, NULL, 0, 0, false, 0, NULL },
    { "sky", PIN_U32, offsetof(HRC2D, sky), sizeof(unsigned int), "unsigned int", "Light from the sky above a side view: open ground receives this color. Solids that absorb light block it like any light, so overhangs shade the ground beneath them and caves stay dark; falloff never dims it. In Depth thickness, heights nearby hide part of it, as for ambient. Black, the default, has no sky.", true, false, NULL, 0, 0, false, 0, NULL },
    { "intensity", PIN_F32, offsetof(HRC2D, intensity), sizeof(float), "float", "Brightness of emissive surfaces as drawn; the light they cast is unchanged.", false, false, NULL, 0, 0, false, 0, NULL },
    { "fog", PIN_F32, offsetof(HRC2D, fog), sizeof(float), "float", "Brightness of light seen in open air: the glow around lights.", false, false, NULL, 0, 0, false, 0, NULL },
    { "volume_density", PIN_F32, offsetof(HRC2D, volume_density), sizeof(float), "float", "Extinction/scattering coefficient for direct Spot Light beams, in inverse world units. Zero disables volume lighting. Eight view-depth samples integrate a homogeneous medium against visible depth slabs; this does not add hidden geometry or feed the HRC bounce solver.", false, false, "u^-1", 0, 1, false, 0, NULL },
    { "shadow_floor", PIN_F32, offsetof(HRC2D, shadow_floor), sizeof(float), "float", "Fraction of a solid's albedo that stays visible where no light reaches it. Depth thickness hides part of it like ambient.", false, false, NULL, 0, 0, false, 0, NULL },
    { "falloff", PIN_F32, offsetof(HRC2D, falloff), sizeof(float), "float", "How quickly light dims with distance; zero keeps it at full strength across the view.", false, false, NULL, 0, 0, false, 0, NULL },
    { "lighting_margin", PIN_F32, offsetof(HRC2D, lighting_margin), sizeof(float), "float", "World-space raster guard around the camera for nearby light transport. The default, zero, leaves offscreen sources and occluders out.", false, false, "u", 0, 0, false, 0, NULL },
    { "bounce_intensity", PIN_F32, offsetof(HRC2D, bounce_intensity), sizeof(float), "float", "Gain of light that lit surfaces reflect back into the transport; needs feedback.", false, false, NULL, 0, 0, false, 0, NULL },
    { "bounce_hysteresis", PIN_F32, offsetof(HRC2D, bounce_hysteresis), sizeof(float), "float", "Current-frame weight of bounce light: 1 disables temporal smoothing, not bounce, and lower values retain more history. Camera movement resets it.", false, false, NULL, 0, 1, true, 0.80000000000000004, NULL },
    { "radiance_hysteresis", PIN_F32, offsetof(HRC2D, radiance_hysteresis), sizeof(float), "float", "Current-frame weight of received light: 1 disables temporal smoothing, and lower values retain more history.", false, false, NULL, 0, 1, true, 0.80000000000000004, NULL },
    { "feedback", PIN_BOOL, offsetof(HRC2D, feedback), sizeof(_Bool), "_Bool", "Feeds light reflected by lit surfaces back into the transport as bounce light.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_radius", PIN_F32, offsetof(HRC2D, surface_radius), sizeof(float), "float", "World units that light received near a solid's surface reaches into it, scaled by Material.edge_light. Zero disables surface lighting.", false, false, "u", 0, 0, false, 0, NULL },
    { "surface_mode", PIN_ENUM, offsetof(HRC2D, surface_mode), sizeof(HRCSurfaceMode), "HRCSurfaceMode", "Blur spreads open-space light; Gather uses bounded visibility probes. Depth thickness is the 2.5D mode: it gathers visible emission past anything with height, and on solids that absorb nothing shades the HRC light by their facing and those heights, counting each light as much as HRC brings it. Solids that absorb light, where HRC's light stops, take the light HRC brings open ground from the lights they see above them, so a side view's tiles catch the light of lanterns in front of them. Light passes over a prop that absorbs nothing where the prop is lower than the light. Heights within the depth radius also hide ambient light, darkening the foot of a wall and the floor around a prop.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_radius", PIN_F32, offsetof(HRC2D, surface_depth_radius), sizeof(float), "float", "Screen-plane reach in project units for Depth thickness mode. Solids that absorb light take light only from the lights within reach, and a solid that absorbs nothing shades the light HRC brings it from them, and all its HRC light where they bring at least half of it, so it should reach the lights that matter.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_gain", PIN_F32, offsetof(HRC2D, surface_depth_gain), sizeof(float), "float", "Gain of the Depth light solids that absorb light take, which at 1 matches HRC's light on open ground, and of Depth highlights. Zero disables Depth thickness.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_quality", PIN_ENUM, offsetof(HRC2D, surface_depth_quality), sizeof(HRCDepthQuality), "HRCDepthQuality", "Source sampling budget; all levels preserve integrated emission.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_visibility", PIN_ENUM, offsetof(HRC2D, surface_depth_visibility), sizeof(HRCDepthVisibility), "HRCDepthVisibility", "Segments share one bounded visibility query per coarse source tile.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_shadow_softness", PIN_F32, offsetof(HRC2D, surface_depth_shadow_softness), sizeof(float), "float", "Blend visibility bins and reconstruction boundaries, in [0, 1].", false, false, NULL, 0, 1, true, 0, NULL },
    { "surface_depth_temporal", PIN_BOOL, offsetof(HRC2D, surface_depth_temporal), sizeof(_Bool), "_Bool", "Reproject and validate a short history of the Depth contribution.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_depth_history", PIN_F32, offsetof(HRC2D, surface_depth_history), sizeof(float), "float", "Maximum previous-frame weight; zero selects the default of 0.65.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_thickness", PIN_F32, offsetof(HRC2D, surface_thickness), sizeof(float), "float", "How far below its visible top a surface blocks Depth light and ambient light, in project units. A light that absorbs nothing never blocks.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_relief", PIN_F32, offsetof(HRC2D, surface_relief), sizeof(float), "float", "Sideways reach of nearby light relative to its height above the plane; larger values deepen normal-mapped relief.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_shadow_mode", PIN_ENUM, offsetof(HRC2D, surface_shadow_mode), sizeof(HRCShadowMode), "HRCShadowMode", "Local heightfield visibility; horizons store four or eight azimuths.", false, false, NULL, 0, 0, false, 0, NULL },
    { "surface_bevel", PIN_F32, offsetof(HRC2D, surface_bevel), sizeof(float), "float", "Outward tilt of solid normals near silhouettes, which catches rim light.", false, false, NULL, 0, 0, false, 0, NULL },
    { "specular", PIN_F32, offsetof(HRC2D, specular), sizeof(float), "float", "Gain of glossy highlights on solids with low material roughness.", false, false, NULL, 0, 0, false, 0, NULL },
    { "tone_mapping", PIN_ENUM, offsetof(HRC2D, tone_mapping), sizeof(HRCToneMapping), "HRCToneMapping", "0 clamps the composite to the display range. 1 keeps it in HDR and presents it with bloom, exposure and AgX tone mapping.", false, false, NULL, 0, 0, false, 0, NULL },
    { "exposure", PIN_F32, offsetof(HRC2D, exposure), sizeof(float), "float", "Exposure in EV before tone mapping.", false, false, "EV", 0, 0, false, 0, NULL },
    { "bloom", PIN_F32, offsetof(HRC2D, bloom), sizeof(float), "float", "Fraction of the blurred image mixed into the frame, in [0, 1].", false, false, NULL, 0, 1, true, 0, NULL },
};

const type_desc T_HRC2D = {
    .name = "HRC2D",
    .size = sizeof(HRC2D),
    .fields = HRC2D_fields,
    .field_count = 32,
};

const type_field EntityCondition_fields[] = {
    { "component", PIN_STR, offsetof(EntityCondition, component), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "field", PIN_STR, offsetof(EntityCondition, field), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "test", PIN_STR, offsetof(EntityCondition, test), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "value", PIN_F64, offsetof(EntityCondition, value), sizeof(double), "double", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_EntityCondition = {
    .name = "EntityCondition",
    .size = sizeof(EntityCondition),
    .fields = EntityCondition_fields,
    .field_count = 4,
};

const type_field Transform3D_fields[] = {
    { "position", PIN_STRUCT, offsetof(Transform3D, position), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "rotation", PIN_STRUCT, offsetof(Transform3D, rotation), sizeof(CF_Quat), "CF_Quat", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "scale", PIN_STRUCT, offsetof(Transform3D, scale), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Transform3D = {
    .name = "Transform3D",
    .size = sizeof(Transform3D),
    .fields = Transform3D_fields,
    .field_count = 3,
};

const type_field GlobalTransform3D_fields[] = {
    { "x_axis", PIN_STRUCT, offsetof(GlobalTransform3D, x_axis), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "y_axis", PIN_STRUCT, offsetof(GlobalTransform3D, y_axis), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "z_axis", PIN_STRUCT, offsetof(GlobalTransform3D, z_axis), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "position", PIN_STRUCT, offsetof(GlobalTransform3D, position), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_GlobalTransform3D = {
    .name = "GlobalTransform3D",
    .size = sizeof(GlobalTransform3D),
    .fields = GlobalTransform3D_fields,
    .field_count = 4,
};

const type_field GlobalTransform2D_fields[] = {
    { "position", PIN_STRUCT, offsetof(GlobalTransform2D, position), sizeof(CF_V2), "vec2", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radians", PIN_F32, offsetof(GlobalTransform2D, radians), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_GlobalTransform2D = {
    .name = "GlobalTransform2D",
    .size = sizeof(GlobalTransform2D),
    .fields = GlobalTransform2D_fields,
    .field_count = 2,
};

const type_field Collider3D_fields[] = {
    { "shape", PIN_ENUM, offsetof(Collider3D, shape), sizeof(Collider3DShape), "Collider3DShape", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "size", PIN_STRUCT, offsetof(Collider3D, size), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "radius", PIN_F32, offsetof(Collider3D, radius), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "height", PIN_F32, offsetof(Collider3D, height), sizeof(float), "float", NULL, false, false, "u", 0, 0, false, 0, NULL },
    { "mesh", PIN_STR, offsetof(Collider3D, mesh), sizeof(const char *), "const char *", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "category", PIN_U32, offsetof(Collider3D, category), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mask", PIN_U32, offsetof(Collider3D, mask), sizeof(unsigned int), "unsigned int", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "sensor", PIN_BOOL, offsetof(Collider3D, sensor), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "friction", PIN_F32, offsetof(Collider3D, friction), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
    { "restitution", PIN_F32, offsetof(Collider3D, restitution), sizeof(float), "float", NULL, false, false, NULL, 0, 1, false, 0, NULL },
};

const type_desc T_Collider3D = {
    .name = "Collider3D",
    .size = sizeof(Collider3D),
    .fields = Collider3D_fields,
    .field_count = 10,
};

const type_field Body3D_fields[] = {
    { "type", PIN_ENUM, offsetof(Body3D, type), sizeof(BodyType), "BodyType", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "linear_velocity", PIN_STRUCT, offsetof(Body3D, linear_velocity), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "angular_velocity", PIN_STRUCT, offsetof(Body3D, angular_velocity), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "mass", PIN_F32, offsetof(Body3D, mass), sizeof(float), "float", NULL, false, false, "kg", 0, 0, false, 0, NULL },
};

const type_desc T_Body3D = {
    .name = "Body3D",
    .size = sizeof(Body3D),
    .fields = Body3D_fields,
    .field_count = 4,
};

const type_field Gravity3D_fields[] = {
    { "acceleration", PIN_STRUCT, offsetof(Gravity3D, acceleration), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Gravity3D = {
    .name = "Gravity3D",
    .size = sizeof(Gravity3D),
    .fields = Gravity3D_fields,
    .field_count = 1,
};

const type_field Character3D_fields[] = {
    { "on_floor", PIN_BOOL, offsetof(Character3D, on_floor), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "floor_normal", PIN_STRUCT, offsetof(Character3D, floor_normal), sizeof(CF_V3), "vec3", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Character3D = {
    .name = "Character3D",
    .size = sizeof(Character3D),
    .fields = Character3D_fields,
    .field_count = 2,
};

const type_field Joint3D_fields[] = {
    { "kind", PIN_ENUM, offsetof(Joint3D, kind), sizeof(Joint3DKind), "Joint3DKind", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "other", PIN_HANDLE, offsetof(Joint3D, other), sizeof(uint64_t), "ecs.entity", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "collide", PIN_BOOL, offsetof(Joint3D, collide), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "limit", PIN_BOOL, offsetof(Joint3D, limit), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "lower", PIN_F32, offsetof(Joint3D, lower), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "upper", PIN_F32, offsetof(Joint3D, upper), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "motor", PIN_BOOL, offsetof(Joint3D, motor), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "speed", PIN_F32, offsetof(Joint3D, speed), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "strength", PIN_F32, offsetof(Joint3D, strength), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "spring", PIN_BOOL, offsetof(Joint3D, spring), sizeof(_Bool), "_Bool", NULL, false, false, NULL, 0, 0, false, 0, NULL },
    { "hertz", PIN_F32, offsetof(Joint3D, hertz), sizeof(float), "float", NULL, false, false, "Hz", 0, 0, false, 0, NULL },
    { "damping", PIN_F32, offsetof(Joint3D, damping), sizeof(float), "float", NULL, false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_Joint3D = {
    .name = "Joint3D",
    .size = sizeof(Joint3D),
    .fields = Joint3D_fields,
    .field_count = 12,
};

const type_field CF_V2_fields[] = {
    { "x", PIN_F32, offsetof(CF_V2, x), sizeof(float), "float", "The x component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(CF_V2, y), sizeof(float), "float", "The y component.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_CF_V2 = {
    .name = "vec2",
    .size = sizeof(CF_V2),
    .fields = CF_V2_fields,
    .field_count = 2,
};

const type_field CF_V3_fields[] = {
    { "x", PIN_F32, offsetof(CF_V3, x), sizeof(float), "float", "The x component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(CF_V3, y), sizeof(float), "float", "The y component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "z", PIN_F32, offsetof(CF_V3, z), sizeof(float), "float", "The z component.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_CF_V3 = {
    .name = "vec3",
    .size = sizeof(CF_V3),
    .fields = CF_V3_fields,
    .field_count = 3,
};

const type_field CF_V4_fields[] = {
    { "x", PIN_F32, offsetof(CF_V4, x), sizeof(float), "float", "The x component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(CF_V4, y), sizeof(float), "float", "The y component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "z", PIN_F32, offsetof(CF_V4, z), sizeof(float), "float", "The z component.", false, false, NULL, 0, 0, false, 0, NULL },
    { "w", PIN_F32, offsetof(CF_V4, w), sizeof(float), "float", "The w component.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_CF_V4 = {
    .name = "CF_V4",
    .size = sizeof(CF_V4),
    .fields = CF_V4_fields,
    .field_count = 4,
};

const type_field CF_Quat_fields[] = {
    { "x", PIN_F32, offsetof(CF_Quat, x), sizeof(float), "float", "The x component of the vector part.", false, false, NULL, 0, 0, false, 0, NULL },
    { "y", PIN_F32, offsetof(CF_Quat, y), sizeof(float), "float", "The y component of the vector part.", false, false, NULL, 0, 0, false, 0, NULL },
    { "z", PIN_F32, offsetof(CF_Quat, z), sizeof(float), "float", "The z component of the vector part.", false, false, NULL, 0, 0, false, 0, NULL },
    { "w", PIN_F32, offsetof(CF_Quat, w), sizeof(float), "float", "The scalar part.", false, false, NULL, 0, 0, false, 0, NULL },
};

const type_desc T_CF_Quat = {
    .name = "CF_Quat",
    .size = sizeof(CF_Quat),
    .fields = CF_Quat_fields,
    .field_count = 4,
};

const type_desc *const REGISTRY_TYPES[] = {
    &T_Transform,
    &T_Velocity,
    &T_GlobalBinding,
    &T_Body,
    &T_Gravity,
    &T_Collider,
    &T_Drive,
    &T_Joint,
    &T_EngineSystem,
    &T_MaterialFluidWorld,
    &T_MaterialDestructionWorld,
    &T_MaterialBreakProfile,
    &T_Combustion,
    &T_MaterialThermal,
    &T_MaterialConstitutive,
    &T_Fuel,
    &T_Igniter,
    &T_Substance,
    &T_MaterialCavity,
    &T_Reflection,
    &T_Destructible,
    &T_FragmentGlow,
    &T_MaterialFrame,
    &T_MaterialSurface3D,
    &T_TerrainVolume,
    &T_IndirectOccluder,
    &T_MaterialTerrain,
    &T_FluidVolume,
    &T_Health,
    &T_Movement,
    &T_Input,
    &T_Weapon,
    &T_Projectile,
    &T_Appearance,
    &T_Sprite,
    &T_Tilemap,
    &T_Orientation,
    &T_Material,
    &T_Shader,
    &T_Geometry3D,
    &T_Aim,
    &T_Dash,
    &T_SpawnRequest,
    &T_Encounter,
    &T_EncounterMember,
    &T_Environment,
    &T_DirectionalLight,
    &T_PointLight,
    &T_SpotLight,
    &T_SurfaceInfluence,
    &T_VisualTransform,
    &T_MeshSurface2D,
    &T_SdfSurface3D,
    &T_SurfaceQuality,
    &T_MeshAsset,
    &T_MeshParts,
    &T_ParticleCurve,
    &T_ParticleEmitter,
    &T_ParticleField3D,
    &T_Trail,
    &T_Experience,
    &T_Pickup,
    &T_Camera,
    &T_Ui,
    &T_Fog,
    &T_Vision,
    &T_SensorContact,
    &T_RenderState,
    &T_Starfield2D,
    &T_HRC2D,
    &T_EntityCondition,
    &T_Transform3D,
    &T_GlobalTransform3D,
    &T_GlobalTransform2D,
    &T_Collider3D,
    &T_Body3D,
    &T_Gravity3D,
    &T_Character3D,
    &T_Joint3D,
    &T_CF_V2,
    &T_CF_V3,
    &T_CF_V4,
    &T_CF_Quat,
};
const int REGISTRY_TYPE_COUNT = 83;

static const enum_value_desc ENUM_idtype_t_VALUES[] = {
    { 0, "P_ALL", "All" },
    { 1, "P_PID", "Pid" },
    { 2, "P_PGID", "Pgid" },
};
const enum_desc ENUM_idtype_t = {
    .name = "idtype_t",
    .values = ENUM_idtype_t_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_CF_ShapeType_VALUES[] = {
    { 0, "CF_SHAPE_TYPE_NONE", "None" },
    { 1, "CF_SHAPE_TYPE_CIRCLE", "Circle" },
    { 2, "CF_SHAPE_TYPE_AABB", "Aabb" },
    { 3, "CF_SHAPE_TYPE_CAPSULE", "Capsule" },
    { 4, "CF_SHAPE_TYPE_POLY", "Poly" },
};
const enum_desc ENUM_CF_ShapeType = {
    .name = "CF_ShapeType",
    .values = ENUM_CF_ShapeType_VALUES,
    .value_count = 5,
};

static const enum_value_desc ENUM_CF_ShapeType3_VALUES[] = {
    { 0, "CF_SHAPE3_TYPE_NONE", "None" },
    { 1, "CF_SHAPE3_TYPE_SPHERE", "Sphere" },
    { 2, "CF_SHAPE3_TYPE_AABB", "Aabb" },
    { 3, "CF_SHAPE3_TYPE_CAPSULE", "Capsule" },
    { 4, "CF_SHAPE3_TYPE_TRIANGLE", "Triangle" },
};
const enum_desc ENUM_CF_ShapeType3 = {
    .name = "CF_ShapeType3",
    .values = ENUM_CF_ShapeType3_VALUES,
    .value_count = 5,
};

static const enum_value_desc ENUM_BodyType_VALUES[] = {
    { 0, "BODY_STATIC", "Static" },
    { 1, "BODY_DYNAMIC", "Dynamic" },
    { 2, "BODY_KINEMATIC", "Kinematic" },
};
const enum_desc ENUM_BodyType = {
    .name = "BodyType",
    .values = ENUM_BodyType_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_ColliderShape_VALUES[] = {
    { 0, "COLLIDER_CIRCLE", "Circle" },
    { 1, "COLLIDER_BOX", "Box" },
};
const enum_desc ENUM_ColliderShape = {
    .name = "ColliderShape",
    .values = ENUM_ColliderShape_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_JointKind_VALUES[] = {
    { 0, "JOINT_HINGE", "Hinge" },
    { 1, "JOINT_SLIDER", "Slider" },
    { 2, "JOINT_WELD", "Weld" },
    { 3, "JOINT_DISTANCE", "Distance" },
};
const enum_desc ENUM_JointKind = {
    .name = "JointKind",
    .values = ENUM_JointKind_VALUES,
    .value_count = 4,
};

static const enum_value_desc ENUM_MaterialSolver_VALUES[] = {
    { 0, "MATERIAL_SOLVER_DDR", "Ddr" },
    { 1, "MATERIAL_SOLVER_MPM", "Mpm" },
};
const enum_desc ENUM_MaterialSolver = {
    .name = "MaterialSolver",
    .values = ENUM_MaterialSolver_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_MaterialDebugView_VALUES[] = {
    { 0, "MATERIAL_VIEW_MATERIAL", "Material" },
    { 1, "MATERIAL_VIEW_HEAT", "Heat" },
    { 2, "MATERIAL_VIEW_SPEED", "Speed" },
    { 3, "MATERIAL_VIEW_STRAIN", "Strain" },
};
const enum_desc ENUM_MaterialDebugView = {
    .name = "MaterialDebugView",
    .values = ENUM_MaterialDebugView_VALUES,
    .value_count = 4,
};

static const enum_value_desc ENUM_FluidKind_VALUES[] = {
    { 0, "FLUID_KIND_GAS", "Gas" },
    { 1, "FLUID_KIND_LIQUID", "Liquid" },
};
const enum_desc ENUM_FluidKind = {
    .name = "FluidKind",
    .values = ENUM_FluidKind_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_Shape_VALUES[] = {
    { 0, "SHAPE_BOX", "Box" },
    { 1, "SHAPE_CIRCLE", "Circle" },
    { 2, "SHAPE_DIAMOND", "Diamond" },
    { 3, "SHAPE_CHEVRON", "Chevron" },
    { 4, "SHAPE_STROKED_CHEVRON", "Stroked Chevron" },
    { 5, "SHAPE_CAPSULE", "Capsule" },
    { 6, "SHAPE_TRIANGLE", "Triangle" },
    { 7, "SHAPE_STROKED_TRIANGLE", "Stroked Triangle" },
};
const enum_desc ENUM_Shape = {
    .name = "Shape",
    .values = ENUM_Shape_VALUES,
    .value_count = 8,
};

static const enum_value_desc ENUM_ToneMapping_VALUES[] = {
    { 0, "TONE_MAPPING_AGX", "AgX" },
    { 1, "TONE_MAPPING_NEUTRAL", "Neutral" },
};
const enum_desc ENUM_ToneMapping = {
    .name = "ToneMapping",
    .values = ENUM_ToneMapping_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_Occlusion_VALUES[] = {
    { 0, "OCCLUSION_OFF", "Off" },
    { 1, "OCCLUSION_LOW", "Low" },
    { 2, "OCCLUSION_MEDIUM", "Medium" },
    { 3, "OCCLUSION_HIGH", "High" },
};
const enum_desc ENUM_Occlusion = {
    .name = "Occlusion",
    .values = ENUM_Occlusion_VALUES,
    .value_count = 4,
};

static const enum_value_desc ENUM_ScreenLight_VALUES[] = {
    { 0, "SCREEN_LIGHT_OFF", "Off" },
    { 1, "SCREEN_LIGHT_SCREEN", "Screen" },
    { 2, "SCREEN_LIGHT_WORLD", "World" },
};
const enum_desc ENUM_ScreenLight = {
    .name = "ScreenLight",
    .values = ENUM_ScreenLight_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_GlobalIllumination_VALUES[] = {
    { 0, "GLOBAL_ILLUMINATION_OFF", "Off" },
    { 1, "GLOBAL_ILLUMINATION_LOW", "Low" },
    { 2, "GLOBAL_ILLUMINATION_MEDIUM", "Medium" },
    { 3, "GLOBAL_ILLUMINATION_HIGH", "High" },
};
const enum_desc ENUM_GlobalIllumination = {
    .name = "GlobalIllumination",
    .values = ENUM_GlobalIllumination_VALUES,
    .value_count = 4,
};

static const enum_value_desc ENUM_SurfaceResolution_VALUES[] = {
    { 0, "SURFACE_SUPERSAMPLED", "Supersampled" },
    { 1, "SURFACE_FULL", "Full" },
};
const enum_desc ENUM_SurfaceResolution = {
    .name = "SurfaceResolution",
    .values = ENUM_SurfaceResolution_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_SdfNormalMode_VALUES[] = {
    { 0, "SDF_NORMAL_CENTRAL", "Central" },
    { 1, "SDF_NORMAL_TETRAHEDRAL", "Tetrahedral" },
    { 2, "SDF_NORMAL_ANALYTIC", "Analytic" },
};
const enum_desc ENUM_SdfNormalMode = {
    .name = "SdfNormalMode",
    .values = ENUM_SdfNormalMode_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_ParticleShape_VALUES[] = {
    { 0, "PARTICLE_ROUND", "Round" },
    { 1, "PARTICLE_TRIANGLE", "Triangle" },
    { 2, "PARTICLE_TRIANGLE_OUTLINE", "Triangle Outline" },
    { 3, "PARTICLE_RING", "Ring" },
    { 4, "PARTICLE_STREAK", "Streak" },
};
const enum_desc ENUM_ParticleShape = {
    .name = "ParticleShape",
    .values = ENUM_ParticleShape_VALUES,
    .value_count = 5,
};

static const enum_value_desc ENUM_Control_VALUES[] = {
    { 0, "CONTROL_NONE", "None" },
    { 1, "CONTROL_MOUSE_LEFT", "Mouse Left" },
    { 2, "CONTROL_MOUSE_RIGHT", "Mouse Right" },
    { 3, "CONTROL_MOUSE_MIDDLE", "Mouse Middle" },
    { 4, "CONTROL_SPACE", "Space" },
    { 5, "CONTROL_SHIFT", "Shift" },
    { 6, "CONTROL_CONTROL", "Control" },
    { 7, "CONTROL_ALT", "Alt" },
    { 8, "CONTROL_TAB", "Tab" },
    { 9, "CONTROL_ENTER", "Enter" },
    { 10, "CONTROL_BACKSPACE", "Backspace" },
    { 11, "CONTROL_ARROW_UP", "Arrow Up" },
    { 12, "CONTROL_ARROW_DOWN", "Arrow Down" },
    { 13, "CONTROL_ARROW_LEFT", "Arrow Left" },
    { 14, "CONTROL_ARROW_RIGHT", "Arrow Right" },
    { 15, "CONTROL_A", "A" },
    { 16, "CONTROL_B", "B" },
    { 17, "CONTROL_C", "C" },
    { 18, "CONTROL_D", "D" },
    { 19, "CONTROL_E", "E" },
    { 20, "CONTROL_F", "F" },
    { 21, "CONTROL_G", "G" },
    { 22, "CONTROL_H", "H" },
    { 23, "CONTROL_I", "I" },
    { 24, "CONTROL_J", "J" },
    { 25, "CONTROL_K", "K" },
    { 26, "CONTROL_L", "L" },
    { 27, "CONTROL_M", "M" },
    { 28, "CONTROL_N", "N" },
    { 29, "CONTROL_O", "O" },
    { 30, "CONTROL_P", "P" },
    { 31, "CONTROL_Q", "Q" },
    { 32, "CONTROL_R", "R" },
    { 33, "CONTROL_S", "S" },
    { 34, "CONTROL_T", "T" },
    { 35, "CONTROL_U", "U" },
    { 36, "CONTROL_V", "V" },
    { 37, "CONTROL_W", "W" },
    { 38, "CONTROL_X", "X" },
    { 39, "CONTROL_Y", "Y" },
    { 40, "CONTROL_Z", "Z" },
    { 41, "CONTROL_0", "0" },
    { 42, "CONTROL_1", "1" },
    { 43, "CONTROL_2", "2" },
    { 44, "CONTROL_3", "3" },
    { 45, "CONTROL_4", "4" },
    { 46, "CONTROL_5", "5" },
    { 47, "CONTROL_6", "6" },
    { 48, "CONTROL_7", "7" },
    { 49, "CONTROL_8", "8" },
    { 50, "CONTROL_9", "9" },
    { 51, "CONTROL_PAD_A", "Pad A" },
    { 52, "CONTROL_PAD_B", "Pad B" },
    { 53, "CONTROL_PAD_X", "Pad X" },
    { 54, "CONTROL_PAD_Y", "Pad Y" },
    { 55, "CONTROL_PAD_LEFT_SHOULDER", "Pad Left Shoulder" },
    { 56, "CONTROL_PAD_RIGHT_SHOULDER", "Pad Right Shoulder" },
    { 57, "CONTROL_PAD_LEFT_TRIGGER", "Pad Left Trigger" },
    { 58, "CONTROL_PAD_RIGHT_TRIGGER", "Pad Right Trigger" },
    { 59, "CONTROL_AIM_STICK", "Aim Stick" },
};
const enum_desc ENUM_Control = {
    .name = "Control",
    .values = ENUM_Control_VALUES,
    .value_count = 60,
};

static const enum_value_desc ENUM_PadControl_VALUES[] = {
    { 0, "PAD_NONE", "None" },
    { 1, "PAD_A", "A" },
    { 2, "PAD_B", "B" },
    { 3, "PAD_X", "X" },
    { 4, "PAD_Y", "Y" },
    { 5, "PAD_BACK", "Back" },
    { 6, "PAD_START", "Start" },
    { 7, "PAD_LEFT_SHOULDER", "Left Shoulder" },
    { 8, "PAD_RIGHT_SHOULDER", "Right Shoulder" },
    { 9, "PAD_DPAD_UP", "Dpad Up" },
    { 10, "PAD_DPAD_DOWN", "Dpad Down" },
    { 11, "PAD_DPAD_LEFT", "Dpad Left" },
    { 12, "PAD_DPAD_RIGHT", "Dpad Right" },
    { 13, "PAD_LEFT_STICK_UP", "Left Stick Up" },
    { 14, "PAD_LEFT_STICK_DOWN", "Left Stick Down" },
    { 15, "PAD_LEFT_STICK_LEFT", "Left Stick Left" },
    { 16, "PAD_LEFT_STICK_RIGHT", "Left Stick Right" },
    { 17, "PAD_LEFT_TRIGGER", "Left Trigger" },
    { 18, "PAD_RIGHT_TRIGGER", "Right Trigger" },
};
const enum_desc ENUM_PadControl = {
    .name = "PadControl",
    .values = ENUM_PadControl_VALUES,
    .value_count = 19,
};

static const enum_value_desc ENUM_HRCToneMapping_VALUES[] = {
    { 0, "HRC_TONE_CLAMP", "Clamp" },
    { 1, "HRC_TONE_AGX", "AgX" },
};
const enum_desc ENUM_HRCToneMapping = {
    .name = "HRCToneMapping",
    .values = ENUM_HRCToneMapping_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_HRCShadowMode_VALUES[] = {
    { 0, "HRC_SHADOW_REFERENCE", "Reference" },
    { 1, "HRC_SHADOW_HORIZON4", "Horizon4" },
    { 2, "HRC_SHADOW_HORIZON8", "Horizon8" },
};
const enum_desc ENUM_HRCShadowMode = {
    .name = "HRCShadowMode",
    .values = ENUM_HRCShadowMode_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_HRCSurfaceMode_VALUES[] = {
    { 0, "HRC_SURFACE_BLUR", "Blur" },
    { 1, "HRC_SURFACE_GATHER", "Gather" },
    { 2, "HRC_SURFACE_DEPTH", "Depth thickness" },
};
const enum_desc ENUM_HRCSurfaceMode = {
    .name = "HRCSurfaceMode",
    .values = ENUM_HRCSurfaceMode_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_HRCDepthQuality_VALUES[] = {
    { 0, "HRC_DEPTH_LOW", "Low" },
    { 1, "HRC_DEPTH_BALANCED", "Balanced" },
    { 2, "HRC_DEPTH_HIGH", "High" },
};
const enum_desc ENUM_HRCDepthQuality = {
    .name = "HRCDepthQuality",
    .values = ENUM_HRCDepthQuality_VALUES,
    .value_count = 3,
};

static const enum_value_desc ENUM_HRCDepthVisibility_VALUES[] = {
    { 0, "HRC_DEPTH_VISIBILITY_MASK", "Angular masks" },
    { 1, "HRC_DEPTH_VISIBILITY_SEGMENTS", "Bounded segments" },
};
const enum_desc ENUM_HRCDepthVisibility = {
    .name = "HRCDepthVisibility",
    .values = ENUM_HRCDepthVisibility_VALUES,
    .value_count = 2,
};

static const enum_value_desc ENUM_Collider3DShape_VALUES[] = {
    { 0, "COLLIDER3D_BOX", "Box" },
    { 1, "COLLIDER3D_SPHERE", "Sphere" },
    { 2, "COLLIDER3D_CAPSULE", "Capsule" },
    { 3, "COLLIDER3D_HULL", "Hull" },
    { 4, "COLLIDER3D_MESH", "Mesh" },
};
const enum_desc ENUM_Collider3DShape = {
    .name = "Collider3DShape",
    .values = ENUM_Collider3DShape_VALUES,
    .value_count = 5,
};

static const enum_value_desc ENUM_Joint3DKind_VALUES[] = {
    { 0, "JOINT3D_HINGE", "Hinge" },
    { 1, "JOINT3D_SLIDER", "Slider" },
    { 2, "JOINT3D_BALL", "Ball" },
    { 3, "JOINT3D_WELD", "Weld" },
    { 4, "JOINT3D_DISTANCE", "Distance" },
};
const enum_desc ENUM_Joint3DKind = {
    .name = "Joint3DKind",
    .values = ENUM_Joint3DKind_VALUES,
    .value_count = 5,
};

const enum_desc *const REGISTRY_ENUMS[] = {
    &ENUM_idtype_t,
    &ENUM_CF_ShapeType,
    &ENUM_CF_ShapeType3,
    &ENUM_BodyType,
    &ENUM_ColliderShape,
    &ENUM_JointKind,
    &ENUM_MaterialSolver,
    &ENUM_MaterialDebugView,
    &ENUM_FluidKind,
    &ENUM_Shape,
    &ENUM_ToneMapping,
    &ENUM_Occlusion,
    &ENUM_ScreenLight,
    &ENUM_GlobalIllumination,
    &ENUM_SurfaceResolution,
    &ENUM_SdfNormalMode,
    &ENUM_ParticleShape,
    &ENUM_Control,
    &ENUM_PadControl,
    &ENUM_HRCToneMapping,
    &ENUM_HRCShadowMode,
    &ENUM_HRCSurfaceMode,
    &ENUM_HRCDepthQuality,
    &ENUM_HRCDepthVisibility,
    &ENUM_Collider3DShape,
    &ENUM_Joint3DKind,
};
const int REGISTRY_ENUM_COUNT = 26;

const ecs_component_desc COMPONENT_Transform = { .id = UINT64_C(1376052968821171689), .key = "Transform", .name = "Transform", .element_size = sizeof(Transform), .type = &T_Transform, .index = ECS_INDEX_BITSET };
Transform *Transform_get(ecs_world *world, ecs_entity entity) { return (Transform *)ecs_component_data(world, entity, COMPONENT_Transform.id); }
const Transform *Transform_get_const(const ecs_world *world, ecs_entity entity) { return (const Transform *)ecs_component_data_const(world, entity, COMPONENT_Transform.id); }
int Transform_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Transform.id); }
int Transform_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Transform.id, NULL, sort_key); }
int Transform_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Transform.id, sort_key); }

const ecs_component_desc COMPONENT_Velocity = { .id = UINT64_C(10879062153742453694), .key = "Velocity", .name = "Velocity", .element_size = sizeof(Velocity), .type = &T_Velocity, .index = ECS_INDEX_BITSET };
Velocity *Velocity_get(ecs_world *world, ecs_entity entity) { return (Velocity *)ecs_component_data(world, entity, COMPONENT_Velocity.id); }
const Velocity *Velocity_get_const(const ecs_world *world, ecs_entity entity) { return (const Velocity *)ecs_component_data_const(world, entity, COMPONENT_Velocity.id); }
int Velocity_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Velocity.id); }
int Velocity_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Velocity.id, NULL, sort_key); }
int Velocity_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Velocity.id, sort_key); }

const ecs_component_desc COMPONENT_GlobalBinding = { .id = UINT64_C(1572183154514976376), .key = "GlobalBinding", .name = "GlobalBinding", .element_size = sizeof(GlobalBinding), .type = &T_GlobalBinding, .index = ECS_INDEX_DENSE };
GlobalBinding *GlobalBinding_get(ecs_world *world, ecs_entity entity) { return (GlobalBinding *)ecs_component_data(world, entity, COMPONENT_GlobalBinding.id); }
const GlobalBinding *GlobalBinding_get_const(const ecs_world *world, ecs_entity entity) { return (const GlobalBinding *)ecs_component_data_const(world, entity, COMPONENT_GlobalBinding.id); }
int GlobalBinding_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_GlobalBinding.id); }
int GlobalBinding_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_GlobalBinding.id, NULL, sort_key); }
int GlobalBinding_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_GlobalBinding.id, sort_key); }

const ecs_component_desc COMPONENT_Body = { .id = UINT64_C(13261214849415044345), .key = "Body", .name = "Body", .element_size = sizeof(Body), .type = &T_Body, .index = ECS_INDEX_DENSE };
Body *Body_get(ecs_world *world, ecs_entity entity) { return (Body *)ecs_component_data(world, entity, COMPONENT_Body.id); }
const Body *Body_get_const(const ecs_world *world, ecs_entity entity) { return (const Body *)ecs_component_data_const(world, entity, COMPONENT_Body.id); }
int Body_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Body.id); }
int Body_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Body.id, NULL, sort_key); }
int Body_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Body.id, sort_key); }

const ecs_component_desc COMPONENT_Gravity = { .id = UINT64_C(6751754628985040281), .key = "Gravity", .name = "Gravity", .element_size = sizeof(Gravity), .type = &T_Gravity, .index = ECS_INDEX_DENSE };
Gravity *Gravity_get(ecs_world *world, ecs_entity entity) { return (Gravity *)ecs_component_data(world, entity, COMPONENT_Gravity.id); }
const Gravity *Gravity_get_const(const ecs_world *world, ecs_entity entity) { return (const Gravity *)ecs_component_data_const(world, entity, COMPONENT_Gravity.id); }
int Gravity_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Gravity.id); }
int Gravity_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Gravity.id, NULL, sort_key); }
int Gravity_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Gravity.id, sort_key); }

const ecs_component_desc COMPONENT_Collider = { .id = UINT64_C(5720808045910798521), .key = "Collider", .name = "Collider", .element_size = sizeof(Collider), .type = &T_Collider, .index = ECS_INDEX_DENSE };
Collider *Collider_get(ecs_world *world, ecs_entity entity) { return (Collider *)ecs_component_data(world, entity, COMPONENT_Collider.id); }
const Collider *Collider_get_const(const ecs_world *world, ecs_entity entity) { return (const Collider *)ecs_component_data_const(world, entity, COMPONENT_Collider.id); }
int Collider_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Collider.id); }
int Collider_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Collider.id, NULL, sort_key); }
int Collider_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Collider.id, sort_key); }

const ecs_component_desc COMPONENT_Drive = { .id = UINT64_C(12718337771097733051), .key = "Drive", .name = "Drive", .element_size = sizeof(Drive), .type = &T_Drive, .index = ECS_INDEX_DENSE };
Drive *Drive_get(ecs_world *world, ecs_entity entity) { return (Drive *)ecs_component_data(world, entity, COMPONENT_Drive.id); }
const Drive *Drive_get_const(const ecs_world *world, ecs_entity entity) { return (const Drive *)ecs_component_data_const(world, entity, COMPONENT_Drive.id); }
int Drive_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Drive.id); }
int Drive_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Drive.id, NULL, sort_key); }
int Drive_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Drive.id, sort_key); }

const ecs_component_desc COMPONENT_Joint = { .id = UINT64_C(3136175112040316091), .key = "Joint", .name = "Joint", .element_size = sizeof(Joint), .type = &T_Joint, .index = ECS_INDEX_DENSE };
Joint *Joint_get(ecs_world *world, ecs_entity entity) { return (Joint *)ecs_component_data(world, entity, COMPONENT_Joint.id); }
const Joint *Joint_get_const(const ecs_world *world, ecs_entity entity) { return (const Joint *)ecs_component_data_const(world, entity, COMPONENT_Joint.id); }
int Joint_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Joint.id); }
int Joint_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Joint.id, NULL, sort_key); }
int Joint_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Joint.id, sort_key); }

const ecs_component_desc COMPONENT_EngineSystem = { .id = UINT64_C(15258781457616275116), .key = "EngineSystem", .name = "EngineSystem", .element_size = sizeof(EngineSystem), .type = &T_EngineSystem, .index = ECS_INDEX_DENSE };
EngineSystem *EngineSystem_get(ecs_world *world, ecs_entity entity) { return (EngineSystem *)ecs_component_data(world, entity, COMPONENT_EngineSystem.id); }
const EngineSystem *EngineSystem_get_const(const ecs_world *world, ecs_entity entity) { return (const EngineSystem *)ecs_component_data_const(world, entity, COMPONENT_EngineSystem.id); }
int EngineSystem_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_EngineSystem.id); }
int EngineSystem_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_EngineSystem.id, NULL, sort_key); }
int EngineSystem_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_EngineSystem.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialFluidWorld = { .id = UINT64_C(15822790923764555406), .key = "MaterialFluidWorld", .name = "MaterialFluidWorld", .element_size = sizeof(MaterialFluidWorld), .type = &T_MaterialFluidWorld, .index = ECS_INDEX_DENSE };
MaterialFluidWorld *MaterialFluidWorld_get(ecs_world *world, ecs_entity entity) { return (MaterialFluidWorld *)ecs_component_data(world, entity, COMPONENT_MaterialFluidWorld.id); }
const MaterialFluidWorld *MaterialFluidWorld_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialFluidWorld *)ecs_component_data_const(world, entity, COMPONENT_MaterialFluidWorld.id); }
int MaterialFluidWorld_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialFluidWorld.id); }
int MaterialFluidWorld_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialFluidWorld.id, NULL, sort_key); }
int MaterialFluidWorld_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialFluidWorld.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialDestructionWorld = { .id = UINT64_C(11573540429726462858), .key = "MaterialDestructionWorld", .name = "MaterialDestructionWorld", .element_size = sizeof(MaterialDestructionWorld), .type = &T_MaterialDestructionWorld, .index = ECS_INDEX_DENSE };
MaterialDestructionWorld *MaterialDestructionWorld_get(ecs_world *world, ecs_entity entity) { return (MaterialDestructionWorld *)ecs_component_data(world, entity, COMPONENT_MaterialDestructionWorld.id); }
const MaterialDestructionWorld *MaterialDestructionWorld_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialDestructionWorld *)ecs_component_data_const(world, entity, COMPONENT_MaterialDestructionWorld.id); }
int MaterialDestructionWorld_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialDestructionWorld.id); }
int MaterialDestructionWorld_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialDestructionWorld.id, NULL, sort_key); }
int MaterialDestructionWorld_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialDestructionWorld.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialBreakProfile = { .id = UINT64_C(13976942854392459914), .key = "MaterialBreakProfile", .name = "MaterialBreakProfile", .element_size = sizeof(MaterialBreakProfile), .type = &T_MaterialBreakProfile, .index = ECS_INDEX_DENSE };
MaterialBreakProfile *MaterialBreakProfile_get(ecs_world *world, ecs_entity entity) { return (MaterialBreakProfile *)ecs_component_data(world, entity, COMPONENT_MaterialBreakProfile.id); }
const MaterialBreakProfile *MaterialBreakProfile_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialBreakProfile *)ecs_component_data_const(world, entity, COMPONENT_MaterialBreakProfile.id); }
int MaterialBreakProfile_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialBreakProfile.id); }
int MaterialBreakProfile_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialBreakProfile.id, NULL, sort_key); }
int MaterialBreakProfile_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialBreakProfile.id, sort_key); }

const ecs_component_desc COMPONENT_Combustion = { .id = UINT64_C(4828855705484780302), .key = "Combustion", .name = "Combustion", .element_size = sizeof(Combustion), .type = &T_Combustion, .index = ECS_INDEX_DENSE };
Combustion *Combustion_get(ecs_world *world, ecs_entity entity) { return (Combustion *)ecs_component_data(world, entity, COMPONENT_Combustion.id); }
const Combustion *Combustion_get_const(const ecs_world *world, ecs_entity entity) { return (const Combustion *)ecs_component_data_const(world, entity, COMPONENT_Combustion.id); }
int Combustion_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Combustion.id); }
int Combustion_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Combustion.id, NULL, sort_key); }
int Combustion_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Combustion.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialThermal = { .id = UINT64_C(2570905452892637745), .key = "MaterialThermal", .name = "MaterialThermal", .element_size = sizeof(MaterialThermal), .type = &T_MaterialThermal, .index = ECS_INDEX_DENSE };
MaterialThermal *MaterialThermal_get(ecs_world *world, ecs_entity entity) { return (MaterialThermal *)ecs_component_data(world, entity, COMPONENT_MaterialThermal.id); }
const MaterialThermal *MaterialThermal_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialThermal *)ecs_component_data_const(world, entity, COMPONENT_MaterialThermal.id); }
int MaterialThermal_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialThermal.id); }
int MaterialThermal_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialThermal.id, NULL, sort_key); }
int MaterialThermal_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialThermal.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialConstitutive = { .id = UINT64_C(666372496313879041), .key = "MaterialConstitutive", .name = "MaterialConstitutive", .element_size = sizeof(MaterialConstitutive), .type = &T_MaterialConstitutive, .index = ECS_INDEX_DENSE };
MaterialConstitutive *MaterialConstitutive_get(ecs_world *world, ecs_entity entity) { return (MaterialConstitutive *)ecs_component_data(world, entity, COMPONENT_MaterialConstitutive.id); }
const MaterialConstitutive *MaterialConstitutive_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialConstitutive *)ecs_component_data_const(world, entity, COMPONENT_MaterialConstitutive.id); }
int MaterialConstitutive_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialConstitutive.id); }
int MaterialConstitutive_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialConstitutive.id, NULL, sort_key); }
int MaterialConstitutive_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialConstitutive.id, sort_key); }

const ecs_component_desc COMPONENT_Fuel = { .id = UINT64_C(7425494363805320913), .key = "Fuel", .name = "Fuel", .element_size = sizeof(Fuel), .type = &T_Fuel, .index = ECS_INDEX_DENSE };
Fuel *Fuel_get(ecs_world *world, ecs_entity entity) { return (Fuel *)ecs_component_data(world, entity, COMPONENT_Fuel.id); }
const Fuel *Fuel_get_const(const ecs_world *world, ecs_entity entity) { return (const Fuel *)ecs_component_data_const(world, entity, COMPONENT_Fuel.id); }
int Fuel_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Fuel.id); }
int Fuel_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Fuel.id, NULL, sort_key); }
int Fuel_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Fuel.id, sort_key); }

const ecs_component_desc COMPONENT_Igniter = { .id = UINT64_C(9213577023823450083), .key = "Igniter", .name = "Igniter", .element_size = sizeof(Igniter), .type = &T_Igniter, .index = ECS_INDEX_DENSE };
Igniter *Igniter_get(ecs_world *world, ecs_entity entity) { return (Igniter *)ecs_component_data(world, entity, COMPONENT_Igniter.id); }
const Igniter *Igniter_get_const(const ecs_world *world, ecs_entity entity) { return (const Igniter *)ecs_component_data_const(world, entity, COMPONENT_Igniter.id); }
int Igniter_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Igniter.id); }
int Igniter_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Igniter.id, NULL, sort_key); }
int Igniter_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Igniter.id, sort_key); }

const ecs_component_desc COMPONENT_Substance = { .id = UINT64_C(5584890992779773777), .key = "Substance", .name = "Substance", .element_size = sizeof(Substance), .type = &T_Substance, .index = ECS_INDEX_DENSE };
Substance *Substance_get(ecs_world *world, ecs_entity entity) { return (Substance *)ecs_component_data(world, entity, COMPONENT_Substance.id); }
const Substance *Substance_get_const(const ecs_world *world, ecs_entity entity) { return (const Substance *)ecs_component_data_const(world, entity, COMPONENT_Substance.id); }
int Substance_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Substance.id); }
int Substance_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Substance.id, NULL, sort_key); }
int Substance_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Substance.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialCavity = { .id = UINT64_C(10223750595691985006), .key = "MaterialCavity", .name = "MaterialCavity", .element_size = sizeof(MaterialCavity), .type = &T_MaterialCavity, .index = ECS_INDEX_DENSE };
MaterialCavity *MaterialCavity_get(ecs_world *world, ecs_entity entity) { return (MaterialCavity *)ecs_component_data(world, entity, COMPONENT_MaterialCavity.id); }
const MaterialCavity *MaterialCavity_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialCavity *)ecs_component_data_const(world, entity, COMPONENT_MaterialCavity.id); }
int MaterialCavity_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialCavity.id); }
int MaterialCavity_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialCavity.id, NULL, sort_key); }
int MaterialCavity_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialCavity.id, sort_key); }

const ecs_component_desc COMPONENT_Reflection = { .id = UINT64_C(2951674686928084704), .key = "Reflection", .name = "Reflection", .element_size = sizeof(Reflection), .type = &T_Reflection, .index = ECS_INDEX_DENSE };
Reflection *Reflection_get(ecs_world *world, ecs_entity entity) { return (Reflection *)ecs_component_data(world, entity, COMPONENT_Reflection.id); }
const Reflection *Reflection_get_const(const ecs_world *world, ecs_entity entity) { return (const Reflection *)ecs_component_data_const(world, entity, COMPONENT_Reflection.id); }
int Reflection_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Reflection.id); }
int Reflection_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Reflection.id, NULL, sort_key); }
int Reflection_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Reflection.id, sort_key); }

const ecs_component_desc COMPONENT_Destructible = { .id = UINT64_C(9765619990782515159), .key = "Destructible", .name = "Destructible", .element_size = sizeof(Destructible), .type = &T_Destructible, .index = ECS_INDEX_DENSE };
Destructible *Destructible_get(ecs_world *world, ecs_entity entity) { return (Destructible *)ecs_component_data(world, entity, COMPONENT_Destructible.id); }
const Destructible *Destructible_get_const(const ecs_world *world, ecs_entity entity) { return (const Destructible *)ecs_component_data_const(world, entity, COMPONENT_Destructible.id); }
int Destructible_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Destructible.id); }
int Destructible_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Destructible.id, NULL, sort_key); }
int Destructible_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Destructible.id, sort_key); }

const ecs_component_desc COMPONENT_FragmentGlow = { .id = UINT64_C(5101314601776981304), .key = "FragmentGlow", .name = "FragmentGlow", .element_size = sizeof(FragmentGlow), .type = &T_FragmentGlow, .index = ECS_INDEX_DENSE };
FragmentGlow *FragmentGlow_get(ecs_world *world, ecs_entity entity) { return (FragmentGlow *)ecs_component_data(world, entity, COMPONENT_FragmentGlow.id); }
const FragmentGlow *FragmentGlow_get_const(const ecs_world *world, ecs_entity entity) { return (const FragmentGlow *)ecs_component_data_const(world, entity, COMPONENT_FragmentGlow.id); }
int FragmentGlow_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_FragmentGlow.id); }
int FragmentGlow_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_FragmentGlow.id, NULL, sort_key); }
int FragmentGlow_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_FragmentGlow.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialFrame = { .id = UINT64_C(15817955399041484001), .key = "MaterialFrame", .name = "MaterialFrame", .element_size = sizeof(MaterialFrame), .type = &T_MaterialFrame, .index = ECS_INDEX_DENSE };
MaterialFrame *MaterialFrame_get(ecs_world *world, ecs_entity entity) { return (MaterialFrame *)ecs_component_data(world, entity, COMPONENT_MaterialFrame.id); }
const MaterialFrame *MaterialFrame_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialFrame *)ecs_component_data_const(world, entity, COMPONENT_MaterialFrame.id); }
int MaterialFrame_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialFrame.id); }
int MaterialFrame_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialFrame.id, NULL, sort_key); }
int MaterialFrame_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialFrame.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialSurface3D = { .id = UINT64_C(13468644155485958468), .key = "MaterialSurface3D", .name = "MaterialSurface3D", .element_size = sizeof(MaterialSurface3D), .type = &T_MaterialSurface3D, .index = ECS_INDEX_DENSE };
MaterialSurface3D *MaterialSurface3D_get(ecs_world *world, ecs_entity entity) { return (MaterialSurface3D *)ecs_component_data(world, entity, COMPONENT_MaterialSurface3D.id); }
const MaterialSurface3D *MaterialSurface3D_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialSurface3D *)ecs_component_data_const(world, entity, COMPONENT_MaterialSurface3D.id); }
int MaterialSurface3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialSurface3D.id); }
int MaterialSurface3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialSurface3D.id, NULL, sort_key); }
int MaterialSurface3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialSurface3D.id, sort_key); }

const ecs_component_desc COMPONENT_TerrainVolume = { .id = UINT64_C(17145811741932608848), .key = "TerrainVolume", .name = "TerrainVolume", .element_size = sizeof(TerrainVolume), .type = &T_TerrainVolume, .index = ECS_INDEX_DENSE };
TerrainVolume *TerrainVolume_get(ecs_world *world, ecs_entity entity) { return (TerrainVolume *)ecs_component_data(world, entity, COMPONENT_TerrainVolume.id); }
const TerrainVolume *TerrainVolume_get_const(const ecs_world *world, ecs_entity entity) { return (const TerrainVolume *)ecs_component_data_const(world, entity, COMPONENT_TerrainVolume.id); }
int TerrainVolume_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_TerrainVolume.id); }
int TerrainVolume_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_TerrainVolume.id, NULL, sort_key); }
int TerrainVolume_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_TerrainVolume.id, sort_key); }

const ecs_component_desc COMPONENT_IndirectOccluder = { .id = UINT64_C(12712491993781860452), .key = "IndirectOccluder", .name = "IndirectOccluder", .element_size = sizeof(IndirectOccluder), .type = &T_IndirectOccluder, .index = ECS_INDEX_DENSE };
IndirectOccluder *IndirectOccluder_get(ecs_world *world, ecs_entity entity) { return (IndirectOccluder *)ecs_component_data(world, entity, COMPONENT_IndirectOccluder.id); }
const IndirectOccluder *IndirectOccluder_get_const(const ecs_world *world, ecs_entity entity) { return (const IndirectOccluder *)ecs_component_data_const(world, entity, COMPONENT_IndirectOccluder.id); }
int IndirectOccluder_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_IndirectOccluder.id); }
int IndirectOccluder_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_IndirectOccluder.id, NULL, sort_key); }
int IndirectOccluder_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_IndirectOccluder.id, sort_key); }

const ecs_component_desc COMPONENT_MaterialTerrain = { .id = UINT64_C(2362275859285985971), .key = "MaterialTerrain", .name = "MaterialTerrain", .element_size = sizeof(MaterialTerrain), .type = &T_MaterialTerrain, .index = ECS_INDEX_DENSE };
MaterialTerrain *MaterialTerrain_get(ecs_world *world, ecs_entity entity) { return (MaterialTerrain *)ecs_component_data(world, entity, COMPONENT_MaterialTerrain.id); }
const MaterialTerrain *MaterialTerrain_get_const(const ecs_world *world, ecs_entity entity) { return (const MaterialTerrain *)ecs_component_data_const(world, entity, COMPONENT_MaterialTerrain.id); }
int MaterialTerrain_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MaterialTerrain.id); }
int MaterialTerrain_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MaterialTerrain.id, NULL, sort_key); }
int MaterialTerrain_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MaterialTerrain.id, sort_key); }

const ecs_component_desc COMPONENT_FluidVolume = { .id = UINT64_C(6156356865550169961), .key = "FluidVolume", .name = "FluidVolume", .element_size = sizeof(FluidVolume), .type = &T_FluidVolume, .index = ECS_INDEX_DENSE };
FluidVolume *FluidVolume_get(ecs_world *world, ecs_entity entity) { return (FluidVolume *)ecs_component_data(world, entity, COMPONENT_FluidVolume.id); }
const FluidVolume *FluidVolume_get_const(const ecs_world *world, ecs_entity entity) { return (const FluidVolume *)ecs_component_data_const(world, entity, COMPONENT_FluidVolume.id); }
int FluidVolume_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_FluidVolume.id); }
int FluidVolume_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_FluidVolume.id, NULL, sort_key); }
int FluidVolume_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_FluidVolume.id, sort_key); }

const ecs_component_desc COMPONENT_Health = { .id = UINT64_C(4594947644670299697), .key = "Health", .name = "Health", .element_size = sizeof(Health), .type = &T_Health, .index = ECS_INDEX_DENSE };
Health *Health_get(ecs_world *world, ecs_entity entity) { return (Health *)ecs_component_data(world, entity, COMPONENT_Health.id); }
const Health *Health_get_const(const ecs_world *world, ecs_entity entity) { return (const Health *)ecs_component_data_const(world, entity, COMPONENT_Health.id); }
int Health_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Health.id); }
int Health_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Health.id, NULL, sort_key); }
int Health_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Health.id, sort_key); }

const ecs_component_desc COMPONENT_Movement = { .id = UINT64_C(992505279072269918), .key = "Movement", .name = "Movement", .element_size = sizeof(Movement), .type = &T_Movement, .index = ECS_INDEX_DENSE };
Movement *Movement_get(ecs_world *world, ecs_entity entity) { return (Movement *)ecs_component_data(world, entity, COMPONENT_Movement.id); }
const Movement *Movement_get_const(const ecs_world *world, ecs_entity entity) { return (const Movement *)ecs_component_data_const(world, entity, COMPONENT_Movement.id); }
int Movement_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Movement.id); }
int Movement_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Movement.id, NULL, sort_key); }
int Movement_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Movement.id, sort_key); }

const ecs_component_desc COMPONENT_Input = { .id = UINT64_C(194520663397847877), .key = "Input", .name = "Input", .element_size = sizeof(Input), .type = &T_Input, .index = ECS_INDEX_DENSE };
Input *Input_get(ecs_world *world, ecs_entity entity) { return (Input *)ecs_component_data(world, entity, COMPONENT_Input.id); }
const Input *Input_get_const(const ecs_world *world, ecs_entity entity) { return (const Input *)ecs_component_data_const(world, entity, COMPONENT_Input.id); }
int Input_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Input.id); }
int Input_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Input.id, NULL, sort_key); }
int Input_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Input.id, sort_key); }

const ecs_component_desc COMPONENT_Weapon = { .id = UINT64_C(6573267426768237093), .key = "Weapon", .name = "Weapon", .element_size = sizeof(Weapon), .type = &T_Weapon, .index = ECS_INDEX_DENSE };
Weapon *Weapon_get(ecs_world *world, ecs_entity entity) { return (Weapon *)ecs_component_data(world, entity, COMPONENT_Weapon.id); }
const Weapon *Weapon_get_const(const ecs_world *world, ecs_entity entity) { return (const Weapon *)ecs_component_data_const(world, entity, COMPONENT_Weapon.id); }
int Weapon_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Weapon.id); }
int Weapon_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Weapon.id, NULL, sort_key); }
int Weapon_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Weapon.id, sort_key); }

const ecs_component_desc COMPONENT_Projectile = { .id = UINT64_C(5701897700160923974), .key = "Projectile", .name = "Projectile", .element_size = sizeof(Projectile), .type = &T_Projectile, .index = ECS_INDEX_DENSE };
Projectile *Projectile_get(ecs_world *world, ecs_entity entity) { return (Projectile *)ecs_component_data(world, entity, COMPONENT_Projectile.id); }
const Projectile *Projectile_get_const(const ecs_world *world, ecs_entity entity) { return (const Projectile *)ecs_component_data_const(world, entity, COMPONENT_Projectile.id); }
int Projectile_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Projectile.id); }
int Projectile_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Projectile.id, NULL, sort_key); }
int Projectile_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Projectile.id, sort_key); }

const ecs_component_desc COMPONENT_Appearance = { .id = UINT64_C(6860109605235283595), .key = "Appearance", .name = "Appearance", .element_size = sizeof(Appearance), .type = &T_Appearance, .index = ECS_INDEX_DENSE };
Appearance *Appearance_get(ecs_world *world, ecs_entity entity) { return (Appearance *)ecs_component_data(world, entity, COMPONENT_Appearance.id); }
const Appearance *Appearance_get_const(const ecs_world *world, ecs_entity entity) { return (const Appearance *)ecs_component_data_const(world, entity, COMPONENT_Appearance.id); }
int Appearance_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Appearance.id); }
int Appearance_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Appearance.id, NULL, sort_key); }
int Appearance_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Appearance.id, sort_key); }

const ecs_component_desc COMPONENT_Sprite = { .id = UINT64_C(10033067928008147280), .key = "Sprite", .name = "Sprite", .element_size = sizeof(Sprite), .type = &T_Sprite, .index = ECS_INDEX_DENSE };
Sprite *Sprite_get(ecs_world *world, ecs_entity entity) { return (Sprite *)ecs_component_data(world, entity, COMPONENT_Sprite.id); }
const Sprite *Sprite_get_const(const ecs_world *world, ecs_entity entity) { return (const Sprite *)ecs_component_data_const(world, entity, COMPONENT_Sprite.id); }
int Sprite_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Sprite.id); }
int Sprite_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Sprite.id, NULL, sort_key); }
int Sprite_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Sprite.id, sort_key); }

const ecs_component_desc COMPONENT_Tilemap = { .id = UINT64_C(4195326914233710945), .key = "Tilemap", .name = "Tilemap", .element_size = sizeof(Tilemap), .type = &T_Tilemap, .index = ECS_INDEX_DENSE };
Tilemap *Tilemap_get(ecs_world *world, ecs_entity entity) { return (Tilemap *)ecs_component_data(world, entity, COMPONENT_Tilemap.id); }
const Tilemap *Tilemap_get_const(const ecs_world *world, ecs_entity entity) { return (const Tilemap *)ecs_component_data_const(world, entity, COMPONENT_Tilemap.id); }
int Tilemap_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Tilemap.id); }
int Tilemap_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Tilemap.id, NULL, sort_key); }
int Tilemap_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Tilemap.id, sort_key); }

const ecs_component_desc COMPONENT_Orientation = { .id = UINT64_C(13392774897737294773), .key = "Orientation", .name = "Orientation", .element_size = sizeof(Orientation), .type = &T_Orientation, .index = ECS_INDEX_DENSE };
Orientation *Orientation_get(ecs_world *world, ecs_entity entity) { return (Orientation *)ecs_component_data(world, entity, COMPONENT_Orientation.id); }
const Orientation *Orientation_get_const(const ecs_world *world, ecs_entity entity) { return (const Orientation *)ecs_component_data_const(world, entity, COMPONENT_Orientation.id); }
int Orientation_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Orientation.id); }
int Orientation_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Orientation.id, NULL, sort_key); }
int Orientation_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Orientation.id, sort_key); }

const ecs_component_desc COMPONENT_Material = { .id = UINT64_C(13209786540468567076), .key = "Material", .name = "Material", .element_size = sizeof(Material), .type = &T_Material, .index = ECS_INDEX_DENSE };
Material *Material_get(ecs_world *world, ecs_entity entity) { return (Material *)ecs_component_data(world, entity, COMPONENT_Material.id); }
const Material *Material_get_const(const ecs_world *world, ecs_entity entity) { return (const Material *)ecs_component_data_const(world, entity, COMPONENT_Material.id); }
int Material_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Material.id); }
int Material_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Material.id, NULL, sort_key); }
int Material_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Material.id, sort_key); }

const ecs_component_desc COMPONENT_Shader = { .id = UINT64_C(11044973552956419468), .key = "Shader", .name = "Shader", .element_size = sizeof(Shader), .type = &T_Shader, .index = ECS_INDEX_DENSE };
Shader *Shader_get(ecs_world *world, ecs_entity entity) { return (Shader *)ecs_component_data(world, entity, COMPONENT_Shader.id); }
const Shader *Shader_get_const(const ecs_world *world, ecs_entity entity) { return (const Shader *)ecs_component_data_const(world, entity, COMPONENT_Shader.id); }
int Shader_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Shader.id); }
int Shader_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Shader.id, NULL, sort_key); }
int Shader_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Shader.id, sort_key); }

const ecs_component_desc COMPONENT_Geometry3D = { .id = UINT64_C(2781276705226794261), .key = "Geometry3D", .name = "Geometry3D", .element_size = sizeof(Geometry3D), .type = &T_Geometry3D, .index = ECS_INDEX_DENSE };
Geometry3D *Geometry3D_get(ecs_world *world, ecs_entity entity) { return (Geometry3D *)ecs_component_data(world, entity, COMPONENT_Geometry3D.id); }
const Geometry3D *Geometry3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Geometry3D *)ecs_component_data_const(world, entity, COMPONENT_Geometry3D.id); }
int Geometry3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Geometry3D.id); }
int Geometry3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Geometry3D.id, NULL, sort_key); }
int Geometry3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Geometry3D.id, sort_key); }

const ecs_component_desc COMPONENT_Aim = { .id = UINT64_C(8007759512335327370), .key = "Aim", .name = "Aim", .element_size = sizeof(Aim), .type = &T_Aim, .index = ECS_INDEX_DENSE };
Aim *Aim_get(ecs_world *world, ecs_entity entity) { return (Aim *)ecs_component_data(world, entity, COMPONENT_Aim.id); }
const Aim *Aim_get_const(const ecs_world *world, ecs_entity entity) { return (const Aim *)ecs_component_data_const(world, entity, COMPONENT_Aim.id); }
int Aim_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Aim.id); }
int Aim_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Aim.id, NULL, sort_key); }
int Aim_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Aim.id, sort_key); }

const ecs_component_desc COMPONENT_Dash = { .id = UINT64_C(12256524080648897009), .key = "Dash", .name = "Dash", .element_size = sizeof(Dash), .type = &T_Dash, .index = ECS_INDEX_DENSE };
Dash *Dash_get(ecs_world *world, ecs_entity entity) { return (Dash *)ecs_component_data(world, entity, COMPONENT_Dash.id); }
const Dash *Dash_get_const(const ecs_world *world, ecs_entity entity) { return (const Dash *)ecs_component_data_const(world, entity, COMPONENT_Dash.id); }
int Dash_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Dash.id); }
int Dash_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Dash.id, NULL, sort_key); }
int Dash_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Dash.id, sort_key); }

const ecs_component_desc COMPONENT_SpawnRequest = { .id = UINT64_C(8528745355580758333), .key = "SpawnRequest", .name = "SpawnRequest", .element_size = sizeof(SpawnRequest), .type = &T_SpawnRequest, .index = ECS_INDEX_DENSE };
SpawnRequest *SpawnRequest_get(ecs_world *world, ecs_entity entity) { return (SpawnRequest *)ecs_component_data(world, entity, COMPONENT_SpawnRequest.id); }
const SpawnRequest *SpawnRequest_get_const(const ecs_world *world, ecs_entity entity) { return (const SpawnRequest *)ecs_component_data_const(world, entity, COMPONENT_SpawnRequest.id); }
int SpawnRequest_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SpawnRequest.id); }
int SpawnRequest_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SpawnRequest.id, NULL, sort_key); }
int SpawnRequest_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SpawnRequest.id, sort_key); }

const ecs_component_desc COMPONENT_Encounter = { .id = UINT64_C(9725745725351648974), .key = "Encounter", .name = "Encounter", .element_size = sizeof(Encounter), .type = &T_Encounter, .index = ECS_INDEX_DENSE };
Encounter *Encounter_get(ecs_world *world, ecs_entity entity) { return (Encounter *)ecs_component_data(world, entity, COMPONENT_Encounter.id); }
const Encounter *Encounter_get_const(const ecs_world *world, ecs_entity entity) { return (const Encounter *)ecs_component_data_const(world, entity, COMPONENT_Encounter.id); }
int Encounter_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Encounter.id); }
int Encounter_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Encounter.id, NULL, sort_key); }
int Encounter_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Encounter.id, sort_key); }

const ecs_component_desc COMPONENT_EncounterMember = { .id = UINT64_C(7233248918529655000), .key = "EncounterMember", .name = "EncounterMember", .element_size = sizeof(EncounterMember), .type = &T_EncounterMember, .index = ECS_INDEX_DENSE };
EncounterMember *EncounterMember_get(ecs_world *world, ecs_entity entity) { return (EncounterMember *)ecs_component_data(world, entity, COMPONENT_EncounterMember.id); }
const EncounterMember *EncounterMember_get_const(const ecs_world *world, ecs_entity entity) { return (const EncounterMember *)ecs_component_data_const(world, entity, COMPONENT_EncounterMember.id); }
int EncounterMember_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_EncounterMember.id); }
int EncounterMember_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_EncounterMember.id, NULL, sort_key); }
int EncounterMember_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_EncounterMember.id, sort_key); }

const ecs_component_desc COMPONENT_Environment = { .id = UINT64_C(1078910189363616798), .key = "Environment", .name = "Environment", .element_size = sizeof(Environment), .type = &T_Environment, .index = ECS_INDEX_DENSE };
Environment *Environment_get(ecs_world *world, ecs_entity entity) { return (Environment *)ecs_component_data(world, entity, COMPONENT_Environment.id); }
const Environment *Environment_get_const(const ecs_world *world, ecs_entity entity) { return (const Environment *)ecs_component_data_const(world, entity, COMPONENT_Environment.id); }
int Environment_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Environment.id); }
int Environment_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Environment.id, NULL, sort_key); }
int Environment_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Environment.id, sort_key); }

const ecs_component_desc COMPONENT_DirectionalLight = { .id = UINT64_C(736741831592902477), .key = "DirectionalLight", .name = "DirectionalLight", .element_size = sizeof(DirectionalLight), .type = &T_DirectionalLight, .index = ECS_INDEX_DENSE };
DirectionalLight *DirectionalLight_get(ecs_world *world, ecs_entity entity) { return (DirectionalLight *)ecs_component_data(world, entity, COMPONENT_DirectionalLight.id); }
const DirectionalLight *DirectionalLight_get_const(const ecs_world *world, ecs_entity entity) { return (const DirectionalLight *)ecs_component_data_const(world, entity, COMPONENT_DirectionalLight.id); }
int DirectionalLight_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_DirectionalLight.id); }
int DirectionalLight_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_DirectionalLight.id, NULL, sort_key); }
int DirectionalLight_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_DirectionalLight.id, sort_key); }

const ecs_component_desc COMPONENT_PointLight = { .id = UINT64_C(6722817392593489167), .key = "PointLight", .name = "PointLight", .element_size = sizeof(PointLight), .type = &T_PointLight, .index = ECS_INDEX_DENSE };
PointLight *PointLight_get(ecs_world *world, ecs_entity entity) { return (PointLight *)ecs_component_data(world, entity, COMPONENT_PointLight.id); }
const PointLight *PointLight_get_const(const ecs_world *world, ecs_entity entity) { return (const PointLight *)ecs_component_data_const(world, entity, COMPONENT_PointLight.id); }
int PointLight_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_PointLight.id); }
int PointLight_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_PointLight.id, NULL, sort_key); }
int PointLight_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_PointLight.id, sort_key); }

const ecs_component_desc COMPONENT_SpotLight = { .id = UINT64_C(9420143719880362251), .key = "SpotLight", .name = "SpotLight", .element_size = sizeof(SpotLight), .type = &T_SpotLight, .index = ECS_INDEX_DENSE };
SpotLight *SpotLight_get(ecs_world *world, ecs_entity entity) { return (SpotLight *)ecs_component_data(world, entity, COMPONENT_SpotLight.id); }
const SpotLight *SpotLight_get_const(const ecs_world *world, ecs_entity entity) { return (const SpotLight *)ecs_component_data_const(world, entity, COMPONENT_SpotLight.id); }
int SpotLight_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SpotLight.id); }
int SpotLight_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SpotLight.id, NULL, sort_key); }
int SpotLight_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SpotLight.id, sort_key); }

const ecs_component_desc COMPONENT_SurfaceInfluence = { .id = UINT64_C(11570609512381709039), .key = "SurfaceInfluence", .name = "SurfaceInfluence", .element_size = sizeof(SurfaceInfluence), .type = &T_SurfaceInfluence, .index = ECS_INDEX_DENSE };
SurfaceInfluence *SurfaceInfluence_get(ecs_world *world, ecs_entity entity) { return (SurfaceInfluence *)ecs_component_data(world, entity, COMPONENT_SurfaceInfluence.id); }
const SurfaceInfluence *SurfaceInfluence_get_const(const ecs_world *world, ecs_entity entity) { return (const SurfaceInfluence *)ecs_component_data_const(world, entity, COMPONENT_SurfaceInfluence.id); }
int SurfaceInfluence_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SurfaceInfluence.id); }
int SurfaceInfluence_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SurfaceInfluence.id, NULL, sort_key); }
int SurfaceInfluence_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SurfaceInfluence.id, sort_key); }

const ecs_component_desc COMPONENT_VisualTransform = { .id = UINT64_C(1343325794762461435), .key = "VisualTransform", .name = "VisualTransform", .element_size = sizeof(VisualTransform), .type = &T_VisualTransform, .index = ECS_INDEX_DENSE };
VisualTransform *VisualTransform_get(ecs_world *world, ecs_entity entity) { return (VisualTransform *)ecs_component_data(world, entity, COMPONENT_VisualTransform.id); }
const VisualTransform *VisualTransform_get_const(const ecs_world *world, ecs_entity entity) { return (const VisualTransform *)ecs_component_data_const(world, entity, COMPONENT_VisualTransform.id); }
int VisualTransform_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_VisualTransform.id); }
int VisualTransform_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_VisualTransform.id, NULL, sort_key); }
int VisualTransform_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_VisualTransform.id, sort_key); }

const ecs_component_desc COMPONENT_MeshSurface2D = { .id = UINT64_C(16004185540402233197), .key = "MeshSurface2D", .name = "MeshSurface2D", .element_size = sizeof(MeshSurface2D), .type = &T_MeshSurface2D, .index = ECS_INDEX_DENSE };
MeshSurface2D *MeshSurface2D_get(ecs_world *world, ecs_entity entity) { return (MeshSurface2D *)ecs_component_data(world, entity, COMPONENT_MeshSurface2D.id); }
const MeshSurface2D *MeshSurface2D_get_const(const ecs_world *world, ecs_entity entity) { return (const MeshSurface2D *)ecs_component_data_const(world, entity, COMPONENT_MeshSurface2D.id); }
int MeshSurface2D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MeshSurface2D.id); }
int MeshSurface2D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MeshSurface2D.id, NULL, sort_key); }
int MeshSurface2D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MeshSurface2D.id, sort_key); }

const ecs_component_desc COMPONENT_SdfSurface3D = { .id = UINT64_C(3670272291154969148), .key = "SdfSurface3D", .name = "SdfSurface3D", .element_size = sizeof(SdfSurface3D), .type = &T_SdfSurface3D, .index = ECS_INDEX_DENSE };
SdfSurface3D *SdfSurface3D_get(ecs_world *world, ecs_entity entity) { return (SdfSurface3D *)ecs_component_data(world, entity, COMPONENT_SdfSurface3D.id); }
const SdfSurface3D *SdfSurface3D_get_const(const ecs_world *world, ecs_entity entity) { return (const SdfSurface3D *)ecs_component_data_const(world, entity, COMPONENT_SdfSurface3D.id); }
int SdfSurface3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SdfSurface3D.id); }
int SdfSurface3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SdfSurface3D.id, NULL, sort_key); }
int SdfSurface3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SdfSurface3D.id, sort_key); }

const ecs_component_desc COMPONENT_SurfaceQuality = { .id = UINT64_C(1486528733283053665), .key = "SurfaceQuality", .name = "SurfaceQuality", .element_size = sizeof(SurfaceQuality), .type = &T_SurfaceQuality, .index = ECS_INDEX_DENSE };
SurfaceQuality *SurfaceQuality_get(ecs_world *world, ecs_entity entity) { return (SurfaceQuality *)ecs_component_data(world, entity, COMPONENT_SurfaceQuality.id); }
const SurfaceQuality *SurfaceQuality_get_const(const ecs_world *world, ecs_entity entity) { return (const SurfaceQuality *)ecs_component_data_const(world, entity, COMPONENT_SurfaceQuality.id); }
int SurfaceQuality_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SurfaceQuality.id); }
int SurfaceQuality_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SurfaceQuality.id, NULL, sort_key); }
int SurfaceQuality_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SurfaceQuality.id, sort_key); }

const ecs_component_desc COMPONENT_MeshAsset = { .id = UINT64_C(12791356551265717872), .key = "MeshAsset", .name = "MeshAsset", .element_size = sizeof(MeshAsset), .type = &T_MeshAsset, .index = ECS_INDEX_DENSE };
MeshAsset *MeshAsset_get(ecs_world *world, ecs_entity entity) { return (MeshAsset *)ecs_component_data(world, entity, COMPONENT_MeshAsset.id); }
const MeshAsset *MeshAsset_get_const(const ecs_world *world, ecs_entity entity) { return (const MeshAsset *)ecs_component_data_const(world, entity, COMPONENT_MeshAsset.id); }
int MeshAsset_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MeshAsset.id); }
int MeshAsset_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MeshAsset.id, NULL, sort_key); }
int MeshAsset_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MeshAsset.id, sort_key); }

const ecs_component_desc COMPONENT_MeshParts = { .id = UINT64_C(17195177857250308626), .key = "MeshParts", .name = "MeshParts", .element_size = sizeof(MeshParts), .type = &T_MeshParts, .index = ECS_INDEX_DENSE };
MeshParts *MeshParts_get(ecs_world *world, ecs_entity entity) { return (MeshParts *)ecs_component_data(world, entity, COMPONENT_MeshParts.id); }
const MeshParts *MeshParts_get_const(const ecs_world *world, ecs_entity entity) { return (const MeshParts *)ecs_component_data_const(world, entity, COMPONENT_MeshParts.id); }
int MeshParts_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_MeshParts.id); }
int MeshParts_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_MeshParts.id, NULL, sort_key); }
int MeshParts_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_MeshParts.id, sort_key); }

const ecs_component_desc COMPONENT_ParticleEmitter = { .id = UINT64_C(7582665383209926251), .key = "ParticleEmitter", .name = "ParticleEmitter", .element_size = sizeof(ParticleEmitter), .type = &T_ParticleEmitter, .index = ECS_INDEX_DENSE };
ParticleEmitter *ParticleEmitter_get(ecs_world *world, ecs_entity entity) { return (ParticleEmitter *)ecs_component_data(world, entity, COMPONENT_ParticleEmitter.id); }
const ParticleEmitter *ParticleEmitter_get_const(const ecs_world *world, ecs_entity entity) { return (const ParticleEmitter *)ecs_component_data_const(world, entity, COMPONENT_ParticleEmitter.id); }
int ParticleEmitter_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_ParticleEmitter.id); }
int ParticleEmitter_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_ParticleEmitter.id, NULL, sort_key); }
int ParticleEmitter_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_ParticleEmitter.id, sort_key); }

const ecs_component_desc COMPONENT_ParticleField3D = { .id = UINT64_C(11273845752498020950), .key = "ParticleField3D", .name = "ParticleField3D", .element_size = sizeof(ParticleField3D), .type = &T_ParticleField3D, .index = ECS_INDEX_DENSE };
ParticleField3D *ParticleField3D_get(ecs_world *world, ecs_entity entity) { return (ParticleField3D *)ecs_component_data(world, entity, COMPONENT_ParticleField3D.id); }
const ParticleField3D *ParticleField3D_get_const(const ecs_world *world, ecs_entity entity) { return (const ParticleField3D *)ecs_component_data_const(world, entity, COMPONENT_ParticleField3D.id); }
int ParticleField3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_ParticleField3D.id); }
int ParticleField3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_ParticleField3D.id, NULL, sort_key); }
int ParticleField3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_ParticleField3D.id, sort_key); }

const ecs_component_desc COMPONENT_Trail = { .id = UINT64_C(14068872884189083247), .key = "Trail", .name = "Trail", .element_size = sizeof(Trail), .type = &T_Trail, .index = ECS_INDEX_DENSE };
Trail *Trail_get(ecs_world *world, ecs_entity entity) { return (Trail *)ecs_component_data(world, entity, COMPONENT_Trail.id); }
const Trail *Trail_get_const(const ecs_world *world, ecs_entity entity) { return (const Trail *)ecs_component_data_const(world, entity, COMPONENT_Trail.id); }
int Trail_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Trail.id); }
int Trail_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Trail.id, NULL, sort_key); }
int Trail_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Trail.id, sort_key); }

const ecs_component_desc COMPONENT_Experience = { .id = UINT64_C(228238735719858517), .key = "Experience", .name = "Experience", .element_size = sizeof(Experience), .type = &T_Experience, .index = ECS_INDEX_DENSE };
Experience *Experience_get(ecs_world *world, ecs_entity entity) { return (Experience *)ecs_component_data(world, entity, COMPONENT_Experience.id); }
const Experience *Experience_get_const(const ecs_world *world, ecs_entity entity) { return (const Experience *)ecs_component_data_const(world, entity, COMPONENT_Experience.id); }
int Experience_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Experience.id); }
int Experience_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Experience.id, NULL, sort_key); }
int Experience_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Experience.id, sort_key); }

const ecs_component_desc COMPONENT_Pickup = { .id = UINT64_C(5003303095809122321), .key = "Pickup", .name = "Pickup", .element_size = sizeof(Pickup), .type = &T_Pickup, .index = ECS_INDEX_DENSE };
Pickup *Pickup_get(ecs_world *world, ecs_entity entity) { return (Pickup *)ecs_component_data(world, entity, COMPONENT_Pickup.id); }
const Pickup *Pickup_get_const(const ecs_world *world, ecs_entity entity) { return (const Pickup *)ecs_component_data_const(world, entity, COMPONENT_Pickup.id); }
int Pickup_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Pickup.id); }
int Pickup_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Pickup.id, NULL, sort_key); }
int Pickup_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Pickup.id, sort_key); }

const ecs_component_desc COMPONENT_Camera = { .id = UINT64_C(16311651766510469378), .key = "Camera", .name = "Camera", .element_size = sizeof(Camera), .type = &T_Camera, .index = ECS_INDEX_DENSE };
Camera *Camera_get(ecs_world *world, ecs_entity entity) { return (Camera *)ecs_component_data(world, entity, COMPONENT_Camera.id); }
const Camera *Camera_get_const(const ecs_world *world, ecs_entity entity) { return (const Camera *)ecs_component_data_const(world, entity, COMPONENT_Camera.id); }
int Camera_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Camera.id); }
int Camera_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Camera.id, NULL, sort_key); }
int Camera_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Camera.id, sort_key); }

const ecs_component_desc COMPONENT_Ui = { .id = UINT64_C(14674119150261194375), .key = "Ui", .name = "Ui", .element_size = sizeof(Ui), .type = &T_Ui, .index = ECS_INDEX_DENSE };
Ui *Ui_get(ecs_world *world, ecs_entity entity) { return (Ui *)ecs_component_data(world, entity, COMPONENT_Ui.id); }
const Ui *Ui_get_const(const ecs_world *world, ecs_entity entity) { return (const Ui *)ecs_component_data_const(world, entity, COMPONENT_Ui.id); }
int Ui_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Ui.id); }
int Ui_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Ui.id, NULL, sort_key); }
int Ui_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Ui.id, sort_key); }

const ecs_component_desc COMPONENT_Fog = { .id = UINT64_C(12434293270790912419), .key = "Fog", .name = "Fog", .element_size = sizeof(Fog), .type = &T_Fog, .index = ECS_INDEX_DENSE };
Fog *Fog_get(ecs_world *world, ecs_entity entity) { return (Fog *)ecs_component_data(world, entity, COMPONENT_Fog.id); }
const Fog *Fog_get_const(const ecs_world *world, ecs_entity entity) { return (const Fog *)ecs_component_data_const(world, entity, COMPONENT_Fog.id); }
int Fog_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Fog.id); }
int Fog_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Fog.id, NULL, sort_key); }
int Fog_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Fog.id, sort_key); }

const ecs_component_desc COMPONENT_Vision = { .id = UINT64_C(12259222880172700181), .key = "Vision", .name = "Vision", .element_size = sizeof(Vision), .type = &T_Vision, .index = ECS_INDEX_DENSE };
Vision *Vision_get(ecs_world *world, ecs_entity entity) { return (Vision *)ecs_component_data(world, entity, COMPONENT_Vision.id); }
const Vision *Vision_get_const(const ecs_world *world, ecs_entity entity) { return (const Vision *)ecs_component_data_const(world, entity, COMPONENT_Vision.id); }
int Vision_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Vision.id); }
int Vision_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Vision.id, NULL, sort_key); }
int Vision_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Vision.id, sort_key); }

const ecs_component_desc COMPONENT_SensorContact = { .id = UINT64_C(17998717769560533633), .key = "SensorContact", .name = "SensorContact", .element_size = sizeof(SensorContact), .type = &T_SensorContact, .index = ECS_INDEX_DENSE };
SensorContact *SensorContact_get(ecs_world *world, ecs_entity entity) { return (SensorContact *)ecs_component_data(world, entity, COMPONENT_SensorContact.id); }
const SensorContact *SensorContact_get_const(const ecs_world *world, ecs_entity entity) { return (const SensorContact *)ecs_component_data_const(world, entity, COMPONENT_SensorContact.id); }
int SensorContact_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_SensorContact.id); }
int SensorContact_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_SensorContact.id, NULL, sort_key); }
int SensorContact_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_SensorContact.id, sort_key); }

const ecs_component_desc COMPONENT_RenderState = { .id = UINT64_C(17062285604489919970), .key = "RenderState", .name = "RenderState", .element_size = sizeof(RenderState), .type = &T_RenderState, .index = ECS_INDEX_DENSE };
RenderState *RenderState_get(ecs_world *world, ecs_entity entity) { return (RenderState *)ecs_component_data(world, entity, COMPONENT_RenderState.id); }
const RenderState *RenderState_get_const(const ecs_world *world, ecs_entity entity) { return (const RenderState *)ecs_component_data_const(world, entity, COMPONENT_RenderState.id); }
int RenderState_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_RenderState.id); }
int RenderState_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_RenderState.id, NULL, sort_key); }
int RenderState_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_RenderState.id, sort_key); }

const ecs_component_desc COMPONENT_Starfield2D = { .id = UINT64_C(10343616148237872441), .key = "Starfield2D", .name = "Starfield2D", .element_size = sizeof(Starfield2D), .type = &T_Starfield2D, .index = ECS_INDEX_DENSE };
Starfield2D *Starfield2D_get(ecs_world *world, ecs_entity entity) { return (Starfield2D *)ecs_component_data(world, entity, COMPONENT_Starfield2D.id); }
const Starfield2D *Starfield2D_get_const(const ecs_world *world, ecs_entity entity) { return (const Starfield2D *)ecs_component_data_const(world, entity, COMPONENT_Starfield2D.id); }
int Starfield2D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Starfield2D.id); }
int Starfield2D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Starfield2D.id, NULL, sort_key); }
int Starfield2D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Starfield2D.id, sort_key); }

const ecs_component_desc COMPONENT_HRC2D = { .id = UINT64_C(17049391141128102915), .key = "HRC2D", .name = "HRC2D", .element_size = sizeof(HRC2D), .type = &T_HRC2D, .index = ECS_INDEX_DENSE };
HRC2D *HRC2D_get(ecs_world *world, ecs_entity entity) { return (HRC2D *)ecs_component_data(world, entity, COMPONENT_HRC2D.id); }
const HRC2D *HRC2D_get_const(const ecs_world *world, ecs_entity entity) { return (const HRC2D *)ecs_component_data_const(world, entity, COMPONENT_HRC2D.id); }
int HRC2D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_HRC2D.id); }
int HRC2D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_HRC2D.id, NULL, sort_key); }
int HRC2D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_HRC2D.id, sort_key); }

const ecs_component_desc COMPONENT_Transform3D = { .id = UINT64_C(15401360202626458448), .key = "Transform3D", .name = "Transform3D", .element_size = sizeof(Transform3D), .type = &T_Transform3D, .index = ECS_INDEX_DENSE };
Transform3D *Transform3D_get(ecs_world *world, ecs_entity entity) { return (Transform3D *)ecs_component_data(world, entity, COMPONENT_Transform3D.id); }
const Transform3D *Transform3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Transform3D *)ecs_component_data_const(world, entity, COMPONENT_Transform3D.id); }
int Transform3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Transform3D.id); }
int Transform3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Transform3D.id, NULL, sort_key); }
int Transform3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Transform3D.id, sort_key); }

const ecs_component_desc COMPONENT_GlobalTransform3D = { .id = UINT64_C(15480464602149853421), .key = "GlobalTransform3D", .name = "GlobalTransform3D", .element_size = sizeof(GlobalTransform3D), .type = &T_GlobalTransform3D, .index = ECS_INDEX_DENSE, .derived_from = UINT64_C(15401360202626458448) };
GlobalTransform3D *GlobalTransform3D_get(ecs_world *world, ecs_entity entity) { return (GlobalTransform3D *)ecs_component_data(world, entity, COMPONENT_GlobalTransform3D.id); }
const GlobalTransform3D *GlobalTransform3D_get_const(const ecs_world *world, ecs_entity entity) { return (const GlobalTransform3D *)ecs_component_data_const(world, entity, COMPONENT_GlobalTransform3D.id); }
int GlobalTransform3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_GlobalTransform3D.id); }
int GlobalTransform3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_GlobalTransform3D.id, NULL, sort_key); }
int GlobalTransform3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_GlobalTransform3D.id, sort_key); }

const ecs_component_desc COMPONENT_GlobalTransform2D = { .id = UINT64_C(15479367289545088068), .key = "GlobalTransform2D", .name = "GlobalTransform2D", .element_size = sizeof(GlobalTransform2D), .type = &T_GlobalTransform2D, .index = ECS_INDEX_DENSE, .derived_from = UINT64_C(1376052968821171689) };
GlobalTransform2D *GlobalTransform2D_get(ecs_world *world, ecs_entity entity) { return (GlobalTransform2D *)ecs_component_data(world, entity, COMPONENT_GlobalTransform2D.id); }
const GlobalTransform2D *GlobalTransform2D_get_const(const ecs_world *world, ecs_entity entity) { return (const GlobalTransform2D *)ecs_component_data_const(world, entity, COMPONENT_GlobalTransform2D.id); }
int GlobalTransform2D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_GlobalTransform2D.id); }
int GlobalTransform2D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_GlobalTransform2D.id, NULL, sort_key); }
int GlobalTransform2D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_GlobalTransform2D.id, sort_key); }

const ecs_component_desc COMPONENT_Collider3D = { .id = UINT64_C(18119630680118210702), .key = "Collider3D", .name = "Collider3D", .element_size = sizeof(Collider3D), .type = &T_Collider3D, .index = ECS_INDEX_DENSE };
Collider3D *Collider3D_get(ecs_world *world, ecs_entity entity) { return (Collider3D *)ecs_component_data(world, entity, COMPONENT_Collider3D.id); }
const Collider3D *Collider3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Collider3D *)ecs_component_data_const(world, entity, COMPONENT_Collider3D.id); }
int Collider3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Collider3D.id); }
int Collider3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Collider3D.id, NULL, sort_key); }
int Collider3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Collider3D.id, sort_key); }

const ecs_component_desc COMPONENT_Body3D = { .id = UINT64_C(15570172463250858062), .key = "Body3D", .name = "Body3D", .element_size = sizeof(Body3D), .type = &T_Body3D, .index = ECS_INDEX_DENSE };
Body3D *Body3D_get(ecs_world *world, ecs_entity entity) { return (Body3D *)ecs_component_data(world, entity, COMPONENT_Body3D.id); }
const Body3D *Body3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Body3D *)ecs_component_data_const(world, entity, COMPONENT_Body3D.id); }
int Body3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Body3D.id); }
int Body3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Body3D.id, NULL, sort_key); }
int Body3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Body3D.id, sort_key); }

const ecs_component_desc COMPONENT_Gravity3D = { .id = UINT64_C(12579094281528875694), .key = "Gravity3D", .name = "Gravity3D", .element_size = sizeof(Gravity3D), .type = &T_Gravity3D, .index = ECS_INDEX_DENSE };
Gravity3D *Gravity3D_get(ecs_world *world, ecs_entity entity) { return (Gravity3D *)ecs_component_data(world, entity, COMPONENT_Gravity3D.id); }
const Gravity3D *Gravity3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Gravity3D *)ecs_component_data_const(world, entity, COMPONENT_Gravity3D.id); }
int Gravity3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Gravity3D.id); }
int Gravity3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Gravity3D.id, NULL, sort_key); }
int Gravity3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Gravity3D.id, sort_key); }

const ecs_component_desc COMPONENT_Character3D = { .id = UINT64_C(15250030159519388059), .key = "Character3D", .name = "Character3D", .element_size = sizeof(Character3D), .type = &T_Character3D, .index = ECS_INDEX_DENSE };
Character3D *Character3D_get(ecs_world *world, ecs_entity entity) { return (Character3D *)ecs_component_data(world, entity, COMPONENT_Character3D.id); }
const Character3D *Character3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Character3D *)ecs_component_data_const(world, entity, COMPONENT_Character3D.id); }
int Character3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Character3D.id); }
int Character3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Character3D.id, NULL, sort_key); }
int Character3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Character3D.id, sort_key); }

const ecs_component_desc COMPONENT_Joint3D = { .id = UINT64_C(11354339754808048980), .key = "Joint3D", .name = "Joint3D", .element_size = sizeof(Joint3D), .type = &T_Joint3D, .index = ECS_INDEX_DENSE };
Joint3D *Joint3D_get(ecs_world *world, ecs_entity entity) { return (Joint3D *)ecs_component_data(world, entity, COMPONENT_Joint3D.id); }
const Joint3D *Joint3D_get_const(const ecs_world *world, ecs_entity entity) { return (const Joint3D *)ecs_component_data_const(world, entity, COMPONENT_Joint3D.id); }
int Joint3D_has(const ecs_world *world, ecs_entity entity) { return ecs_component_has(world, entity, COMPONENT_Joint3D.id); }
int Joint3D_add(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_add(world, entity, COMPONENT_Joint3D.id, NULL, sort_key); }
int Joint3D_remove(ecs_world *world, ecs_entity entity, uint64_t sort_key) { return ecs_world_record_remove(world, entity, COMPONENT_Joint3D.id, sort_key); }

const ecs_component_desc *const REGISTRY_COMPONENTS[] = {
    &COMPONENT_Transform,
    &COMPONENT_Velocity,
    &COMPONENT_GlobalBinding,
    &COMPONENT_Body,
    &COMPONENT_Gravity,
    &COMPONENT_Collider,
    &COMPONENT_Drive,
    &COMPONENT_Joint,
    &COMPONENT_EngineSystem,
    &COMPONENT_MaterialFluidWorld,
    &COMPONENT_MaterialDestructionWorld,
    &COMPONENT_MaterialBreakProfile,
    &COMPONENT_Combustion,
    &COMPONENT_MaterialThermal,
    &COMPONENT_MaterialConstitutive,
    &COMPONENT_Fuel,
    &COMPONENT_Igniter,
    &COMPONENT_Substance,
    &COMPONENT_MaterialCavity,
    &COMPONENT_Reflection,
    &COMPONENT_Destructible,
    &COMPONENT_FragmentGlow,
    &COMPONENT_MaterialFrame,
    &COMPONENT_MaterialSurface3D,
    &COMPONENT_TerrainVolume,
    &COMPONENT_IndirectOccluder,
    &COMPONENT_MaterialTerrain,
    &COMPONENT_FluidVolume,
    &COMPONENT_Health,
    &COMPONENT_Movement,
    &COMPONENT_Input,
    &COMPONENT_Weapon,
    &COMPONENT_Projectile,
    &COMPONENT_Appearance,
    &COMPONENT_Sprite,
    &COMPONENT_Tilemap,
    &COMPONENT_Orientation,
    &COMPONENT_Material,
    &COMPONENT_Shader,
    &COMPONENT_Geometry3D,
    &COMPONENT_Aim,
    &COMPONENT_Dash,
    &COMPONENT_SpawnRequest,
    &COMPONENT_Encounter,
    &COMPONENT_EncounterMember,
    &COMPONENT_Environment,
    &COMPONENT_DirectionalLight,
    &COMPONENT_PointLight,
    &COMPONENT_SpotLight,
    &COMPONENT_SurfaceInfluence,
    &COMPONENT_VisualTransform,
    &COMPONENT_MeshSurface2D,
    &COMPONENT_SdfSurface3D,
    &COMPONENT_SurfaceQuality,
    &COMPONENT_MeshAsset,
    &COMPONENT_MeshParts,
    &COMPONENT_ParticleEmitter,
    &COMPONENT_ParticleField3D,
    &COMPONENT_Trail,
    &COMPONENT_Experience,
    &COMPONENT_Pickup,
    &COMPONENT_Camera,
    &COMPONENT_Ui,
    &COMPONENT_Fog,
    &COMPONENT_Vision,
    &COMPONENT_SensorContact,
    &COMPONENT_RenderState,
    &COMPONENT_Starfield2D,
    &COMPONENT_HRC2D,
    &COMPONENT_Transform3D,
    &COMPONENT_GlobalTransform3D,
    &COMPONENT_GlobalTransform2D,
    &COMPONENT_Collider3D,
    &COMPONENT_Body3D,
    &COMPONENT_Gravity3D,
    &COMPONENT_Character3D,
    &COMPONENT_Joint3D,
};
const int REGISTRY_COMPONENT_COUNT = 77;

const service_desc *const REGISTRY_SERVICES[] = {
};
const int REGISTRY_SERVICE_COUNT = 0;

const type_catalog_pack TYPE_PACK = { REGISTRY_TYPES, 83, REGISTRY_ENUMS, 26 };

