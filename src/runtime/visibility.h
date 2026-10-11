#ifndef CHIBI_VISIBILITY_H
#define CHIBI_VISIBILITY_H

#include "execution/host.h"
#include "runtime/behaviors/entities.h"
#include "runtime/components.h"

typedef struct visibility visibility;
typedef struct
{
    bool visible, explored, obstacle;
} visibility_sample;

visibility *visibility_new(ecs_world *world, entities *entities);
void visibility_free(visibility *visibility);
void visibility_reset(visibility *visibility);
const char *visibility_update(visibility *visibility, float seconds);
visibility_sample visibility_at(const visibility *visibility, CF_V2 point);
/* Borrowed row-major cells: R=current sight, G=explored, B=remembered terrain,
 * A=255. No actor positions are stored in map memory. NULL means fog is off. */
const unsigned char *visibility_cells(const visibility *visibility, Fog *settings);

const char *visibility_sample_values(void *, const pin_value *, pin_value *);
extern const node_host_call VISIBILITY_SAMPLE_CALL;
extern const node_access VISIBILITY_READ_ACCESS;

#endif
