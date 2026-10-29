#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COMPONENT                                                              \
    "#define COMPONENT(T) struct __attribute__((annotate(\"node:type\"), "     \
    "annotate(\"node:component:index:dense\"))) T\n"
#define HEALTH "typedef COMPONENT(Health) { int current; } Health;\n"
#define HEALTH_MAXIMUM                                                         \
    "typedef COMPONENT(Health) { int current; int maximum; } Health;\n"
#define VITALITY "typedef COMPONENT(Vitality) { int current; } Vitality;\n"
#define SPEED "typedef COMPONENT(Speed) { float value; } Speed;\n"
#define PACE "typedef COMPONENT(Pace) { float value; } Pace;\n"
#define COLLIDER "typedef COMPONENT(Collider) { float value; } Collider;\n"
#define HANDLE                                                                 \
    "typedef unsigned long long uint64_t;\n" COMPONENT                         \
    "typedef COMPONENT(Owner) { %s entity "                                    \
    "__attribute__((annotate(\"node:handle:ecs.entity\"))); } Owner;\n"
#define JIT                                                                    \
    "#define TYPE __attribute__((annotate(\"node:type\")))\n"                  \
    "#define FN __attribute__((annotate(\"node:fn\"), "                        \
    "annotate(\"node:jit:identity\")))\n"                                      \
    "typedef struct TYPE Vector { float x; float y; } Vector;\n"               \
    "typedef Vector Alias;\n"                                                  \
    "typedef struct TYPE Different { float x; float y; } Different;\n"         \
    "FN %s;\n"
#define PRESENTED                                                              \
    "typedef COMPONENT(Shot) { float speed "                                   \
    "__attribute__((annotate(\"node:unit:u/s\"), "                             \
    "annotate(\"node:range:0 2.5\"))); %s; } Shot;\n"
#define DERIVED                                                                \
    "#define DERIVED(T, SOURCE) struct "                                       \
    "__attribute__((annotate(\"node:type\"), "                                 \
    "annotate(\"node:component:index:dense\"), "                               \
    "annotate(\"node:derived-from:\" #SOURCE))) T\n"                           \
    "typedef DERIVED(Total, %s) { int value; } Total;\n"
#define HANDLE_PIN "PIN_HANDLE,"
#define JIT_REJECTION "identity requires one struct argument"
#define CONCURRENT_RUNS "6"
/* Runs "$@" $1 times at once and fails if any run fails. */
#define CONCURRENT_SCRIPT                                                      \
    "n=$1; shift; pids=; i=0\n"                                                \
    "while [ $i -lt $n ]; do\n"                                                \
    "    \"$@\" & pids=\"$pids $!\"; i=$((i + 1))\n"                           \
    "done\n"                                                                   \
    "status=0\n"                                                               \
    "for pid in $pids; do wait $pid || status=1; done\n"                       \
    "exit $status\n"

typedef struct
{
    unsigned long long id;
    char key[128];
    char name[128];
} identity_row;

typedef struct
{
    char *text;
    long long modified;
} output_snapshot;

static char directory[PATH_MAX], source[PATH_MAX], output[PATH_MAX],
    runtime[PATH_MAX], manifest[PATH_MAX], log_path[PATH_MAX],
    output_target[PATH_MAX], manifest_target[PATH_MAX];
static const char *const command[] = { CHIBI_REFLECT_PATH,
                                       source,
                                       "--prefix",
                                       "TEST",
                                       "--types-output",
                                       output,
                                       "--runtime-output",
                                       runtime,
                                       "--manifest",
                                       manifest,
                                       NULL };
static identity_row *original, *inserted, *readded, *latest;
static output_snapshot output_before, manifest_before;

static void path(char *out, const char *name)
{
    int size = snprintf(out, PATH_MAX, "%s/%s", directory, name);
    assert(size > 0 && size < PATH_MAX);
}

static int reflect(const char *text)
{
    tool_write(source, text);
    return tool_run(command, directory, log_path);
}

static int reflectf(const char *format, const char *argument)
{
    char text[2048];
    int size = snprintf(text, sizeof text, format, argument);
    assert(size > 0 && size < (int)sizeof text);
    return reflect(text);
}

