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
    scene *scene;
} prototype_record;
struct prototypes
{
    prototype_record *records;
};
static int index_of(const prototypes *prototypes, const char *name)
{
    for (int i = 0; i < acount(prototypes->records); i++) {
        if (!strcmp(prototypes->records[i].name, name)) {
            return i;
        }
    }
    return -1;
}
prototypes *prototypes_new()
{
    return xcalloc(1, sizeof(prototypes));
}
void prototypes_free(prototypes *prototypes)
{
    if (!prototypes) {
        return;
    }
    for (int i = 0; i < acount(prototypes->records); i++) {
        free(prototypes->records[i].name);
        scene_free(prototypes->records[i].scene);
    }
    afree(prototypes->records);
    free(prototypes);
}
int prototypes_register(prototypes *prototypes, const char *name, scene *scene, char *error, size_t size)
{
    assert(prototypes && scene && (!size || error));
    if (!name || !name[0] || !scene_is_tree(scene)) {
        if (size) {
            snprintf(error, size, "prepared prototype requires a name and one root object");
        }
        return -1;
    }
    int index = index_of(prototypes, name);
    scene_retain(scene);
    if (index >= 0) {
        scene_free(prototypes->records[index].scene);
        prototypes->records[index].scene = scene;
        return 0;
    }
    apush(prototypes->records, ((prototype_record){ xstrdup(name), scene }));
    return 0;
}
const scene *prototypes_find(const prototypes *prototypes, const char *name)
{
    assert(prototypes && name);
    int index = index_of(prototypes, name);
    return index < 0 ? NULL : prototypes->records[index].scene;
}
int prototypes_count(const prototypes *prototypes)
{
    return acount(prototypes->records);
}
const char *prototypes_name(const prototypes *prototypes, int index)
{
    assert(index >= 0 && index < acount(prototypes->records));
    return prototypes->records[index].name;
}
