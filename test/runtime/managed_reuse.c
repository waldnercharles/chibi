#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_managed_fixture.h"

static void aliases(int kind)
{
    behavior_asset *a = whole_owned_tick_fixture(kind, false);
    uint64_t source_id = a->variables[0].id;
    uint64_t text_id = behavior_asset_add_variable(a, "Text", PIN_STR, "const char *", PIN_CONTAINER_SINGLE, NULL, "")
                           ->id;
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    arena retained = { 0 };
    pin_value snapshot;
    assert(!behavior_instance_copy_variable(bi, source_id, &retained, &snapshot));
    for (int i = 0; i < 16; i++) {
        pin_value borrowed;
        assert(!behavior_instance_get_variable(bi, source_id, &borrowed));
        assert(!behavior_instance_set_variable(bi, source_id, &borrowed));
        assert(pin_value_equal(&bi->variables[0], &snapshot));
    }
    /* A slice borrows the current value's items while replacing that value. */
    pin_array tail = *(pin_array *)bi->variables[0].ptr;
    tail.items++;
    tail.count--;
    tail.capacity = tail.count;
    pin_value value = bi->variables[0];
    value.ptr = &tail;
    assert(!behavior_instance_set_variable(bi, source_id, &value));
    const pin_array *array = bi->variables[0].ptr, *original = snapshot.ptr;
    assert(array->count == 2 && original->count == 3);
    assert(pin_value_equal(&array->items[0], &original->items[1]));
    assert(pin_value_equal(&array->items[1], &original->items[2]));
    if (kind == ARRAY_OP_STRUCTS) {
        pin_value items[] = { array->items[0], array->items[1] };
        items[1].type_name = "MissingManagedRecord";
        pin_array bad = *array;
        bad.items = items;
        value.ptr = &bad;
        assert(behavior_instance_set_variable(bi, source_id, &value) < 0);
        assert(bi->variables[0].ptr == array && array->count == 2);
        assert(pin_value_equal(&array->items[0], &original->items[1]));
    }
    char text[8192];
    memset(text, 'a', sizeof text - 1);
    text[sizeof text - 1] = 0;
    value = (pin_value){ .type = PIN_STR, .type_name = "const char *", .str = text };
    assert(!behavior_instance_set_variable(bi, text_id, &value));
    for (int i = 0; i < 16; i++) {
        pin_value borrowed;
        assert(!behavior_instance_get_variable(bi, text_id, &borrowed));
        borrowed.str += 1;
        assert(!behavior_instance_set_variable(bi, text_id, &borrowed));
        assert(strlen(bi->variables[4].str) == sizeof text - 2 - (size_t)i);
    }
    /* The borrowed substrings were smaller. Warm both arenas at the restored
     * size before requiring allocation-free steady-state writes. */
    assert(!behavior_instance_set_variable(bi, text_id, &value));
    assert(!behavior_instance_set_variable(bi, text_id, &value));
    uint64_t allocations = arena_block_allocations();
    for (int i = 0; i < 16; i++) {
        assert(!behavior_instance_set_variable(bi, text_id, &value));
        assert(!strcmp(bi->variables[4].str, text));
    }
    assert(allocations == arena_block_allocations());
    /* Reordering variables must move both arenas with their stable identity. */
    char *json = behavior_asset_save_json(a);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
    assert(next);
    behavior_variable swap = next->variables[0];
    next->variables[0] = next->variables[4];
    next->variables[4] = swap;
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(a);
    a = next;
    allocations = arena_block_allocations();
    for (int i = 0; i < 16; i++) {
        assert(!behavior_instance_set_variable(bi, text_id, &value));
        pin_value current;
        assert(!behavior_instance_get_variable(bi, source_id, &current));
        assert(!behavior_instance_set_variable(bi, source_id, &current));
    }
    assert(allocations == arena_block_allocations());
    assert(!behavior_instance_pool_release(bi));
    for (int i = 0; i < bi->variable_count * 2; i++) {
        assert(!bi->variable_arenas[i].first);
    }
    assert(behavior_instance_pool_reuse(bi, a, NULL, NULL, NULL) == bi);
    pin_value reset;
    assert(!behavior_instance_get_variable(bi, source_id, &reset));
    assert(pin_value_equal(&reset, &snapshot));
    assert(!behavior_instance_get_variable(bi, text_id, &reset));
    assert(reset.str && !reset.str[0]);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    assert(original->count == 3);
    if (kind == ARRAY_OP_INTS) {
        assert(original->items[0].i == 1);
    } else if (kind == ARRAY_OP_STRUCTS) {
        const whole_managed *record = original->items[0].ptr;
        assert(!strcmp(record->label, "one") && !strcmp(record->nested.text, "one"));
        assert(record->count == 1 && record->nested.weight == 0.25f);
    } else {
        assert(!strcmp(original->items[0].str, "one"));
    }
    arena_free(&retained);
}

static void scalar_transition()
{
    behavior_asset *a = behavior_asset_new("ManagedTransition");
    uint64_t id = behavior_asset_add_variable(a, "Value", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!bi->variable_spares);
    pin_value value = { .type = PIN_STR, .type_name = "const char *", .str = "retained" };
    assert(!behavior_instance_set_variable(bi, id, &value));
    pin_value borrowed;
    assert(!behavior_instance_get_variable(bi, id, &borrowed));
    assert(!behavior_instance_set_variable(bi, id, &borrowed));
    value = (pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 };
    assert(!behavior_instance_set_variable(bi, id, &value));
    assert(!behavior_instance_get_variable(bi, id, &borrowed));
    assert(borrowed.type == PIN_I32 && borrowed.i == 42);
    value = (pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "restored" };
    uint64_t allocations = arena_block_allocations();
    for (int i = 0; i < 16; i++) {
        assert(!behavior_instance_set_variable(bi, id, &value));
        assert(!behavior_instance_get_variable(bi, id, &borrowed));
        assert(!strcmp(borrowed.str, "restored"));
    }
    assert(allocations == arena_block_allocations());
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

void test_managed_reuse()
{
    scalar_transition();
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        aliases(kind);
    }
    test_note("PASS: reusable managed variable storage, self-assignment, borrowed slices/text, rejected copies, hot-apply reordering and pooling\n");
}