static identity_row *read_rows()
{
    char *text = tool_read(manifest);
    assert(text);
    identity_row *rows = NULL;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        if (line[0] == '#') {
            continue;
        }
        identity_row row;
        int count = sscanf(line, "%*s %*u %llu %127s %127s", &row.id, row.key, row.name);
        assert(count == 3);
        apush(rows, row);
    }
    free(text);
    return rows;
}

static bool generate(identity_row **rows, const char *declarations)
{
    afree(*rows);
    if (reflectf(COMPONENT "%s", declarations)) {
        return false;
    }
    *rows = read_rows();
    return true;
}

static const identity_row *find(identity_row *rows, const char *name)
{
    for (int i = 0; i < acount(rows); i++) {
        if (!strcmp(rows[i].name, name)) {
            return &rows[i];
        }
    }
    return NULL;
}

static bool same(identity_row *a, const char *a_name, identity_row *b, const char *b_name)
{
    const identity_row *left = find(a, a_name);
    const identity_row *right = find(b, b_name);
    return left && right && left->id == right->id && !strcmp(left->key, right->key);
}

static bool unique(identity_row *rows)
{
    for (int i = 0; i < acount(rows); i++) {
        for (int j = i + 1; j < acount(rows); j++) {
            if (rows[i].id == rows[j].id) {
                return false;
            }
        }
    }
    return true;
}

static long long modified_ns(const char *file)
{
    struct stat info;
    assert(!stat(file, &info));
#ifdef __APPLE__
    return (long long)info.st_mtimespec.tv_sec * 1000000000LL +
           info.st_mtimespec.tv_nsec;
#else
    return (long long)info.st_mtim.tv_sec * 1000000000LL + info.st_mtim.tv_nsec;
#endif
}

static void snapshot(output_snapshot *before, const char *file)
{
    free(before->text);
    before->text = tool_read(file);
    assert(before->text);
    before->modified = modified_ns(file);
}

static bool unchanged(const output_snapshot *before, const char *file)
{
    char *text = tool_read(file);
    bool same_text = text && !strcmp(text, before->text);
    free(text);
    return same_text && modified_ns(file) == before->modified;
}

static bool linked(const char *file, const char *target)
{
    struct stat info;
    char actual[PATH_MAX];
    if (lstat(file, &info) || !S_ISLNK(info.st_mode)) {
        return false;
    }
    ssize_t size = readlink(file, actual, sizeof actual - 1);
    if (size < 0) {
        return false;
    }
    actual[size] = '\0';
    return !strcmp(actual, target);
}

static bool outputs_unchanged()
{
    return unchanged(&output_before, output) && unchanged(&manifest_before, manifest);
}

static bool links_unchanged()
{
    return linked(output, output_target) && linked(manifest, manifest_target) &&
           outputs_unchanged();
}

/* Replace each generated output with a link to a read-only reference, so an
 * unchanged run must leave the link, its target and its timestamp alone. */
static void link_read_only(const char *file, const char *target)
{
    assert(!rename(file, target));
    assert(!symlink(target, file));
    assert(!chmod(target, 0444));
}

static int reflect_concurrently()
{
    const char *shell[] = {
        "/bin/sh", "-c", CONCURRENT_SCRIPT, "concurrent", CONCURRENT_RUNS,
    };
    const char *full[sizeof shell / sizeof *shell + sizeof command / sizeof *command];
    int count = 0;
    for (int i = 0; i < (int)(sizeof shell / sizeof *shell); i++) {
        full[count++] = shell[i];
    }
    for (int i = 0; command[i]; i++) {
        full[count++] = command[i];
    }
    full[count] = NULL;
    return tool_run(full, directory, log_path);
}

static bool log_contains(const char *text)
{
    char *log = tool_read(log_path);
    bool found = log && strstr(log, text);
    free(log);
    return found;
}

