#ifndef CHIBI_COMPONENT_PRESENTATION_H
#define CHIBI_COMPONENT_PRESENTATION_H

#include "ecs/component_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How editors present reflected components. The engine presents its own
 * components (engine_components.kdl, and the units and ranges its fields
 * reflect); a content root's document overrides that key by key. Any other
 * component shows under Object without an icon, and its numbers are unbounded
 * and unitless. */
#define COMPONENT_PRESENTATION_PATH "editor/Components.kdl"

typedef enum
{
    COMPONENT_TAB_OBJECT, /* the default */
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

/* The engine's own document, in editor/Components.kdl's format: the tabs and
 * icons of its components. Owned by the caller. */
char *component_presentation_engine_text();

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

/* How an editor shows a numeric field. */
typedef struct
{
    double min, max;  /* equal when unbounded */
    const char *unit; /* NULL without one */
} field_hint;

/* What a document lists. Empty (zero) presents the engine's defaults. */
typedef struct
{
    presented_component *components;
    presented_field *fields;
} component_presentation;

/* Parses editor/Components.kdl, naming components as prototype documents do,
 * into `out`, which is empty or holds an earlier read. A listed component or
 * field keeps the engine's value for each key the document leaves out; a
 * document replaces an engine range or unit but cannot remove one. Returns
 * false with a message in `error` and leaves `out` unchanged when the document
 * is invalid, so a caller keeps the last valid presentation. */
bool component_presentation_read(
    const char *text,
    ecs_component_catalog registry,
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
/* A numeric field's range and unit: the document's where it lists the field,
 * else the reflected field's. `unit` is borrowed from either. */
field_hint component_presentation_field(
    const component_presentation *presentation,
    const ecs_component_desc *desc,
    int field
);

#endif
