#include "support/asset_value.h"

#include "support/asset_json.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

/* Snapshot lifetimes are independent of the source document, including
 * string bytes, nested arrays and source locations. */
TEST_CASE(asset_value_snapshots_own_their_values)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_value *items = av_obj_add_arr(doc, root, "items");
    av_value *item = av_arr_add_obj(doc, items);
    av_obj_add_str(doc, item, "name", "alpha");
    av_value *name = av_obj_get(item, "name");
    av_set_source(name, (av_source){ 20, 27, 15, 19 });
    av_obj_add_uint(doc, item, "id", UINT64_MAX);
    av_obj_set(doc, item, "color", av_color(doc, 0x399fe8ff));
    av_obj_set(doc, item, "number", av_float(doc, 0.035f));
    av_obj_add_int(doc, item, "offset", INT64_MIN);
    av_obj_add_bool(doc, item, "active", true);
    av_obj_add_null(doc, root, "optional");
    av_obj_set(doc, root, "bytes", av_strn(doc, "a\0b", 3));
    REQUIRE(av_get_source(root).start == SIZE_MAX);

    av_value *snapshot = av_value_copy(root);
    av_doc *subtree = av_doc_clone_value(item);
    REQUIRE(av_equals(root, snapshot));
    av_set_str(name, "changed");
    REQUIRE(!av_equals(root, snapshot));
    av_obj_remove_key(root, "optional");
    av_doc_free(doc);
    av_value *saved_item = av_arr_get(av_obj_get(snapshot, "items"), 0);
    REQUIRE(!strcmp(av_get_str(av_obj_get(saved_item, "name")), "alpha"));
    REQUIRE(av_get_source(av_obj_get(saved_item, "name")).start == 20);
    REQUIRE(av_get_uint(av_obj_get(saved_item, "id")) == UINT64_MAX);
    REQUIRE(av_is_color(av_obj_get(saved_item, "color")));
    REQUIRE(av_is_hex(av_obj_get(saved_item, "color")));
    REQUIRE(av_get_uint(av_obj_get(saved_item, "color")) == 0x399fe8ff);
    REQUIRE((float)av_get_num(av_obj_get(saved_item, "number")) == 0.035f);
    REQUIRE(av_get_sint(av_obj_get(saved_item, "offset")) == INT64_MIN);
    REQUIRE(av_get_bool(av_obj_get(saved_item, "active")));
    REQUIRE(av_is_null(av_obj_get(snapshot, "optional")));
    REQUIRE(av_get_len(av_obj_get(snapshot, "bytes")) == 3);
    REQUIRE(!memcmp(av_get_str(av_obj_get(snapshot, "bytes")), "a\0b", 3));
    REQUIRE(av_equals(saved_item, av_doc_root(subtree)));
    av_doc_free(subtree);
    av_value_free(snapshot);
    return true;
}

/* Objects keep insertion order with replaced keys in place; removed nodes
 * stay alive within their owner; numbers compare by exact value. */
TEST_CASE(asset_value_collections_keep_order_and_exact_numbers)
{
    av_doc *doc = av_doc_new();
    av_value *ordered = av_obj(doc);
    av_obj_add_int(doc, ordered, "a", 1);
    av_obj_add_int(doc, ordered, "b", 2);
    av_obj_add_int(doc, ordered, "c", 3);
    av_obj_add_int(doc, ordered, "b", 20);
    REQUIRE(av_obj_size(ordered) == 3);
    REQUIRE(!strcmp(av_get_str(av_obj_key_at(ordered, 1)), "b"));
    REQUIRE(av_get_int(av_obj_get(ordered, "b")) == 20);
    REQUIRE(av_obj_remove_key(ordered, "b"));
    REQUIRE(!strcmp(av_get_str(av_obj_key_at(ordered, 1)), "c"));
    av_value *reordered = av_obj(doc);
    av_obj_add_uint(doc, reordered, "c", 3);
    av_obj_add_uint(doc, reordered, "a", 1);
    REQUIRE(av_equals(ordered, reordered));
    av_obj_iter iter = av_obj_iter_with(ordered);
    av_value *key = av_obj_iter_next(&iter);
    REQUIRE(!strcmp(av_get_str(key), "a"));
    REQUIRE(av_get_int(av_obj_iter_get_val(key)) == 1);
    REQUIRE(av_obj_iter_next(&iter));
    REQUIRE(!av_obj_iter_next(&iter));
    av_value *array = av_arr(doc);
    av_arr_append(array, av_sint(doc, 1));
    av_arr_append(array, av_sint(doc, 2));
    av_value *old = av_arr_replace(array, 0, av_sint(doc, 3));
    REQUIRE(av_get_int(old) == 1);
    REQUIRE(av_get_int(av_arr_get(array, 0)) == 3);
    REQUIRE(!av_arr_get(array, 2));
    av_arr_append(array, av_sint(doc, 4));
    av_value *removed = av_arr_remove(array, 1);
    REQUIRE(av_get_int(removed) == 2);
    REQUIRE(av_arr_size(array) == 2);
    REQUIRE(av_get_int(av_arr_get(array, 0)) == 3);
    REQUIRE(av_get_int(av_arr_get(array, 1)) == 4);
    REQUIRE(!av_arr_remove(array, 2));
    /* Removed nodes remain alive and can be reattached within their owner. */
    av_arr_append(array, removed);
    REQUIRE(av_get_int(av_arr_get(array, 2)) == 2);

    REQUIRE(av_equals(av_sint(doc, 42), av_uint(doc, 42)));
    REQUIRE(av_equals(av_sint(doc, 42), av_real(doc, 42)));
    REQUIRE(!av_equals(av_sint(doc, -1), av_uint(doc, UINT64_MAX)));
    REQUIRE(!av_equals(av_uint(doc, UINT64_MAX), av_real(doc, 0x1p64)));
    REQUIRE(!av_equals(av_sint(doc, INT64_MAX), av_real(doc, 0x1p63)));
    REQUIRE(!av_equals(av_sint(doc, 1), av_real(doc, 1.5)));
    av_doc_free(doc);
    return true;
}

/* A protocol document owns every key and string after native release. */
TEST_CASE(asset_value_json_owns_its_strings)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_value *items = av_obj_add_arr(doc, root, "records");
    av_value *item = av_arr_add_obj(doc, items);
    av_obj_add_str(doc, item, "name", "owned string");
    yyjson_mut_doc *wire = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(wire, av_to_json(wire, root));
    av_doc_free(doc);
    char *json = yyjson_mut_write(wire, 0, NULL);
    REQUIRE(json);
    REQUIRE(!strcmp(json, "{\"records\":[{\"name\":\"owned string\"}]}"));
    free(json);
    yyjson_mut_doc_free(wire);
    return true;
}

void test_asset_value()
{
    RUN_TEST_CASE(asset_value_snapshots_own_their_values);
    RUN_TEST_CASE(asset_value_collections_keep_order_and_exact_numbers);
    RUN_TEST_CASE(asset_value_json_owns_its_strings);
}