static bool handle_pin(const char *text, const char *type)
{
    for (const char *pin = text; (pin = strstr(pin, HANDLE_PIN)); pin++) {
        const char *end = strchr(pin, '\n');
        const char *found = strstr(pin, type);
        if (found && (!end || found < end)) {
            return true;
        }
    }
    return false;
}

TEST_CASE(component_insertion_keeps_existing_identities)
{
    REQUIRE(generate(&original, HEALTH SPEED));
    REQUIRE(acount(original) == 2 && unique(original));
    REQUIRE(generate(&inserted, COLLIDER HEALTH SPEED));
    REQUIRE(acount(inserted) == 3 && unique(inserted));
    REQUIRE(same(inserted, "Health", original, "Health"));
    REQUIRE(same(inserted, "Speed", original, "Speed"));
    return true;
}

/* Blank lines move the renamed declaration, so only its layout identifies it. */
TEST_CASE(component_rename_keeps_its_identity)
{
    REQUIRE(generate(&latest, COLLIDER "\n\n" VITALITY SPEED));
    REQUIRE(acount(latest) == 3 && unique(latest));
    REQUIRE(same(latest, "Vitality", original, "Health"));
    REQUIRE(same(latest, "Speed", original, "Speed"));
    return true;
}

TEST_CASE(restored_component_name_reclaims_its_alias)
{
    REQUIRE(generate(&latest, COLLIDER SPEED HEALTH_MAXIMUM));
    REQUIRE(acount(latest) == 3 && unique(latest));
    REQUIRE(same(latest, "Health", original, "Health"));
    REQUIRE(same(latest, "Speed", original, "Speed"));
    REQUIRE(same(latest, "Collider", inserted, "Collider"));
    return true;
}

TEST_CASE(removed_component_drops_its_row)
{
    REQUIRE(generate(&latest, COLLIDER HEALTH_MAXIMUM));
    REQUIRE(acount(latest) == 2 && !find(latest, "Speed"));
    REQUIRE(same(latest, "Health", original, "Health"));
    REQUIRE(same(latest, "Collider", inserted, "Collider"));
    return true;
}

/* Pace takes Speed's layout after Speed's row is gone. */
TEST_CASE(new_component_does_not_inherit_a_removed_identity)
{
    REQUIRE(generate(&readded, COLLIDER HEALTH_MAXIMUM PACE));
    REQUIRE(acount(readded) == 3 && unique(readded));
    REQUIRE(find(readded, "Pace") && !same(readded, "Pace", original, "Speed"));
    REQUIRE(same(readded, "Health", original, "Health"));
    REQUIRE(same(readded, "Collider", inserted, "Collider"));
    return true;
}

/* Without a row, a component's identity derives from its key again. */
TEST_CASE(readded_component_regains_its_key_identity)
{
    REQUIRE(generate(&latest, COLLIDER HEALTH_MAXIMUM PACE SPEED));
    REQUIRE(acount(latest) == 4 && unique(latest));
    REQUIRE(same(latest, "Speed", original, "Speed"));
    REQUIRE(same(latest, "Pace", readded, "Pace"));
    REQUIRE(same(latest, "Health", original, "Health"));
    REQUIRE(same(latest, "Collider", inserted, "Collider"));
    return true;
}

TEST_CASE(unchanged_reflection_keeps_linked_outputs)
{
    link_read_only(output, output_target);
    link_read_only(manifest, manifest_target);
    snapshot(&output_before, output);
    snapshot(&manifest_before, manifest);
    REQUIRE(tool_run(command, directory, log_path) == 0);
    REQUIRE(links_unchanged());
    REQUIRE(reflect_concurrently() == 0);
    REQUIRE(links_unchanged());
    return true;
}

TEST_CASE(failed_reflection_keeps_linked_outputs)
{
    REQUIRE(reflect("this is invalid C") != 0);
    REQUIRE(links_unchanged());
    return true;
}

