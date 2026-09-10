#include "reflect_internal.h"

#include <stdlib.h>
#include <string.h>

uint64_t stable_service_id(const char *name)
{
    uint64_t id = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        id = (id ^ *p) * UINT64_C(1099511628211);
    }
    return id ? id : 1;
}

typedef struct
{
    char *file;
    unsigned line;
    uint64_t id;
    char *key;
    char *name;
    uint64_t fingerprint;
    char *aliases;
} component_identity;

static component_identity *g_component_identities;
static const char *g_manifest_path;

static uint64_t declaration_fingerprint(const struct_info *s)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_bytes(hash, &s->size, sizeof s->size);
    for (int i = 0; i < acount(s->fields); i++) {
        const field_info *field = &s->fields[i];
        hash = hash_string(hash, field->name);
        hash = hash_string(hash, field->spelling);
        if (field->handle) {
            hash = hash_string(hash, field->handle);
        }
        hash = hash_bytes(hash, &field->offset, sizeof field->offset);
        hash = hash_bytes(hash, &field->size, sizeof field->size);
    }
    return hash ? hash : 1;
}

static int alias_has(const char *aliases, const char *name)
{
    if (!aliases) {
        return 0;
    }
    size_t length = strlen(name);
    const char *at = aliases;
    while (*at) {
        if (strncmp(at, name, length) == 0 &&
            (at[length] == '\0' || at[length] == '|')) {
            return 1;
        }
        at = strchr(at, '|');
        if (!at) {
            break;
        }
        at++;
    }
    return 0;
}

static void add_alias(component_identity *identity, const char *name)
{
    if (!name || !name[0] || alias_has(identity->aliases, name)) {
        return;
    }
    size_t old_size = identity->aliases ? strlen(identity->aliases) : 0;
    size_t name_size = strlen(name);
    char *aliases = (char *)
        realloc(identity->aliases, old_size + name_size + (old_size ? 2 : 1));
    if (!aliases) {
        fprintf(stderr, "reflect: out of memory recording component alias\n");
        g_failed = 1;
        return;
    }
    if (old_size) {
        aliases[old_size++] = '|';
    }
    memcpy(aliases + old_size, name, name_size + 1);
    identity->aliases = aliases;
}

static int identity_declared(const component_identity *identity)
{
    for (int i = 0; i < acount(g_collection->structs); i++) {
        const struct_info *s = &g_collection->structs[i];
        if (s->component && (strcmp(identity->name, s->name) == 0 ||
                             alias_has(identity->aliases, s->name))) {
            return 1;
        }
    }
    return 0;
}

static int identity_rank(const component_identity *identity, const struct_info *s)
{
    if (strcmp(identity->name, s->name) == 0) {
        return 4;
    }
    if (alias_has(identity->aliases, s->name)) {
        return 3;
    }
    /* Layout and location only infer renames after the old declaration is gone.
     * An inserted component may occupy its line or share its field layout. */
    if (identity_declared(identity)) {
        return 0;
    }
    if (identity->fingerprint && identity->fingerprint == declaration_fingerprint(s)) {
        return 2;
    }
    return identity->line == s->line && strcmp(identity->file, s->file) == 0;
}

static int manifest_match(const struct_info *s)
{
    int best = 0;
    for (int i = 0; i < acount(g_component_identities); i++) {
        int rank = identity_rank(&g_component_identities[i], s);
        if (rank > best) {
            best = rank;
        }
    }
    if (!best) {
        return -1;
    }
    int found = -1;
    for (int i = 0; i < acount(g_component_identities); i++) {
        component_identity *identity = &g_component_identities[i];
        if (identity_rank(identity, s) != best) {
            continue;
        }
        if (found >= 0 && g_component_identities[found].id != identity->id) {
            fprintf(
                stderr,
                "reflect: ambiguous component identity for %s (manifest "
                "entries %s and %s)\n",
                s->name,
                g_component_identities[found].name,
                identity->name
            );
            g_failed = 1;
            return -1;
        }
        found = i;
    }
    return found;
}

uint64_t component_id(const struct_info *s)
{
    int index = manifest_match(s);
    if (index >= 0) {
        return g_component_identities[index].id;
    }
    return stable_service_id(component_key(s));
}

const char *component_key(const struct_info *s)
{
    int index = manifest_match(s);
    if (index >= 0) {
        return g_component_identities[index].key;
    }
    static char key[256];
    snprintf(key, sizeof key, "%s.%s", g_prefix, s->name);
    return key;
}

