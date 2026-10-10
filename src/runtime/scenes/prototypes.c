#include "runtime/scenes/prototypes.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    game_scene *scene;
} prototype_record;
struct game_prototypes
{
    prototype_record *records;
};
static int index_of(const game_prototypes *prototypes, const char *name)
{
    for (int i = 0; i < acount(prototypes->records); i++) {
        if (!strcmp(prototypes->records[i].name, name)) {
            return i;
        }
    }
    return -1;
}
game_prototypes *game_prototypes_new()
{
    return xcalloc(1, sizeof(game_prototypes));
}
void game_prototypes_free(game_prototypes *prototypes)
{
    if (!prototypes) {
        return;
    }
    for (int i = 0; i < acount(prototypes->records); i++) {
        free(prototypes->records[i].name);
        game_scene_free(prototypes->records[i].scene);
    }
    afree(prototypes->records);
    free(prototypes);
}
int game_prototypes_register(
    game_prototypes *prototypes,
    const char *name,
    game_scene *scene,
    char *error,
    size_t size
)
{
    assert(prototypes && scene && (!size || error));
    if (!name || !name[0] || !game_scene_is_tree(scene)) {
        if (size) {
            snprintf(error, size, "prepared prototype requires a name and one root object");
        }
        return -1;
    }
    int index = index_of(prototypes, name);
    game_scene_retain(scene);
    if (index >= 0) {
        game_scene_free(prototypes->records[index].scene);
        prototypes->records[index].scene = scene;
        return 0;
    }
    apush(prototypes->records, ((prototype_record){ xstrdup(name), scene }));
    return 0;
}
int game_prototypes_remove(game_prototypes *prototypes, const char *name)
{
    assert(prototypes && name);
    int index = index_of(prototypes, name);
    if (index < 0) {
        return -1;
    }
    free(prototypes->records[index].name);
    game_scene_free(prototypes->records[index].scene);
    adel(prototypes->records, index);
    return 0;
}
const game_scene *game_prototypes_find(const game_prototypes *prototypes, const char *name)
{
    assert(prototypes && name);
    int index = index_of(prototypes, name);
    return index < 0 ? NULL : prototypes->records[index].scene;
}
int game_prototypes_count(const game_prototypes *prototypes)
{
    return acount(prototypes->records);
}
const char *game_prototypes_name(const game_prototypes *prototypes, int index)
{
    assert(index >= 0 && index < acount(prototypes->records));
    return prototypes->records[index].name;
}