TEST_CASE(handle_pins_keep_their_nominal_type)
{
    REQUIRE(reflectf(HANDLE, "uint64_t") == 0);
    char *types = tool_read(output);
    bool nominal = types && handle_pin(types, "\"ecs.entity\"") &&
                   !handle_pin(types, "\"handle\"");
    free(types);
    REQUIRE(nominal);
    snapshot(&output_before, output);
    snapshot(&manifest_before, manifest);
    const char *storages[] = { "int", "void *" };
    for (int i = 0; i < (int)(sizeof storages / sizeof *storages); i++) {
        REQUIRE(reflectf(HANDLE, storages[i]) != 0);
        REQUIRE(outputs_unchanged());
    }
    return true;
}

TEST_CASE(numeric_fields_reflect_their_unit_and_range)
{
    REQUIRE(reflectf(COMPONENT PRESENTED, "float damage") == 0);
    char *types = tool_read(output);
    bool presented = types && strstr(types, "\"speed\", PIN_F32,") &&
                     strstr(types, "false, \"u/s\", 0, 2.5, true, 0, NULL, PIN_CONTAINER_SINGLE },") &&
                     strstr(types, "false, NULL, 0, 0, false, 0, NULL, PIN_CONTAINER_SINGLE },");
    free(types);
    REQUIRE(presented);
    snapshot(&output_before, output);
    snapshot(&manifest_before, manifest);
    const char *invalid[] = {
        "const char *name __attribute__((annotate(\"node:unit:u\")))",
        "float empty __attribute__((annotate(\"node:unit:\")))",
        "float inverted __attribute__((annotate(\"node:range:1 0\")))",
        "float suffixed __attribute__((annotate(\"node:range:0 1f\")))",
        "float single __attribute__((annotate(\"node:range:1\")))",
        "float endless __attribute__((annotate(\"node:range:-inf 0\")))",
        "int fractional __attribute__((annotate(\"node:range:0 1.5\")))",
        "int overflow __attribute__((annotate(\"node:range:0 2147483648\")))",
        ("float both __attribute__((annotate(\"node:range:0 1\"), "
         "annotate(\"node:range-hint:0 1\")))"),
        "float fallback __attribute__((annotate(\"node:range-default:1\")))",
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(reflectf(COMPONENT PRESENTED, invalid[i]) != 0);
        REQUIRE(outputs_unchanged());
    }
    return true;
}

TEST_CASE(numeric_range_hints_and_defaults_are_distinct)
{
    REQUIRE(
        reflectf(
            COMPONENT PRESENTED,
            "float hint __attribute__((annotate(\"node:range-hint:0 1\"))); "
            "float fallback __attribute__((annotate(\"node:range:0 1\"), "
            "annotate(\"node:range-default:0.5\")))"
        ) == 0
    );
    char *types = tool_read(output);
    bool distinct = types && strstr(types, "false, NULL, 0, 1, false, 0, NULL },") &&
                    strstr(types, "false, NULL, 0, 1, true, 0.5, NULL },");
    free(types);
    REQUIRE(distinct);
    return true;
}

TEST_CASE(jit_identity_requires_one_matching_struct_argument)
{
    REQUIRE(reflectf(JIT, "Alias convert(Vector value)") == 0);
    snapshot(&output_before, output);
    snapshot(&manifest_before, manifest);
    const char *signatures[] = { "Alias convert()",
                                 "Alias convert(float value)",
                                 "float convert(Vector value)",
                                 "Alias convert(Vector *value)",
                                 "Different convert(Vector value)",
                                 "Alias convert(Vector a, Vector b)" };
    for (int i = 0; i < (int)(sizeof signatures / sizeof *signatures); i++) {
        REQUIRE(reflectf(JIT, signatures[i]) != 0);
        REQUIRE(log_contains(JIT_REJECTION));
        REQUIRE(outputs_unchanged());
    }
    return true;
}

/* A derived component names its source's ID, and scripts get no way to
 * write, add or remove it. */
