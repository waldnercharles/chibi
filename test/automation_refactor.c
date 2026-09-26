#include "automation/controller.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void test_check(int ok, const char *message);

#define SOURCE "projects/arena2d/content"

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

/* A temporary copy of a project's content. */
static void copy_project(char *root)
{
    strcpy(root, "/tmp/chibi-refactor-XXXXXX");
    assert(mkdtemp(root));
    char content[PATH_MAX];
    snprintf(content, sizeof content, "%s/content", root);
    assert(mkdir(content, 0755) == 0);
    DIR *folders = opendir(SOURCE);
    struct dirent *folder;
    while ((folder = readdir(folders))) {
        if (folder->d_name[0] == '.') {
            continue;
        }
        char from[PATH_MAX], to[PATH_MAX];
        snprintf(from, sizeof from, SOURCE "/%s", folder->d_name);
        snprintf(to, sizeof to, "%s/%s", content, folder->d_name);
        assert(mkdir(to, 0755) == 0);
        DIR *files = opendir(from);
        struct dirent *file;
        while ((file = readdir(files))) {
            if (file->d_name[0] == '.') {
                continue;
            }
            char source[PATH_MAX + 256], target[PATH_MAX + 256];
            snprintf(source, sizeof source, "%s/%s", from, file->d_name);
            snprintf(target, sizeof target, "%s/%s", to, file->d_name);
            char *text = text_file_read(source);
            assert(text);
            write_text(target, text);
            free(text);
        }
        closedir(files);
    }
    closedir(folders);
}

static void remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *item;
    while (directory && (item = readdir(directory))) {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) {
            continue;
        }
        char child[PATH_MAX + 256];
        snprintf(child, sizeof child, "%s/%s", path, item->d_name);
        struct stat info;
        assert(lstat(child, &info) == 0);
        if (S_ISDIR(info.st_mode)) {
            remove_tree(child);
        } else {
            assert(unlink(child) == 0);
        }
    }
    if (directory) {
        closedir(directory);
    }
    assert(rmdir(path) == 0);
}

typedef struct
{
    char *text;
    yyjson_doc *doc;
    yyjson_val *result;
} reply;

static reply ask(const char *root, const char *format, const char *extra)
{
    char request[4096];
    snprintf(request, sizeof request, format, extra ? extra : "");
    int exit_code = 0;
    reply r = { .text = chibi_automation_handle(request, root, &exit_code) };
    r.doc = yyjson_read(r.text, strlen(r.text), 0);
    r.result = yyjson_obj_get(yyjson_doc_get_root(r.doc), "result");
    return r;
}

static void reply_free(reply *r)
{
    yyjson_doc_free(r->doc);
    free(r->text);
}

static bool ok(const reply *r)
{
    return yyjson_get_bool(yyjson_obj_get(yyjson_doc_get_root(r->doc), "ok"));
}

static const char *error_code(const reply *r)
{
    return yyjson_get_str(
        yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(r->doc), "error"), "code")
    );
}

static size_t count(const reply *r, const char *key)
{
    return yyjson_arr_size(yyjson_obj_get(r->result, key));
}

/* The dry run's expected_hashes, as JSON to pass back. */
static char *hashes(const reply *r)
{
    return yyjson_val_write(yyjson_obj_get(r->result, "expected_hashes"), 0, NULL);
}

static bool exists(const char *root, const char *path)
{
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", root, path);
    struct stat info;
    return stat(full, &info) == 0;
}

