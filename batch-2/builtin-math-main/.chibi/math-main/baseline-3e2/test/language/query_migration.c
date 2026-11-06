#include "assets/project_migrate.h"
#include "language/syntax.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char project_root[128], source_path[192], refused_path[192];
static void cleanup()
{
    if (project_root[0]) {
        unlink(source_path);
        unlink(refused_path);
        rmdir(project_root);
        project_root[0] = '\0';
    }
}
static bool write_source(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool written = fwrite(text, 1, strlen(text), file) == strlen(text);
    return !fclose(file) && written;
}
static bool source_contains(const char *path, const char *text)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char bytes[512];
    size_t count = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[count] = '\0';
    fclose(file);
    return strstr(bytes, text) != NULL;
}

TEST_CASE(query_migration_preserves_live_bindings_and_step_reads)
{
    const char
        *source = "DamageTargets :: system(damage: Damage, armor: Armor?,) "
                  "join target in self.Targets join health: Health from target "
                  "without Frozen, Disabled where health.value > 0 "
                  "on Tick :: (dt: number s) { "
                  "health.value -= damage.amount * dt; "
                  "Read :: (value: Damage) -> Damage { return value } "
                  "on Tick :: (dt: number s) { local_dt := dt } }";
    char error[512] = { 0 }, *migrated = NULL;
    int status = chibi_migrate37_queries(source, &migrated, error, sizeof error);
    if (status) {
        fprintf(stderr, "%s\n", error);
    }
    REQUIRE(!status && migrated);
    REQUIRE(strstr(migrated, "damage: ^Damage"));
    REQUIRE(strstr(migrated, "armor: ^Armor?"));
    REQUIRE(strstr(migrated, "target in self.Targets"));
    REQUIRE(strstr(migrated, "health: ^Health from target"));
    REQUIRE(strstr(migrated, "if ! Frozen && ! Disabled"));
    REQUIRE(strstr(migrated, "if health.value > 0)"));
    REQUIRE(strstr(migrated, "health.value -= damage.amount * dt"));
    REQUIRE(strstr(migrated, "Read :: (value: Damage) -> Damage"));
    REQUIRE(strstr(migrated, "on Tick :: (dt: number s) { local_dt := dt }"));
    char *again = NULL;
    REQUIRE(!chibi_migrate37_queries(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    free(migrated);
    return true;
}

TEST_CASE(query_migration_keeps_comments_literals_and_empty_roots)
{
    const char *source =
        "S :: system( /* parameters */ ) /* generator */ join target in "
        "self.Targets "
        "join value: /* component */ Health from target, /* filter */ "
        "without Frozen, /* another */ Disabled, where value.value > 0 "
        "on /* event */ Tick :: ( /* payload */ dt: number s /* seconds */ ) { "
        "text := \"join without where on Tick\"; value.value -= dt }";
    char error[512] = { 0 }, *migrated = NULL;
    int status = chibi_migrate37_queries(source, &migrated, error, sizeof error);
    if (status) {
        fprintf(stderr, "%s\n", error);
    }
    REQUIRE(!status && migrated);
    const char *comments[] = {
        "/* parameters */", "/* generator */", "/* component */",
        "/* filter */",     "/* another */",   "/* event */",
        "/* payload */",    "/* seconds */",
    };
    for (size_t i = 0; i < sizeof comments / sizeof *comments; i++) {
        REQUIRE(strstr(migrated, comments[i]));
    }
    REQUIRE(strstr(migrated, "\"join without where on Tick\""));
    REQUIRE(strstr(migrated, "value.value -= dt"));
    script_syntax *syntax = script_parse(migrated);
    REQUIRE(!syntax->diagnostic_count);
    script_syntax_free(syntax);
    free(migrated);
    return true;
}

TEST_CASE(query_migration_is_idempotent_and_refuses_unsafe_sources)
{
    const char *current[] = {
        "S :: system(value: ^Health) { value.value -= dt }",
        "S :: system(value: Health, target: entity in self.Targets, "
        "health: ^Health from target, if health.value > 0, amount := "
        "value.value) {}",
        "B :: behavior { on Tick :: (dt: number s) { step := dt } }",
        "S :: system() {}",
        "L :: library { Read :: (value: Health split self) {} }",
    };
    char error[512] = { 0 }, *migrated = NULL;
    for (size_t i = 0; i < sizeof current / sizeof *current; i++) {
        REQUIRE(!chibi_migrate37_queries(current[i], &migrated, error, sizeof error));
        REQUIRE(!migrated);
    }
    const struct
    {
        const char *source, *message;
    } refused[] = {
        { "S :: system(value: Health) { query !Frozen; }", "removed query declaration" },
        { "S :: system(value) {}", "untyped component binding" },
        { "S :: system(value: Health) on Begin :: () {}", "not Tick" },
        { "S :: system(value: Health) on Tick :: (other: int) {}", "other than dt" },
        { "S :: system(value: Health?) { copy := value }",
          "optional component used as a whole value" },
        { "S :: system(value: Targets.Health from self) {}", "both present" },
        { "S :: system(value: Targets.Parent.Health) {}", "ambiguous qualified" },
        { "S :: system(", "invalid version 37 source" },
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        REQUIRE(chibi_migrate37_queries(refused[i].source, &migrated, error, sizeof error) < 0);
        REQUIRE(!migrated && strstr(error, refused[i].message));
    }
    return true;
}

TEST_CASE(query_migration_expands_compact_providers_and_preserves_imports)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "S :: system(aim: Targets.Position?) { if aim { aim.x = 1 } }",
          "aim: ^Position? from Targets" },
        { "S :: system(aim: ^Targets.Position?) { if aim { aim.x = 1 } }",
          "aim: ^Position? from Targets" },
        { "S :: system(aim: ^Targets.Position?, if aim) { aim.x = 1 }",
          "aim: ^Position? from Targets" },
        { "S :: system(aim: Position split self) {}", "aim: ^Position from self" },
        { "S :: system(aim: ^Position split self) {}", "aim: ^Position from self" },
        { "B :: behavior { on Begin :: () { matches := query(aim: "
          "^Targets.Position?, if aim) } }",
          "aim: ^Position? from Targets" },
        { "S :: system(aim: Targets /* provider */ . /* component */ "
          "Position?) {}",
          "/* provider */  /* component */ Position? from Targets" },
        { "package Demo; import pkg \"components\"; S :: system(value: "
          "pkg.Health) {}",
          "value: ^pkg.Health" },
    };
    char error[512] = { 0 }, *migrated = NULL, *again = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        int status = chibi_migrate37_queries(cases[i].source, &migrated, error, sizeof error);
        if (status || !migrated || !strstr(migrated, cases[i].expected)) {
            fprintf(stderr, "provider case %zu: %s: %s\n", i, error, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(!status && migrated && strstr(migrated, cases[i].expected));
        REQUIRE(!chibi_migrate37_queries(migrated, &again, error, sizeof error));
        REQUIRE(!again);
        free(migrated);
        migrated = NULL;
    }
    return true;
}

