#include "authoring/workspace_saved.h"

#include <stdio.h>
#include <string.h>
static const char *own(arena *a, const char *s)
{
    char *copy = arena_alloc(a, strlen(s) + 1);
    strcpy(copy, s);
    return copy;
}
static bool refuse(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
bool workspace_saved_query(
    const save *store,
    int first,
    int count,
    arena *a,
    workspace_saved_row **rows,
    int *total,
    int *returned,
    uint64_t *revision,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *total = *returned = 0;
    *revision = 0;
    if (!store) {
        return refuse(error, size, "The runtime session has no saved data");
    }
    if (first < 0 || count < 1 || count > 256) {
        return refuse(error, size, "Saved data pages require First>=0 and Count=1..256");
    }
    *total = save_count(store);
    *revision = save_revision(store);
    int n = first < *total ? *total - first : 0;
    if (n > count) {
        n = count;
    }
    if (n) {
        *rows = arena_alloc(a, (size_t)n * sizeof **rows);
    }
    for (int i = 0; i < n; i++) {
        const char *key = save_key(store, first + i);
        (*rows)[i] = (workspace_saved_row){ own(a, key), own(a, save_get(store, key)) };
    }
    *returned = n;
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool workspace_saved_write(
    save *store,
    uint64_t revision,
    const char *key,
    const char *value,
    char *error,
    size_t size
)
{
    if (!store) {
        return refuse(error, size, "The runtime session cannot edit saved data");
    }
    if (save_revision(store) != revision) {
        return refuse(error, size, "Saved data changed; refresh before editing");
    }
    if (!key || !value || !save_get(store, key)) {
        return refuse(error, size, "The saved key no longer exists");
    }
    if (!save_set(store, key, value)) {
        return refuse(error, size, "Could not edit the saved value");
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}
