#include "graph/document/serialize.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "test/instance_bank_fixture.h"
#include "test/whole_retrigger_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *fixture(bool extra)
{
    behavior_asset *asset = whole_retrigger_fixture(WHOLE_RETRIGGER_DIRECT);
    behavior_asset_add_variable(asset, "Label", PIN_STR, pin_type_name(PIN_STR), PIN_CONTAINER_SINGLE, NULL, "");
    if (extra) {
        behavior_asset_add_variable(asset, "Extra", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    }
    return asset;
}

static pin_value value(behavior_instance *bi, const char *name)
{
    for (int i = 0; i < acount(bi->asset->variables); i++) {
        if (!strcmp(bi->asset->variables[i].name, name)) {
            pin_value v;
            assert(!behavior_instance_get_variable(bi, bi->asset->variables[i].id, &v));
            return v;
        }
    }
    assert(false);
    return (pin_value){ 0 };
}

static void wait_for_value(behavior_instance *bi, int expected)
{
    pin_value inputs[] = { { .type = PIN_F64, .d = .5 },
                           { .type = PIN_I32, .i = expected },
                           { .type = PIN_I32 } };
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ inputs, 3 }, NULL);
    assert(run && flow_run_status(run) == FLOW_SUSPENDED);
}

static behavior_instance *create(
    behavior_instance_bank **bank,
    behavior_asset *asset,
    behavior_backend backend,
    int index
)
{
    behavior_instance *bi = behavior_instance_create_in_bank(bank, asset, NULL);
    assert(bi && !behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    char label[32];
    snprintf(label, sizeof label, "instance %d", index);
    pin_value text = { .type = PIN_STR, .type_name = pin_type_name(PIN_STR), .str = label };
    assert(!behavior_instance_set_variable(bi, asset->variables[2].id, &text));
    wait_for_value(bi, index + 10);
    return bi;
}

void instance_bank_smoke(behavior_backend backend)
{
    enum
    {
        COUNT = 273
    };
    behavior_asset *assets[] = { fixture(false), fixture(true) };
    behavior_instance_bank *bank = NULL;
    behavior_instance *instances[COUNT];
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int i = 0; i < COUNT; i++) {
        instances[i] = create(&bank, assets[i % 2], backend, i);
    }
    /* Cancel scattered waits, then reuse holes while other pages retain live
     * activations and owned strings. Different schemas share this test bank. */
    for (int i = 0; i < COUNT; i += 3) {
        behavior_instance_destroy(instances[i]);
        instances[i] = NULL;
    }
    for (int i = 0; i < COUNT; i += 3) {
        instances[i] = create(&bank, assets[(i + 1) % 2], backend, i);
    }
    for (int i = 0; i < COUNT; i++) {
        behavior_instance *bi = instances[i];
        assert(!behavior_instance_tick(bi, 1));
        assert(value(bi, "Value").i == i + 10 && value(bi, "Tails").i == 1);
        char label[32];
        snprintf(label, sizeof label, "instance %d", i);
        assert(!strcmp(value(bi, "Label").str, label));
        assert(acount(bi->activations) == 1);
        assert(!behavior_instance_release_activation(bi, bi->activations[0]));
    }
    for (int a = 0; a < 2; a++) {
        char *json = behavior_asset_save_json(assets[a]);
        behavior_asset *next = behavior_asset_load_json(json);
        free(json);
        assert(next);
        int last = acount(next->variables) - 1;
        behavior_variable swap = next->variables[0];
        next->variables[0] = next->variables[last];
        next->variables[last] = swap;
        for (int i = 0; i < COUNT; i++) {
            behavior_instance *bi = instances[i];
            if (bi->asset != assets[a]) {
                continue;
            }
            assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
            behavior_instance_update(bi, next);
            assert(value(bi, "Value").i == i + 10);
            wait_for_value(bi, i + 1000);
        }
        behavior_asset_free(assets[a]);
        assets[a] = next;
    }
    for (int i = COUNT - 1; i >= 0; i--) {
        behavior_instance *bi = instances[i];
        assert(!behavior_instance_tick(bi, 2));
        assert(value(bi, "Value").i == i + 1000 && value(bi, "Tails").i == 2);
        char label[32];
        snprintf(label, sizeof label, "instance %d", i);
        assert(!strcmp(value(bi, "Label").str, label));
        behavior_instance_destroy(bi);
    }
    behavior_instance_bank_free(bank);
    for (int i = 0; i < 2; i++) {
        behavior_asset_free(assets[i]);
    }

    /* An inactive pooled instance may be reused after a schema edit. Its new
     * variable headers must have the new size even when its page had the old one. */
    bank = NULL;
    behavior_asset *asset = fixture(false);
    behavior_instance *bi = create(&bank, asset, backend, 7);
    assert(!behavior_instance_pool_release(bi));
    behavior_asset_add_variable(asset, "New", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "");
    assert(behavior_instance_pool_reuse(bi, asset, NULL, NULL, NULL) == bi);
    assert(value(bi, "Value").i == 0 && value(bi, "New").d == 0);
    wait_for_value(bi, 42);
    assert(!behavior_instance_tick(bi, 1));
    assert(value(bi, "Value").i == 42);
    behavior_instance_destroy(bi);
    behavior_instance_bank_free(bank);
    behavior_asset_free(asset);
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    printf(
        "PASS: %s instance pages retain waits, independent owned values, "
        "cancellation, schema edits and hot apply\n",
        behavior_backend_name(backend)
    );
}
