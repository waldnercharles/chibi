#ifndef CHIBI_EDITOR_COMPONENT_PRESENTATION_H
#define CHIBI_EDITOR_COMPONENT_PRESENTATION_H

#include "assets/prototype.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How editors present reflected components, authored per content root. A
 * component the document omits shows under Object without an icon, and its
 * numbers are unbounded and unitless. */
#define COMPONENT_PRESENTATION_PATH "editor/Components.kdl"

typedef enum
{
    COMPONENT_TAB_OBJECT, /* the default; documents name the others */
    COMPONENT_TAB_PHYSICS,
    COMPONENT_TAB_MATERIAL,
    COMPONENT_TAB_GAMEPLAY,
    COMPONENT_TAB_COUNT,
} component_tab;

/* In priority order: a prototype shows the first icon one of its components
 * names. */
typedef enum
{
    COMPONENT_ICON_NONE = -1,
    COMPONENT_ICON_CAMERA,
    COMPONENT_ICON_SYSTEM,
    COMPONENT_ICON_LIGHT,
    COMPONENT_ICON_EMITTER,
    COMPONENT_ICON_WATER,
    COMPONENT_ICON_TERRAIN,
    COMPONENT_ICON_GAMEPAD,
    COMPONENT_ICON_KEYBOARD,
    COMPONENT_ICON_PROJECTILE,
    COMPONENT_ICON_HEALTH,
    COMPONENT_ICON_DIAMOND,
    COMPONENT_ICON_SHAPE,
    COMPONENT_ICON_COUNT,
} component_icon;

extern const char *const COMPONENT_TAB_NAMES[COMPONENT_TAB_COUNT];

typedef struct
{
    uint64_t id;
    component_tab tab;
    component_icon icon;
} presented_component;

typedef struct
{
    uint64_t component;
    int field;       /* index into the component's reflected fields */
    double min, max; /* equal when unbounded */
    char *unit;      /* NULL without one */
} presented_field;

/* Empty (zero) presents every component generically. */
typedef struct
{
    presented_component *components;
    presented_field *fields;
} component_presentation;

/* Parses editor/Components.kdl, naming components as prototype documents do,
 * into `out`, which is empty or holds an earlier read. Returns false with a
 * message in `error` and leaves `out` unchanged when the document is invalid,
 * so a caller keeps the last valid presentation. */
bool component_presentation_read(
    const char *text,
    const game_prototype_registry *registry,
    component_presentation *out,
    char *error,
    size_t error_size
);
void component_presentation_free(component_presentation *presentation);
component_tab component_presentation_tab(
    const component_presentation *presentation,
    const ecs_component_desc *desc
);
component_icon component_presentation_icon(
    const component_presentation *presentation,
    const ecs_component_desc *desc
);
/* NULL when the field is presented generically. */
const presented_field *component_presentation_field(
    const component_presentation *presentation,
    const ecs_component_desc *desc,
    int field
);

#endif
