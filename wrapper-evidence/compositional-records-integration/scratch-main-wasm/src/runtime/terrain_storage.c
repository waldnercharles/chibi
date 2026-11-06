#include "runtime/terrain_storage.h"

#include "execution/function.h"
#include "runtime/materials.h"
#include "runtime/save.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct terrain_storage
{
    materials *materials;
    save *save;
    ecs_entity source;
    char *identity;
    int sequence, request;
    bool load, pending, succeeded;
    char error[256];
};

terrain_storage *terrain_storage_new(materials *materials, save *save)
{
    assert(materials && save);
    terrain_storage *storage = xcalloc(1, sizeof *storage);
    storage->materials = materials;
    storage->save = save;
    return storage;
}

void terrain_storage_reset(terrain_storage *storage)
{
    free(storage->identity);
    storage->identity = NULL;
    storage->request = 0;
    storage->pending = storage->succeeded = false;
    storage->error[0] = 0;
}

void terrain_storage_free(terrain_storage *storage)
{
    terrain_storage_reset(storage);
    free(storage);
}

int terrain_storage_request(
    terrain_storage *storage,
    ecs_entity source,
    const char *identity,
    bool load,
    char *error,
    size_t size
)
{
    const char *problem = NULL;
    if (storage->pending) {
        problem = "A terrain storage request is already pending";
    } else if (!identity || !identity[0] || strlen(identity) > SAVE_BLOB_KEY_MAX) {
        problem = "Terrain save identity must contain 1-64 bytes";
    } else if (!ecs_entity_to_u64(source)) {
        problem = "Terrain storage requires a source entity";
    } else if (storage->sequence == INT_MAX) {
        problem = "Terrain storage request tokens are exhausted";
    }
    if (problem) {
        snprintf(error, size, "%s", problem);
        return 0;
    }
    free(storage->identity);
    storage->identity = xstrdup(identity);
    storage->source = source;
    storage->load = load;
    storage->pending = true;
    storage->succeeded = false;
    storage->error[0] = 0;
    storage->request = ++storage->sequence;
    if (size) {
        error[0] = 0;
    }
    return storage->request;
}

bool terrain_storage_pending(const terrain_storage *storage)
{
    return storage->pending;
}

terrain_storage_state terrain_storage_result(const terrain_storage *storage, int request)
{
    if (request <= 0 || request != storage->request) {
        return (terrain_storage_state){
            .error = "Terrain storage request is unknown or expired"
        };
    }
    return (terrain_storage_state){ .pending = storage->pending,
                                    .succeeded = storage->succeeded,
                                    .error = storage->error };
}

void terrain_storage_flush(terrain_storage *storage)
{
    if (!storage->pending) {
        return;
    }
    unsigned char *bytes = NULL;
    size_t length = 0;
    bool ok = false;
    if (storage->load &&
        !save_blob_read(storage->save, storage->identity, &bytes, &length)) {
        snprintf(storage->error, sizeof storage->error, "%s", save_error(storage->save));
        goto done;
    }
    const char *problem = materials_prepare(storage->materials);
    if (problem) {
        snprintf(storage->error, sizeof storage->error, "%s", problem);
        goto done;
    }
    if (storage->load) {
        if (!materials_terrain_checkpoint_read(
                storage->materials,
                storage->source,
                storage->identity,
                bytes,
                length,
                storage->error,
                sizeof storage->error
            )) {
            goto done;
        }
        problem = materials_prepare(storage->materials);
        if (problem) {
            snprintf(storage->error, sizeof storage->error, "Terrain restored, but preparation failed: %s", problem);
            goto done;
        }
        ok = true;
        goto done;
    }
    if (!materials_terrain_checkpoint_write(
            storage->materials,
            storage->source,
            storage->identity,
            &bytes,
            &length,
            storage->error,
            sizeof storage->error
        )) {
        goto done;
    }
    ok = save_blob_write(storage->save, storage->identity, bytes, length);
    if (!ok) {
        snprintf(storage->error, sizeof storage->error, "%s", save_error(storage->save));
    }
done:
    free(bytes);
    free(storage->identity);
    storage->identity = NULL;
    storage->pending = false;
    storage->succeeded = ok;
}

static const char *request(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out,
    bool load
)
{
    const char *name;
    const char *conversion_error = pin_text_cstring(ctx->value_storage, in[1], &name, NULL);
    if (conversion_error) {
        return conversion_error;
    }
    char error[256] = "Terrain storage requires a material world";
    out[0].i = service
                 ? terrain_storage_request(service, ecs_entity_from_u64(in[0].u64), name, load, error, sizeof error)
                 : 0;
    pin_value value;
    const char *text_error = pin_value_text(ctx->value_storage, &value, error, strlen(error));
    if (text_error) {
        return text_error;
    }
    out[1].text = value.text;
    return NULL;
}

static const char *save_terrain(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    return request(ctx, service, in, out, false);
}

static const char *load_terrain(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    return request(ctx, service, in, out, true);
}

static const char *result(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    terrain_storage_state
        state = service
                  ? terrain_storage_result(service, in[0].i)
                  : (terrain_storage_state){
                        .error = "Terrain storage requires a material world"
                    };
    out[0].b = state.pending;
    out[1].b = state.succeeded;
    pin_value value;
    const char *text_error = pin_value_text(
        ctx->value_storage,
        &value,
        state.error,
        strlen(state.error)
    );
    if (text_error) {
        return text_error;
    }
    out[2].text = value.text;
    return NULL;
}

const node_host_call TERRAIN_SAVE_CALL = { .service = TERRAIN_STORAGE_SERVICE,
                                           .context = save_terrain,
                                           .optional_service = true };
const node_host_call TERRAIN_LOAD_CALL = { .service = TERRAIN_STORAGE_SERVICE,
                                           .context = load_terrain,
                                           .optional_service = true };
const node_host_call TERRAIN_STORAGE_RESULT_CALL = { .service = TERRAIN_STORAGE_SERVICE,
                                                     .context = result,
                                                     .optional_service = true };
const node_access TERRAIN_STORAGE_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const node_access TERRAIN_STORAGE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
