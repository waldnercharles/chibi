#include "support/asset_value.h"

#include "support/asset_json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_asset_value()
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
    av_obj_add_int(doc, item, "offset", INT64_MIN);
    av_obj_add_bool(doc, item, "active", true);
    av_obj_add_null(doc, root, "optional");
    av_obj_set(doc, root, "bytes", av_strn(doc, "a\0b", 3));
    assert(av_get_source(root).start == SIZE_MAX);

    /* Snapshot lifetimes are independent of the source document, including
     * string bytes, nested arrays and source locations. */
    av_value *snapshot = av_value_copy(root);
    av_doc *subtree = av_doc_clone_value(item);
    assert(av_equals(root, snapshot));
    av_set_str(name, "changed");
    assert(!av_equals(root, snapshot));
    av_obj_remove_key(root, "optional");
    av_doc_free(doc);
    av_value *saved_item = av_arr_get(av_obj_get(snapshot, "items"), 0);
    assert(!strcmp(av_get_str(av_obj_get(saved_item, "name")), "alpha"));
    assert(av_get_source(av_obj_get(saved_item, "name")).start == 20);
    assert(av_get_uint(av_obj_get(saved_item, "id")) == UINT64_MAX);
    assert(av_get_sint(av_obj_get(saved_item, "offset")) == INT64_MIN);
    assert(av_get_bool(av_obj_get(saved_item, "active")));
    assert(av_is_null(av_obj_get(snapshot, "optional")));
    assert(av_get_len(av_obj_get(snapshot, "bytes")) == 3);
    assert(!memcmp(av_get_str(av_obj_get(snapshot, "bytes")), "a\0b", 3));
    assert(av_equals(saved_item, av_doc_root(subtree)));

    doc = av_doc_new();
    av_value *ordered = av_obj(doc);
    av_obj_add_int(doc, ordered, "a", 1);
    av_obj_add_int(doc, ordered, "b", 2);
    av_obj_add_int(doc, ordered, "c", 3);
    av_obj_add_int(doc, ordered, "b", 20);
    assert(av_obj_size(ordered) == 3);
    assert(!strcmp(av_get_str(av_obj_key_at(ordered, 1)), "b"));
    assert(av_get_int(av_obj_get(ordered, "b")) == 20);
    assert(av_obj_remove_key(ordered, "b"));
    assert(!strcmp(av_get_str(av_obj_key_at(ordered, 1)), "c"));
    av_value *reordered = av_obj(doc);
    av_obj_add_uint(doc, reordered, "c", 3);
    av_obj_add_uint(doc, reordered, "a", 1);
    assert(av_equals(ordered, reordered));
    av_obj_iter iter = av_obj_iter_with(ordered);
    av_value *key = av_obj_iter_next(&iter);
    assert(!strcmp(av_get_str(key), "a"));
    assert(av_get_int(av_obj_iter_get_val(key)) == 1);
    assert(av_obj_iter_next(&iter));
    assert(!av_obj_iter_next(&iter));
    av_value *array = av_arr(doc);
    av_arr_append(array, av_sint(doc, 1));
    av_arr_append(array, av_sint(doc, 2));
    av_value *old = av_arr_replace(array, 0, av_sint(doc, 3));
    assert(av_get_int(old) == 1);
    assert(av_get_int(av_arr_get(array, 0)) == 3);
    assert(!av_arr_get(array, 2));
    av_arr_append(array, av_sint(doc, 4));
    av_value *removed = av_arr_remove(array, 1);
    assert(av_get_int(removed) == 2);
    assert(av_arr_size(array) == 2);
    assert(av_get_int(av_arr_get(array, 0)) == 3);
    assert(av_get_int(av_arr_get(array, 1)) == 4);
    assert(!av_arr_remove(array, 2));
    /* Removed nodes remain alive and can be reattached within their owner. */
    av_arr_append(array, removed);
    assert(av_get_int(av_arr_get(array, 2)) == 2);

    assert(av_equals(av_sint(doc, 42), av_uint(doc, 42)));
    assert(av_equals(av_sint(doc, 42), av_real(doc, 42)));
    assert(!av_equals(av_sint(doc, -1), av_uint(doc, UINT64_MAX)));
    assert(!av_equals(av_uint(doc, UINT64_MAX), av_real(doc, 0x1p64)));
    assert(!av_equals(av_sint(doc, INT64_MAX), av_real(doc, 0x1p63)));
    assert(!av_equals(av_sint(doc, 1), av_real(doc, 1.5)));
    av_doc_free(doc);
    av_doc_free(subtree);
    av_value_free(snapshot);

    doc = av_doc_new();
    root = av_obj(doc);
    av_doc_set_root(doc, root);
    items = av_obj_add_arr(doc, root, "records");
    item = av_arr_add_obj(doc, items);
    av_obj_add_str(doc, item, "name", "owned string");
    yyjson_mut_doc *wire = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(wire, av_to_json(wire, root));
    av_doc_free(doc);
    /* A protocol document owns every key and string after native release. */
    char *json = yyjson_mut_write(wire, 0, NULL);
    assert(json);
    assert(!strcmp(json, "{\"records\":[{\"name\":\"owned string\"}]}"));
    free(json);
    yyjson_mut_doc_free(wire);
    puts("PASS: native asset values preserve owned snapshots, types and source spans");
}
