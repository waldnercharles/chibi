#include "host/clock_package.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>

static _Atomic uint64_t next_domain = 1;

static host_clock_status platform_read(void *, int64_t *nanoseconds, int *error)
{
    return host_clock_read(nanoseconds, error);
}

void host_clock_grant_init(host_clock_grant *grant, host_clock_provider read, void *state)
{
    assert(grant);
    uint64_t domain = atomic_fetch_add_explicit(&next_domain, 1, memory_order_relaxed);
    assert(domain && domain < UINT64_MAX);
    *grant = (host_clock_grant){ domain, read ? read : platform_read, state };
}

static const char *sample(void *service, const pin_value *, pin_value *results)
{
    results[0].i64 = 0;
    results[1].u64 = 0;
    results[2].i = HOST_CLOCK_SAMPLE_DENIED;
    results[3].i = 0;
    if (!service) {
        return NULL;
    }
    const host_clock_grant *grant = service;
    assert(grant->domain && grant->read);
    int64_t nanoseconds = 0;
    int error = 0;
    host_clock_status status = grant->read(grant->state, &nanoseconds, &error);
    if (status == HOST_CLOCK_OK && nanoseconds < 0) {
        status = HOST_CLOCK_FAILED;
        error = ERANGE;
    }
    if (status == HOST_CLOCK_OK) {
        results[0].i64 = nanoseconds;
        results[1].u64 = grant->domain;
        results[2].i = HOST_CLOCK_SAMPLE_OK;
        return NULL;
    }
    assert(status == HOST_CLOCK_UNAVAILABLE || status == HOST_CLOCK_FAILED);
    results[2].i = status == HOST_CLOCK_UNAVAILABLE ? HOST_CLOCK_SAMPLE_UNAVAILABLE
                                                    : HOST_CLOCK_SAMPLE_FAILED;
    results[3].i = status == HOST_CLOCK_FAILED ? error : 0;
    return NULL;
}

const native_capability_pack *host_clock_package()
{
    static const script_catalog_parameter results[] = {
        { .name = "nanoseconds", .type = { .storage = PIN_I64 } },
        { .name = "domain", .type = { .storage = PIN_U64 } },
        { .name = "status", .type = { .storage = PIN_I32 } },
        { .name = "platform_error", .type = { .storage = PIN_I32 } },
    };
    static const script_catalog_capability capability = {
        .id = HOST_CLOCK_SAMPLE_ID,
        .name = "host:clock.Sample",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .results = results,
        .result_count = sizeof results / sizeof *results,
        .effects = SCRIPT_EFFECT_HOST,
    };
    static const script_catalog catalog = { .capabilities = &capability,
                                            .capability_count = 1 };
    static const node_host_call call = {
        .service = HOST_CLOCK_SERVICE,
        .invoke = sample,
        .optional_service = true,
    };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    static const native_capability_binding binding = {
        .id = HOST_CLOCK_SAMPLE_ID,
        .call = &call,
        .access = &access,
    };
    static const native_capability_pack pack = {
        .catalog = &catalog,
        .bindings = &binding,
        .binding_count = 1,
    };
    return &pack;
}
