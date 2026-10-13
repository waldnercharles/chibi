#include "authoring/resource_catalog.h"

#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[128];
static void setup()
{
    strcpy(directory, "/tmp/chibi-resources-XXXXXX");
    assert(mkdtemp(directory));
}
static void teardown()
{
    tool_remove_tree(directory);
}
static void write_bytes(const char *name, const void *bytes, size_t size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, size, file) == size);
    assert(!fclose(file));
}
static void write_text(const char *name, const char *text)
{
    write_bytes(name, text, strlen(text));
}
static av_doc *scan()
{
    char error[256];
    av_doc *doc = authoring_resource_catalog(directory, error, sizeof error);
    assert(doc);
    return doc;
}
static av_value *resource(av_doc *doc, const char *key)
{
    av_value *records = av_obj_get(av_doc_root(doc), "resources");
    for (size_t i = 0; i < av_arr_size(records); i++) {
        av_value *record = av_arr_get(records, i);
        if (!strcmp(av_get_str(av_obj_get(record, "key")), key)) {
            return record;
        }
    }
    return NULL;
}
static bool is_text(const av_value *value, const char *expected)
{
    const char *text = av_get_str(value);
    return text && !strcmp(text, expected);
}
TEST_CASE(resources_sort_and_report_literal_origins)
{
    write_text("z.ttf", "bad font");
    write_text("a.mesh", "bad mesh");
    write_text("z.chibi", "// \"content/missing.png\"\nlet a = \"content/z.ttf\";\n");
    write_text("a.rml", "<img src='content/z.ttf'/>\n");
    av_doc *doc = scan();
    av_value *root = av_doc_root(doc), *records = av_obj_get(root, "resources");
    REQUIRE(av_get_bool(av_obj_get(root, "complete")));
    REQUIRE(av_arr_size(records) == 3);
    REQUIRE(is_text(av_obj_get(av_arr_get(records, 0), "key"), "content/a.mesh"));
    REQUIRE(is_text(av_obj_get(av_arr_get(records, 1), "key"), "content/missing.png"));
    av_value *font = resource(doc, "content/z.ttf");
    REQUIRE(is_text(av_obj_get(font, "status"), "failed"));
    av_value *uses = av_obj_get(font, "consumers");
    REQUIRE(av_arr_size(uses) == 2);
    REQUIRE(is_text(av_obj_get(av_arr_get(uses, 0), "path"), "content/a.rml"));
    REQUIRE(av_get_int(av_obj_get(av_arr_get(uses, 1), "line")) == 2);
    REQUIRE(is_text(av_obj_get(resource(doc, "content/missing.png"), "status"), "missing"));
    av_doc_free(doc);
    return true;
}
TEST_CASE(resources_fingerprint_tracks_raw_bytes_and_paths)
{
    write_text("a.ttf", "first");
    av_doc *first = scan(), *again = scan();
    REQUIRE(av_equals_ordered(av_doc_root(first), av_doc_root(again)));
    char *hash = xstrdup(av_get_str(av_obj_get(av_doc_root(first), "content_hash")));
    write_text("a.ttf", "other");
    av_doc *changed = scan();
    REQUIRE(!is_text(av_obj_get(av_doc_root(changed), "content_hash"), hash));
    av_doc_free(changed);
    write_text("a.ttf", "first");
    write_text("source.chibi", "let value = 2;\n");
    changed = scan();
    REQUIRE(!is_text(av_obj_get(av_doc_root(changed), "content_hash"), hash));
    REQUIRE(is_text(
        av_obj_get(av_doc_root(changed), "resource_hash"),
        av_get_str(av_obj_get(av_doc_root(first), "resource_hash"))
    ));
    av_doc_free(changed);
    char before[512], after[512];
    snprintf(before, sizeof before, "%s/a.ttf", directory);
    snprintf(after, sizeof after, "%s/b.ttf", directory);
    REQUIRE(!rename(before, after));
    changed = scan();
    REQUIRE(!is_text(av_obj_get(av_doc_root(changed), "content_hash"), hash));
    free(hash);
    av_doc_free(first);
    av_doc_free(again);
    av_doc_free(changed);
    return true;
}
TEST_CASE(resources_fingerprint_includes_authored_material_edits_without_resource_uses)
{
    write_text("surface.kdl", "name \"Surface\"\ncolor 0xff0000ff\n");
    write_text("layout.rcss", "body { color: white; }\n");
    av_doc *first = scan();
    av_value *sources = av_obj_get(av_doc_root(first), "authored_sources");
    REQUIRE(av_arr_size(sources) == 2);
    REQUIRE(is_text(av_obj_get(av_arr_get(sources, 0), "path"), "content/layout.rcss"));
    REQUIRE(av_is_str(av_obj_get(av_arr_get(sources, 1), "content_hash")));
    write_text("surface.kdl", "name \"Surface\"\ncolor 0x00ff00ff\n");
    av_doc *changed = scan();
    REQUIRE(!is_text(
        av_obj_get(av_doc_root(changed), "content_hash"),
        av_get_str(av_obj_get(av_doc_root(first), "content_hash"))
    ));
    REQUIRE(is_text(
        av_obj_get(av_doc_root(changed), "resource_hash"),
        av_get_str(av_obj_get(av_doc_root(first), "resource_hash"))
    ));
    av_doc_free(first);
    av_doc_free(changed);
    return true;
}
TEST_CASE(resources_omit_identity_when_an_authored_source_exceeds_the_budget)
{
    char path[512];
    snprintf(path, sizeof path, "%s/large.chibi", directory);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(!ftruncate(fileno(file), AUTHORING_RESOURCE_FILE_BYTES_MAX + 1));
    REQUIRE(!fclose(file));
    av_doc *doc = scan();
    av_value *root = av_doc_root(doc);
    REQUIRE(!av_get_bool(av_obj_get(root, "complete")));
    REQUIRE(!av_obj_get(root, "content_hash"));
    REQUIRE(!av_obj_get(root, "resource_hash"));
    av_value *sources = av_obj_get(root, "authored_sources");
    REQUIRE(av_arr_size(sources) == 1);
    REQUIRE(is_text(av_obj_get(av_arr_get(sources, 0), "status"), "limit_exceeded"));
    REQUIRE(!av_obj_get(av_arr_get(sources, 0), "content_hash"));
    av_doc_free(doc);
    return true;
}
TEST_CASE(resources_expose_header_estimate_without_claiming_decode)
{
    /* Deliberately only a PNG header: estimates must remain labelled metadata. */
    unsigned char png[24] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    png[19] = 2;
    png[23] = 3;
    write_bytes("image.png", png, sizeof png);
    av_doc *doc = scan();
    av_value *image = resource(doc, "content/image.png");
    REQUIRE(is_text(av_obj_get(image, "status"), "metadata"));
    REQUIRE(av_get_int(av_obj_get(av_obj_get(image, "metadata"), "width")) == 2);
    REQUIRE(av_get_uint(av_obj_get(av_obj_get(image, "costs"), "cpu_payload_bytes")) == 24);
    REQUIRE(av_is_null(av_obj_get(av_obj_get(image, "costs"), "gpu_payload_bytes")));
    av_doc_free(doc);
    return true;
}
TEST_CASE(resources_bound_file_reads_and_omit_incomplete_identity)
{
    char path[512];
    snprintf(path, sizeof path, "%s/large.ttf", directory);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(!ftruncate(fileno(file), AUTHORING_RESOURCE_FILE_BYTES_MAX + 1));
    REQUIRE(!fclose(file));
    av_doc *doc = scan();
    REQUIRE(!av_get_bool(av_obj_get(av_doc_root(doc), "complete")));
    REQUIRE(!av_obj_get(av_doc_root(doc), "content_hash"));
    REQUIRE(is_text(av_obj_get(resource(doc, "content/large.ttf"), "status"), "limit_exceeded"));
    av_doc_free(doc);
    return true;
}
TEST_CASE(resources_skip_symlinks_and_do_not_claim_missing)
{
    char path[512];
    snprintf(path, sizeof path, "%s/link.ttf", directory);
    REQUIRE(!symlink("/etc/passwd", path));
    write_text("source.kdl", "font \"content/link.ttf\"\n");
    av_doc *doc = scan();
    REQUIRE(!av_get_bool(av_obj_get(av_doc_root(doc), "complete")));
    REQUIRE(is_text(av_obj_get(resource(doc, "content/link.ttf"), "status"), "not_inspected"));
    av_doc_free(doc);
    return true;
}
TEST_CASE(resources_read_wav_frames_and_font_metadata)
{
    unsigned char wav[48] = { 'R', 'I', 'F', 'F', 40,  0,   0,   0,   'W', 'A',
                              'V', 'E', 'f', 'm', 't', ' ', 16,  0,   0,   0,
                              1,   0,   1,   0,   68,  172, 0,   0,   136, 88,
                              1,   0,   2,   0,   16,  0,   'd', 'a', 't', 'a',
                              4,   0,   0,   0,   0,   0,   0,   0 };
    write_bytes("sound.wav", wav, sizeof wav);
    unsigned char font[12] = { 0, 1, 0, 0 };
    write_bytes("font.ttf", font, sizeof font);
    av_doc *doc = scan();
    av_value *sound = resource(doc, "content/sound.wav");
    REQUIRE(is_text(av_obj_get(sound, "status"), "metadata"));
    REQUIRE(av_get_uint(av_obj_get(av_obj_get(sound, "metadata"), "frames")) == 2);
    REQUIRE(av_get_uint(av_obj_get(av_obj_get(sound, "costs"), "cpu_payload_bytes")) == 8);
    REQUIRE(is_text(av_obj_get(resource(doc, "content/font.ttf"), "status"), "metadata"));
    av_doc_free(doc);
    return true;
}
void test_tool_resource_catalog()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(resources_sort_and_report_literal_origins);
    RUN_TEST_CASE(resources_fingerprint_tracks_raw_bytes_and_paths);
    RUN_TEST_CASE(resources_fingerprint_includes_authored_material_edits_without_resource_uses);
    RUN_TEST_CASE(resources_omit_identity_when_an_authored_source_exceeds_the_budget);
    RUN_TEST_CASE(resources_expose_header_estimate_without_claiming_decode);
    RUN_TEST_CASE(resources_bound_file_reads_and_omit_incomplete_identity);
    RUN_TEST_CASE(resources_skip_symlinks_and_do_not_claim_missing);
    RUN_TEST_CASE(resources_read_wav_frames_and_font_metadata);
    pu_clear_setup();
}