void load_component_manifest(const char *path)
{
    g_manifest_path = path;
    if (!path) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        char file_name[256];
        char key[128];
        char name[128];
        char fingerprint[64] = "0";
        char aliases[256] = "";
        unsigned line_number;
        unsigned long long id;
        int count = sscanf(
            line,
            "%255s %u %llu %127s %127s %63s %255s",
            file_name,
            &line_number,
            &id,
            key,
            name,
            fingerprint,
            aliases
        );
        if (count < 5 || !id || (count >= 6 && !strtoull(fingerprint, NULL, 10))) {
            fprintf(stderr, "reflect: invalid component manifest line: %s", line);
            g_failed = 1;
            continue;
        }
        apush(
            g_component_identities,
            (component_identity){
                strdup(file_name),
                line_number,
                (uint64_t)id,
                strdup(key),
                strdup(name),
                count >= 6 ? (uint64_t)strtoull(fingerprint, NULL, 10) : 0,
                count >= 7 ? strdup(aliases) : NULL,
            }
        );
    }
    fclose(file);

    for (int i = 0; i < acount(g_component_identities); i++) {
        for (int j = i + 1; j < acount(g_component_identities); j++) {
            component_identity *left = &g_component_identities[i];
            component_identity *right = &g_component_identities[j];
            if (left->id == right->id && strcmp(left->key, right->key) != 0) {
                fprintf(
                    stderr,
                    "reflect: duplicate component identity %llu for %s and "
                    "%s\n",
                    (unsigned long long)left->id,
                    left->name,
                    right->name
                );
                g_failed = 1;
            }
            if (strcmp(left->key, right->key) == 0 && left->id != right->id) {
                fprintf(stderr, "reflect: duplicate component key %s\n", left->key);
                g_failed = 1;
            }
        }
    }
}

void record_new_components(const collect_ctx *ctx)
{
    if (!g_manifest_path) {
        return;
    }
    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *s = &ctx->structs[i];
        if (!s->component) {
            continue;
        }
        int index = manifest_match(s);
        uint64_t fingerprint = declaration_fingerprint(s);
        if (index < 0) {
            char key[256];
            snprintf(key, sizeof key, "%s.%s", g_prefix, s->name);
            apush(
                g_component_identities,
                ((component_identity){ strdup(s->file),
                                       s->line,
                                       stable_service_id(key),
                                       strdup(key),
                                       strdup(s->name),
                                       fingerprint,
                                       NULL })
            );
            continue;
        }
        component_identity *identity = &g_component_identities[index];
        if (strcmp(identity->name, s->name) != 0) {
            add_alias(identity, identity->name);
        }
        free(identity->file);
        free(identity->name);
        identity->file = strdup(s->file);
        identity->line = s->line;
        identity->name = strdup(s->name);
        identity->fingerprint = fingerprint;
    }
    for (int i = 0; i < acount(ctx->structs); i++) {
        const struct_info *left = &ctx->structs[i];
        if (!left->component) {
            continue;
        }
        for (int j = i + 1; j < acount(ctx->structs); j++) {
            const struct_info *right = &ctx->structs[j];
            if (right->component && component_id(left) == component_id(right)) {
                fprintf(
                    stderr,
                    "reflect: duplicate component identity %llu for "
                    "declarations %s and %s\n",
                    (unsigned long long)component_id(left),
                    left->name,
                    right->name
                );
                g_failed = 1;
            }
        }
    }
    if (g_failed) {
        return;
    }
}

void write_component_manifest()
{
    if (!g_manifest_path) {
        return;
    }
    char *temporary;
    FILE *file = begin_generated(g_manifest_path, &temporary);
    if (!file) {
        fprintf(stderr, "reflect: could not update component manifest %s\n", g_manifest_path);
        g_failed = 1;
        return;
    }
    fputs("# file line id key current_name fingerprint aliases\n", file);
    for (int i = 0; i < acount(g_component_identities); i++) {
        const component_identity *identity = &g_component_identities[i];
        fprintf(
            file,
            "%s %u %llu %s %s %llu %s\n",
            identity->file,
            identity->line,
            (unsigned long long)identity->id,
            identity->key,
            identity->name,
            (unsigned long long)identity->fingerprint,
            identity->aliases ? identity->aliases : "-"
        );
    }
    publish_generated(file, temporary, g_manifest_path);
}

void dispose_component_manifest()
{
    for (int i = 0; i < acount(g_component_identities); i++) {
        free(g_component_identities[i].file);
        free(g_component_identities[i].key);
        free(g_component_identities[i].name);
        free(g_component_identities[i].aliases);
    }
    afree(g_component_identities);
    g_manifest_path = NULL;
}

uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t hash_string(uint64_t hash, const char *text)
{
    return hash_bytes(hash, text ? text : "", strlen(text ? text : "") + 1);
}