static bool holds(const char *root, const char *path, const char *text)
{
    char full[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", root, path);
    char *contents = text_file_read(full);
    bool found = contents && strstr(contents, text);
    free(contents);
    return found;
}

#define RENAME_DAMAGE                                                          \
    "{\"contract\":1,\"operation\":\"rename_symbol\",\"event\":\"Damage\","    \
    "\"new_name\":\"Harm\"%s}"

/* A dry run lists every asset and change; publishing needs its hashes. */
static void renames(const char *root)
{
    reply dry = ask(root, RENAME_DAMAGE, NULL);
    bool listed = ok(&dry) && count(&dry, "assets") == 3 &&
                  yyjson_obj_size(yyjson_obj_get(dry.result, "expected_hashes")) == 3 &&
                  exists(root, "content/events/Damage.yaml");
    reply unhashed = ask(root, RENAME_DAMAGE, ",\"dry_run\":false");
    char *expected = hashes(&dry);
    char extra[2048];
    snprintf(extra, sizeof extra, ",\"dry_run\":false,\"expected_hashes\":%s", expected);
    reply applied = ask(root, RENAME_DAMAGE, extra);
    bool published = ok(&applied) &&
                     yyjson_get_bool(yyjson_obj_get(applied.result, "published")) &&
                     exists(root, "content/events/Harm.yaml") &&
                     !exists(root, "content/events/Damage.yaml") &&
                     holds(root, "content/behaviors/HullDamage.yaml", "reference_name: Harm");
    reply valid = ask(root, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"behavior\",\"name\":\"ProjectileHit\"%s}", NULL);
    test_check(listed && !ok(&unhashed) && published && ok(&valid), "renameSymbol dry-runs, then publishes every asset with the dry run's hashes");
    free(expected);
    reply_free(&dry);
    reply_free(&unhashed);
    reply_free(&applied);
    reply_free(&valid);

    const char *parameter = "{\"contract\":1,\"operation\":\"rename_symbol\","
                            "\"asset\":{\"kind\":\"library\",\"name\":"
                            "\"WeaponActions\"},"
                            "\"graph\":\"Spawn Prototype "
                            "At\",\"parameter\":\"Prototype\",\"new_name\":"
                            "\"Template\"%s}";
    dry = ask(root, parameter, NULL);
    bool calls = ok(&dry) && strstr(dry.text, "content/behaviors/HullDamage.yaml");
    reply component = ask(root, "{\"contract\":1,\"operation\":\"rename_symbol\",\"component\":\"GameHealth\",\"new_name\":\"Vitality\"%s}", NULL);
    test_check(
        calls && !ok(&component) && !strcmp(error_code(&component), "invalid_request"),
        "renameSymbol selects parameters of functions and refuses components"
    );
    reply_free(&dry);
    reply_free(&component);
}

#define RENAME_PURSUIT                                                         \
    "{\"contract\":1,\"operation\":\"rename_symbol\",\"asset\":{\"kind\":"     \
    "\"behavior\",\"name\":\"Pursuit\"},\"new_name\":\"Chase\"%s}"

/* An asset that changed after the dry run publishes nothing. */
static void conflicts(const char *root)
{
    reply dry = ask(root, RENAME_PURSUIT, NULL);
    char *expected = hashes(&dry);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/content/prototypes/Swarmer.yaml", root);
    char *swarmer = text_file_read(path);
    size_t size = strlen(swarmer) + 2;
    char *edited = malloc(size);
    snprintf(edited, size, "%s\n", swarmer);
    write_text(path, edited);
    char extra[2048];
    snprintf(extra, sizeof extra, ",\"dry_run\":false,\"expected_hashes\":%s", expected);
    reply stale = ask(root, RENAME_PURSUIT, extra);
    test_check(
        !ok(&stale) && !strcmp(error_code(&stale), "asset_conflict") &&
            exists(root, "content/behaviors/Pursuit.yaml") &&
            !exists(root, "content/behaviors/Chase.yaml") &&
            holds(root, "content/scenes/Sandbox.yaml", "Pursuit"),
        "renameSymbol publishes nothing when an asset changed since the dry run"
    );
    write_text(path, swarmer);
    free(swarmer);
    free(edited);
    free(expected);
    reply_free(&dry);
    reply_free(&stale);
}

/* A dry run refuses what could never publish: a case-only rename, which file
   systems that ignore case see as the file itself, and names no file takes. */
static void refusals(const char *root)
{
    reply cased = ask(root, "{\"contract\":1,\"operation\":\"rename_symbol\",\"asset\":{\"kind\":\"behavior\",\"name\":\"Pursuit\"},\"new_name\":\"pursuit\"%s}", NULL);
    reply unsafe = ask(root, "{\"contract\":1,\"operation\":\"rename_symbol\",\"event\":\"Harm\",\"new_name\":\"../Harm\"%s}", NULL);
    test_check(
        !ok(&cased) && !strcmp(error_code(&cased), "invalid_request") &&
            strstr(cased.text, "only in case") && !ok(&unsafe) &&
            !strcmp(error_code(&unsafe), "invalid_request") &&
            exists(root, "content/events/Harm.yaml"),
        "renameSymbol refuses case-only renames and names no file takes in the "
        "dry run"
    );
    reply_free(&cased);
    reply_free(&unsafe);
}

#define DELETE_PURSUIT                                                         \
    "{\"contract\":1,\"operation\":\"delete_symbol\",\"asset\":{\"kind\":"     \
    "\"behavior\",\"name\":\"Pursuit\"}%s}"

/* Deleting refuses while references remain, and force lists them. */
static void deletes(const char *root)
{
    reply refused = ask(root, DELETE_PURSUIT, NULL);
    bool listed = !ok(&refused) && count(&refused, "references") == 4;
    reply forced = ask(root, DELETE_PURSUIT, ",\"force\":true");
    bool dangling = ok(&forced) && count(&forced, "dangling") == 4 &&
                    count(&forced, "assets") == 1;
    char *expected = hashes(&forced);
    char extra[2048];
    snprintf(extra, sizeof extra, ",\"force\":true,\"dry_run\":false,\"expected_hashes\":%s", expected);
    reply deleted = ask(root, DELETE_PURSUIT, extra);
    test_check(listed && dangling && ok(&deleted) && !exists(root, "content/behaviors/Pursuit.yaml"), "deleteSymbol refuses a used asset, listing its references, and force deletes it");
    reply unused = ask(root, "{\"contract\":1,\"operation\":\"delete_symbol\",\"event\":\"RefreshEncounter\",\"force\":true%s}", NULL);
    yyjson_val *asset = yyjson_arr_get_first(yyjson_obj_get(unused.result, "assets"));
    test_check(
        ok(&unused) && count(&unused, "assets") == 1 &&
            yyjson_get_bool(yyjson_obj_get(asset, "deleted")) &&
            !strcmp(yyjson_get_str(yyjson_obj_get(asset, "path")), "content/events/RefreshEncounter.yaml"),
        "deleteSymbol deletes an event with its definition"
    );
    free(expected);
    reply_free(&refused);
    reply_free(&forced);
    reply_free(&deleted);
    reply_free(&unused);
}

void test_automation_refactor()
{
    char root[PATH_MAX];
    copy_project(root);
    renames(root);
    conflicts(root);
    refusals(root);
    deletes(root);
    remove_tree(root);
}