TEST_CASE(derived_component_names_its_source)
{
    REQUIRE(generate(&latest, COLLIDER SPEED));
    REQUIRE(reflectf(COMPONENT SPEED DERIVED, "Speed") == 0);
    char *types = tool_read(output), *capabilities = tool_read(runtime);
    char expected[128];
    snprintf(
        expected,
        sizeof expected,
        ".derived_from = UINT64_C(%llu)",
        find(latest, "Speed")->id
    );
    bool derived = types && strstr(types, expected) && capabilities &&
                   strstr(capabilities, ".derived = true") &&
                   strstr(capabilities, "\"Get_Total\"") &&
                   !strstr(capabilities, "\"Set_Total") &&
                   !strstr(capabilities, "\"Add_Total\"") &&
                   !strstr(capabilities, "\"Remove_Total\"");
    free(types);
    free(capabilities);
    REQUIRE(derived);
    snapshot(&output_before, output);
    snapshot(&manifest_before, manifest);
    REQUIRE(reflectf(COMPONENT DERIVED, "Missing") != 0);
    REQUIRE(log_contains("Total derives from Missing, which is not a component"));
    REQUIRE(outputs_unchanged());
    return true;
}

TEST_CASE(empty_prefix_generates_canonical_component_names)
{
    const char *const unprefixed[] = {
        CHIBI_REFLECT_PATH, source,  "--prefix", "", "--types-output", output,
        "--runtime-output", runtime, NULL
    };
    tool_write(source, COMPONENT HEALTH);
    REQUIRE(tool_run(unprefixed, directory, log_path) == 0);
    char *types = tool_read(output), *capabilities = tool_read(runtime);
    bool canonical = types && capabilities &&
                     strstr(types, "const type_desc T_Health =") &&
                     strstr(types, "const ecs_component_desc COMPONENT_Health =") &&
                     strstr(types, ".key = \"Health\"") &&
                     strstr(types, "Health *Health_get(") &&
                     strstr(capabilities, "const native_capability_pack NATIVE_PACK =") &&
                     strstr(capabilities, "native_bool_result[]") &&
                     strstr(capabilities, ".results = native_bool_result,") &&
                     strstr(capabilities, ".source_name = \"Health\"") &&
                     !strstr(types, "_COMPONENT_Health") &&
                     !strstr(types, "_T_Health") &&
                     !strstr(capabilities, "_NATIVE_PACK");
    free(types);
    free(capabilities);
    REQUIRE(canonical);
    return true;
}

static void begin()
{
    strcpy(directory, "/tmp/chibi-reflect-test-XXXXXX");
    assert(mkdtemp(directory));
    path(source, "components.c");
    path(output, "types.c");
    path(runtime, "capabilities.c");
    path(manifest, "components.tsv");
    path(log_path, "reflect.log");
    path(output_target, "types.c.stable");
    path(manifest_target, "components.tsv.stable");
}

static void end()
{
    afree(original);
    afree(inserted);
    afree(readded);
    afree(latest);
    free(output_before.text);
    free(manifest_before.text);
    tool_remove_tree(directory);
}

void test_tool_reflect_ranges()
{
    begin();
    RUN_TEST_CASE(numeric_fields_reflect_their_unit_and_range);
    RUN_TEST_CASE(numeric_range_hints_and_defaults_are_distinct);
    end();
}

void test_tool_reflect()
{
    begin();
    RUN_TEST_CASE(component_insertion_keeps_existing_identities);
    RUN_TEST_CASE(component_rename_keeps_its_identity);
    RUN_TEST_CASE(restored_component_name_reclaims_its_alias);
    RUN_TEST_CASE(removed_component_drops_its_row);
    RUN_TEST_CASE(new_component_does_not_inherit_a_removed_identity);
    RUN_TEST_CASE(readded_component_regains_its_key_identity);
    RUN_TEST_CASE(unchanged_reflection_keeps_linked_outputs);
    RUN_TEST_CASE(failed_reflection_keeps_linked_outputs);
    RUN_TEST_CASE(handle_pins_keep_their_nominal_type);
    RUN_TEST_CASE(numeric_fields_reflect_their_unit_and_range);
    RUN_TEST_CASE(numeric_range_hints_and_defaults_are_distinct);
    RUN_TEST_CASE(jit_identity_requires_one_matching_struct_argument);
    RUN_TEST_CASE(derived_component_names_its_source);
    RUN_TEST_CASE(empty_prefix_generates_canonical_component_names);
    end();
}
