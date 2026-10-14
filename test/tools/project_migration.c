#include "authoring/project_migration.h"

#include "assets/project.h"
#include "assets/project_migrate.h"
#include "authoring/source_metadata.h"
#include "language/syntax.h"
#include "support/rooted_files.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char root[128], source[256];
static const char
    *old_source = "library Damage { pure function DamageFor(level: int) -> "
                  "(Result: int) { return level } }\n";
static void setup()
{
    strcpy(root, "/tmp/chibi-migration-copy-XXXXXX");
    assert(mkdtemp(root));
    snprintf(source, sizeof source, "%s/original", root);
    assert(!mkdir(source, 0700));
    char path[512];
    snprintf(path, sizeof path, "%s/content", source);
    assert(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/project.kdl", source);
    tool_write(path, "version 23\nname \"Old\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n");
    snprintf(path, sizeof path, "%s/content/Damage.chibi", source);
    tool_write(path, old_source);
}
static void teardown()
{
    tool_remove_tree(root);
}
TEST_CASE(migration_copy_preserves_original_and_runs_real_transform)
{
    int version = 0;
    char error[512], path[512];
    char *copy = NULL;
    REQUIRE(project_migration_version(source, &version, error, sizeof error));
    REQUIRE(version == 23);
    REQUIRE(project_migration_copy(source, root, "upgraded", &copy, error, sizeof error));
    REQUIRE(project_migration_version(copy, &version, error, sizeof error));
    REQUIRE(version == CHIBI_PROJECT_VERSION);
    snprintf(path, sizeof path, "%s/content/Damage.chibi", copy);
    char *text = tool_read(path);
    REQUIRE(text && strstr(text, "DamageFor :: pure fn"));
    free(text);
    snprintf(path, sizeof path, "%s/content/Damage.chibi", source);
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, old_source));
    free(text);
    REQUIRE(project_migration_version(source, &version, error, sizeof error));
    REQUIRE(version == 23);
    snprintf(path, sizeof path, "%s/.chibi/migration-report.txt", copy);
    text = tool_read(path);
    REQUIRE(text && strstr(text, "Original project unchanged"));
    REQUIRE(strstr(text, "Source files SHA256:") && strstr(text, "Result files SHA256:"));
    char *again = NULL;
    REQUIRE(project_migration_copy(source, root, "upgraded-again", &again, error, sizeof error));
    snprintf(path, sizeof path, "%s/.chibi/migration-report.txt", again);
    char *second = tool_read(path);
    REQUIRE(second && !strcmp(text, second));
    free(second);
    free(again);
    free(text);
    free(copy);
    return true;
}
TEST_CASE(migration_copy_refuses_collision_external_and_failed_source_without_changes)
{
    char error[512], path[512];
    char *copy = NULL;
    int version = 0;
    REQUIRE(!project_migration_copy(source, root, "original", &copy, error, sizeof error));
    REQUIRE(!copy);
    REQUIRE(!project_migration_copy(source, source, "nested", &copy, error, sizeof error));
    REQUIRE(!copy);
    snprintf(path, sizeof path, "%s/content/Broken.chibi", source);
    tool_write(path, "library Broken { function");
    REQUIRE(!project_migration_copy(source, root, "failed", &copy, error, sizeof error));
    REQUIRE(!copy);
    snprintf(path, sizeof path, "%s/failed", root);
    REQUIRE(access(path, F_OK) != 0);
    REQUIRE(project_migration_version(source, &version, error, sizeof error));
    REQUIRE(version == 23);
    snprintf(path, sizeof path, "%s/project.kdl", source);
    tool_write(path, "version 23\ncontent \"../outside\"\nstart_scene \"scenes/Start.kdl\"\n");
    REQUIRE(!project_migration_copy(source, root, "external", &copy, error, sizeof error));
    REQUIRE(!copy);
    snprintf(path, sizeof path, "%s/external", root);
    REQUIRE(access(path, F_OK) != 0);
    return true;
}
static const char
    *field_source = "behavior Fields {\n"
                    "// state and self.Count remain here and in strings.\n"
                    "state /* keep */ Count: int = 2;\n"
                    "state \"Display Value\": int = 3;\n"
                    "Update :: fn(Count: int) {\n"
                    "state int Local = 4;\n"
                    "state.Local += 1; state.Count += state.Local;\n"
                    "self.Count = Count; self.Local = 1;\n"
                    "}\n"
                    "on Tick { state.Count += 1; label := \"state "
                    "self.Count\"; self.Local = 1; }\n"
                    "}\n";
