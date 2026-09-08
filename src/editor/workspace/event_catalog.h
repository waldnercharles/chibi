#ifndef CBLUEPRINT_APP_EVENT_CATALOG_H
#define CBLUEPRINT_APP_EVENT_CATALOG_H

#include "editor/workspace/editor.h"

static const editor_event_param APP_TICK_PARAMS[] = {
    { "DeltaSeconds", PIN_F32, "float" },
};
static const editor_event_param APP_CONTACT_PARAMS[] = {
    { "Other", PIN_HANDLE, "ecs.entity" },
};
static const editor_event_param APP_DAMAGE_PARAMS[] = {
    { "Amount", PIN_I32, "int" },
    { "Source", PIN_HANDLE, "ecs.entity" },
};
static const editor_event_signature APP_EVENT_CATALOG[] = {
    { "Fire", NULL, 0 },
    { "Tick", APP_TICK_PARAMS, 1 },
    { "Contact Begin", APP_CONTACT_PARAMS, 1 },
    { "Damage", APP_DAMAGE_PARAMS, 2 },
};

#endif