TEST_CASE(query_migration_dereferences_whole_values_without_capturing_shadows)
{
    const char *source =
        "S :: system(position: Position, velocity: Velocity, optional: "
        "Health?) "
        "where changed(position) && optional != nil { "
        "position += velocity * dt; copy := position; Consume(position); "
        "Mutate(&position); snapshot: Position = {value: position}; "
        "position.value = velocity.value; "
        "if optional { optional.value = 1 }  "
        "Read :: (position: Position) -> Position { return position }; "
        "Nested :: () { position: Position = {}; Consume(position) }; "
        "for position in 0..3 { Consume(position) } "
        "Consume(position); position: Position = {}; Consume(position) }";
    char error[512] = { 0 }, *migrated = NULL;
    int status = chibi_migrate37_queries(source, &migrated, error, sizeof error);
    if (status) {
        fprintf(stderr, "%s\n", error);
    }
    REQUIRE(!status && migrated);
    REQUIRE(strstr(migrated, "changed(position) && optional != nil"));
    REQUIRE(strstr(migrated, "position^ += velocity^ * dt"));
    REQUIRE(strstr(migrated, "copy := position^; Consume(position^)"));
    REQUIRE(strstr(migrated, "Mutate(position)"));
    REQUIRE(strstr(migrated, "snapshot: Position = {value: position^}"));
    REQUIRE(strstr(migrated, "position.value = velocity.value"));
    REQUIRE(strstr(migrated, "if optional { optional.value = 1 }"));
    REQUIRE(strstr(migrated, "Read :: (position: Position) -> Position { return position }"));
    REQUIRE(strstr(migrated, "position: Position = {}; Consume(position)"));
    REQUIRE(strstr(migrated, "for position in 0..3 { Consume(position) }"));
    REQUIRE(strstr(migrated, "Consume(position^); position: Position = {}; Consume(position)"));
    free(migrated);
    return true;
}

