#ifndef CHIBI_PARTICLES_DEFINITION_H
#define CHIBI_PARTICLES_DEFINITION_H
#include "runtime/native_definitions.h"
#include "runtime/particles.h"

#define PARTICLES_BURST_NAME "Emit Particles"
#define PARTICLES_DIRECTED_NAME "Emit Directed Particles"
#define PARTICLES_EXEC(X, NAME)                                                \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_EXEC,                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_FLOW }),                                       \
      0)
#define PARTICLES_DATA(X, NAME, STORAGE, TYPE_NAME, VALUE, FLAGS)              \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      VALUE,                                                                   \
      FLAGS)
#define PARTICLES_SCALAR(X, NAME, STORAGE, TYPE_NAME, FIELD, VALUE, FLAGS)     \
    PARTICLES_DATA(X, NAME, STORAGE, TYPE_NAME, ((pin_value){ .type = STORAGE, .FIELD = VALUE }), FLAGS)
#define PARTICLES_FLOAT(X, NAME, VALUE)                                        \
    PARTICLES_SCALAR(X, NAME, PIN_F32, "number", f, VALUE, PIN_FLAG_ADVANCED)
#define PARTICLES_VECTOR(X, NAME)                                              \
    PARTICLES_DATA(X, NAME, PIN_STRUCT, "vec2", ((pin_value){ .type = PIN_STRUCT }), 0)
#define PARTICLES_BURST_INPUTS(X)                                                         \
    PARTICLES_EXEC(X, "Exec")                                                             \
    PARTICLES_VECTOR(X, "Position")                                                       \
    PARTICLES_SCALAR(X, "Color", PIN_U32, "unsigned int", u, 0xF0CD69FFu, PIN_FLAG_COLOR) \
    PARTICLES_SCALAR(X, "Count", PIN_I32, "int", i, 12, 0)                                \
    PARTICLES_SCALAR(X, "Speed", PIN_F32, "number", f, 80.0f, 0)                          \
    PARTICLES_SCALAR(X, "Lifetime", PIN_F32, "number", f, 0.3f, 0)
#define PARTICLES_DIRECTED_INPUTS(X)                                                                              \
    PARTICLES_EXEC(X, "Exec")                                                                                     \
    PARTICLES_VECTOR(X, "Position")                                                                               \
    PARTICLES_VECTOR(X, "Direction")                                                                              \
    PARTICLES_SCALAR(X, "Count", PIN_I32, "int", i, 1, 0)                                                         \
    PARTICLES_SCALAR(X, "Color", PIN_U32, "unsigned int", u, 0xFF991AFFu, PIN_FLAG_ADVANCED | PIN_FLAG_COLOR)     \
    PARTICLES_SCALAR(X, "End Color", PIN_U32, "unsigned int", u, 0x4D00FF80u, PIN_FLAG_ADVANCED | PIN_FLAG_COLOR) \
    PARTICLES_FLOAT(X, "Min Speed", 30)                                                                           \
    PARTICLES_FLOAT(X, "Max Speed", 60)                                                                           \
    PARTICLES_FLOAT(X, "Min Lifetime", 0.2f)                                                                      \
    PARTICLES_FLOAT(X, "Max Lifetime", 0.4f)                                                                      \
    PARTICLES_FLOAT(X, "Min Size", 2)                                                                             \
    PARTICLES_FLOAT(X, "Max Size", 4)                                                                             \
    PARTICLES_FLOAT(X, "Spread", 1.571f)                                                                          \
    PARTICLES_FLOAT(X, "Min Drag", 4)                                                                             \
    PARTICLES_FLOAT(X, "Max Drag", 8)                                                                             \
    PARTICLES_FLOAT(X, "Min Emission", 0.2f)                                                                      \
    PARTICLES_FLOAT(X, "Max Emission", 0.4f)                                                                      \
    PARTICLES_FLOAT(X, "Size Exponent", 1.5f)                                                                     \
    PARTICLES_FLOAT(X, "Elevation", 0)                                                                            \
    PARTICLES_SCALAR(X, "Shape", PIN_I32, "int", i, PARTICLE_ROUND, PIN_FLAG_ADVANCED)
#define PARTICLES_OUTPUTS(X) PARTICLES_EXEC(X, "Then")
extern const native_definition_pack PARTICLES_DEFINITION_PACK;

#define PARTICLES_BURST_PRESENTATION                                           \
    .category = "Effects", .keywords = "particles burst sparks impact",        \
    .description = "Bursts Count streaks from Position in random directions "  \
                   "at "                                                       \
                   "up to Speed, each lasting Lifetime seconds."
#define PARTICLES_DIRECTED_PRESENTATION                                        \
    .category = "Effects", .keywords = "particles spray trail cone",           \
    .description = "Sprays Count particles from Position along Direction "     \
                   "within a Spread-radian cone, sampling each Min/Max range."

#endif