TEST_CASE(migration_fields_preserve_callable_storage_and_entity_collisions)
{
    char error[256] = "", *migrated = NULL, *again = NULL;
    REQUIRE(!chibi_migrate_fields(field_source, &migrated, error, sizeof error));
    REQUIRE(migrated);
    REQUIRE(strstr(migrated, "/* keep */ Count: int = 2;"));
    REQUIRE(strstr(migrated, "\"Display Value\": int = 3;"));
    REQUIRE(strstr(migrated, "Local: int = 4;"));
    REQUIRE(strstr(migrated, "self.Local += 1; self.Count += self.Local;"));
    REQUIRE(strstr(migrated, "cast<entity>(self).Count = Count;"));
    REQUIRE(strstr(migrated, "cast<entity>(self).Local = 1;"));
    REQUIRE(strstr(migrated, "\"state self.Count\"; self.Local = 1;"));
    REQUIRE(strstr(migrated, "// state and self.Count remain here and in strings."));
    script_syntax *syntax = script_parse(migrated);
    REQUIRE(!syntax->diagnostic_count);
    script_syntax_free(syntax);
    REQUIRE(!chibi_migrate_fields(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    free(migrated);
    const char *shared = "system Counters() { shared Total: int = 0; state "
                         "Value := 1; }";
    REQUIRE(!chibi_migrate_fields(shared, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "shared Total: int = 0;"));
    free(migrated);
    const char
        *plain = "behavior Plain { Count := 0; on Tick { self.Count += 1; } }";
    REQUIRE(!chibi_migrate_fields(plain, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "cast<entity>(self).Count += 1;"));
    REQUIRE(!chibi_migrate_fields(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    free(migrated);
    const char *root_only = "behavior Root { state Count := 0; on Tick { "
                            "state.Count += 1; } }";
    REQUIRE(!chibi_migrate_fields(root_only, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "self.Count += 1;"));
    REQUIRE(strstr(migrated, "self.Count := 0;"));
    REQUIRE(!chibi_migrate_fields(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    free(migrated);
    REQUIRE(chibi_migrate_fields("behavior Bad { state", &migrated, error, sizeof error) < 0);
    REQUIRE(!migrated && strstr(error, "invalid version 24 source at 1:"));
    const char *shadowed = "behavior Shadow { state Count := 0; Work :: "
                           "fn(\"self\": entity) { self.Count = 1; } }";
    REQUIRE(!chibi_migrate_fields(shadowed, &migrated, error, sizeof error));
    REQUIRE(migrated && strstr(migrated, "self.Count = 1;") && !strstr(migrated, "cast<entity>"));
    free(migrated);
    const char *conflict = "behavior Shadow { state Count := 0; Work :: "
                           "fn(\"self\": entity) { state.Count = 1; } }";
    REQUIRE(chibi_migrate_fields(conflict, &migrated, error, sizeof error) < 0);
    REQUIRE(!migrated && strstr(error, "self is shadowed"));
    return true;
}
TEST_CASE(migration_fields_preserve_source_bound_anchors_and_fail_atomically)
{
    char path[512], error[512] = "", json[512];
    snprintf(path, sizeof path, "%s/content/Fields.chibi", source);
    tool_write(path, field_source);
    snprintf(
        json,
        sizeof json,
        "{\"version\":2,\"anchors\":[{\"start\":%zu,\"id\":73,\"kind\":"
        "\"node\"}],"
        "\"positions\":[{\"id\":73,\"x\":10,\"y\":20}]}",
        (size_t)(strstr(field_source, "state /* keep */") - field_source)
    );
    char *metadata = authoring_source_metadata_bind(json, field_source);
    REQUIRE(metadata);
    snprintf(path, sizeof path, "%s/content/Fields.chibi.meta", source);
    tool_write(path, metadata);
    free(metadata);
    /* The setup's old23 library must run its own historical step first. */
    snprintf(path, sizeof path, "%s/content/Damage.chibi", source);
    char *damage = NULL;
    REQUIRE(!chibi_migrate_declarations(old_source, &damage, error, sizeof error));
    REQUIRE(damage);
    tool_write(path, damage);
    free(damage);
    snprintf(path, sizeof path, "%s/content/Broken.chibi", source);
    tool_write(path, "behavior Broken { state");
    snprintf(path, sizeof path, "%s/content", source);
    REQUIRE(chibi_project_migrate_content(path, NULL, 24, error, sizeof error) < 0);
    snprintf(path, sizeof path, "%s/content/Fields.chibi", source);
    char *text = tool_read(path);
    REQUIRE(text && !strcmp(text, field_source));
    free(text);
    snprintf(path, sizeof path, "%s/content/Broken.chibi", source);
    REQUIRE(!unlink(path));
    snprintf(path, sizeof path, "%s/content", source);
    REQUIRE(!chibi_project_migrate_content(path, NULL, 24, error, sizeof error));
    snprintf(path, sizeof path, "%s/content/Fields.chibi", source);
    text = tool_read(path);
    REQUIRE(text && strcmp(text, field_source));
    snprintf(path, sizeof path, "%s/content/Fields.chibi.meta", source);
    metadata = tool_read(path);
    REQUIRE(metadata && authoring_source_metadata_matches(metadata, text));
    snprintf(json, sizeof json, "\"start\":%zu", (size_t)(strstr(text, "Count: int") - text));
    REQUIRE(strstr(metadata, json));
    REQUIRE(strstr(metadata, "\"id\":73") && strstr(metadata, "\"x\":10"));
    snprintf(path, sizeof path, "%s/content", source);
    REQUIRE(!chibi_project_migrate_content(path, NULL, 24, error, sizeof error));
    snprintf(path, sizeof path, "%s/content/Fields.chibi", source);
    char *second = tool_read(path);
    REQUIRE(second && !strcmp(second, text));
    free(second);
    free(metadata);
    free(text);
    return true;
}
TEST_SUITE(test_tool_project_migration)
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(migration_copy_preserves_original_and_runs_real_transform);
    RUN_TEST_CASE(migration_copy_refuses_collision_external_and_failed_source_without_changes);
    RUN_TEST_CASE(migration_fields_preserve_callable_storage_and_entity_collisions);
    RUN_TEST_CASE(migration_fields_preserve_source_bound_anchors_and_fail_atomically);
    pu_clear_setup();
}