TEST_CASE(query_migration_visits_nested_provider_queries)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "B :: behavior { on Tick :: () { q := query(p: Position, nested := "
          "query(aim: ^Targets.Position)); } }",
          "B :: behavior { on Tick :: () { q := query(p: Position, nested := "
          "query(aim: ^Position from Targets)); } }" },
        { "S :: system(p: Position, if Ready(query(aim: ^Targets.Position?))) "
          "{}",
          "S :: system(p: Position, if Ready(query(aim: ^Position? from "
          "Targets))) {}" },
    };
    char error[512] = { 0 };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char *migrated = NULL, *again = NULL;
        int status = chibi_migrate37_queries(cases[i].source, &migrated, error, sizeof error);
        if (status || !migrated || strcmp(migrated, cases[i].expected)) {
            fprintf(stderr, "nested provider case %zu: %s: %s\n", i, error, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(!status && migrated);
        REQUIRE(!strcmp(migrated, cases[i].expected));
        REQUIRE(!chibi_migrate37_queries(migrated, &again, error, sizeof error));
        REQUIRE(!again);
        free(migrated);
    }
    return true;
}

TEST_CASE(query_migration_keeps_outer_reads_in_local_initializers)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "S :: system(p: Position) { if true { p := p; Consume(p); } "
          "Consume(p); }",
          "S :: system(p: ^Position) { if true { p := p^; Consume(p); } "
          "Consume(p^); }" },
        { "S :: system(p: Position) { if true { p: Position = p; Consume(p); } "
          "Consume(p); }",
          "S :: system(p: ^Position) { if true { p: Position = p^; Consume(p); "
          "} Consume(p^); }" },
    };
    char error[512] = { 0 };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char *migrated = NULL, *again = NULL;
        int status = chibi_migrate37_queries(cases[i].source, &migrated, error, sizeof error);
        if (status || !migrated || strcmp(migrated, cases[i].expected)) {
            fprintf(stderr, "initializer case %zu: %s: %s\n", i, error, migrated ? migrated : "(unchanged)");
        }
        REQUIRE(!status && migrated);
        REQUIRE(!strcmp(migrated, cases[i].expected));
        REQUIRE(!chibi_migrate37_queries(migrated, &again, error, sizeof error));
        REQUIRE(!again);
        free(migrated);
    }
    return true;
}

TEST_CASE(query_migration_stages_project_sources_atomically)
{
    snprintf(project_root, sizeof project_root, "/tmp/chibi-query-migration-XXXXXX");
    REQUIRE(mkdtemp(project_root));
    snprintf(source_path, sizeof source_path, "%s/Good.chibi", project_root);
    snprintf(refused_path, sizeof refused_path, "%s/Refused.chibi", project_root);
    const char
        *source = "S :: system(health: Health) on Tick :: (dt: number s) "
                  "{ health.value -= dt }";
    REQUIRE(write_source(source_path, source));
    REQUIRE(write_source(refused_path, "S :: system() { query !Frozen; }"));
    char error[512] = { 0 };
    REQUIRE(chibi_project_migrate_content(project_root, NULL, 37, error, sizeof error) < 0);
    REQUIRE(strstr(error, "Refused.chibi") && strstr(error, "removed query declaration"));
    REQUIRE(source_contains(source_path, source));
    REQUIRE(write_source(refused_path, "S :: system() {}"));
    REQUIRE(!chibi_project_migrate_content(project_root, NULL, 37, error, sizeof error));
    REQUIRE(source_contains(source_path, "health: ^Health"));
    REQUIRE(!source_contains(source_path, "on Tick"));
    REQUIRE(!chibi_project_migrate_content(project_root, NULL, 37, error, sizeof error));
    REQUIRE(source_contains(source_path, "health: ^Health"));
    return true;
}

void test_language_query_migration()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(query_migration_preserves_live_bindings_and_step_reads);
    RUN_TEST_CASE(query_migration_keeps_comments_literals_and_empty_roots);
    RUN_TEST_CASE(query_migration_is_idempotent_and_refuses_unsafe_sources);
    RUN_TEST_CASE(query_migration_expands_compact_providers_and_preserves_imports);
    RUN_TEST_CASE(query_migration_dereferences_whole_values_without_capturing_shadows);
    RUN_TEST_CASE(query_migration_visits_nested_provider_queries);
    RUN_TEST_CASE(query_migration_keeps_outer_reads_in_local_initializers);
    RUN_TEST_CASE(query_migration_stages_project_sources_atomically);
    pu_clear_setup();
}
