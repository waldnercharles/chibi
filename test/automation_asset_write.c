#include "automation/asset_write.h"
#include "automation/controller.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void join_path(char *out, size_t size, const char *parent, const char *name)
{
    int written = snprintf(out, size, "%s/%s", parent, name);
    assert(written >= 0 && (size_t)written < size);
}

static void make_root_and_content(char *root)
{
    strcpy(root, "/tmp/chibi-asset-write-XXXXXX");
    assert(mkdtemp(root));

    char content[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    assert(mkdir(content, 0755) == 0);
}

static void make_project(char *root)
{
    make_root_and_content(root);
    char content[PATH_MAX], behaviors[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(behaviors, sizeof behaviors, content, "behaviors");
    assert(mkdir(behaviors, 0755) == 0);
}

static void write_all(int fd, const char *text, size_t bytes)
{
    size_t written = 0;
    while (written < bytes) {
        ssize_t count = write(fd, text + written, bytes - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        assert(count > 0);
        written += (size_t)count;
    }
}

static void write_file(const char *path, const char *text, size_t bytes)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    write_all(fd, text, bytes);
    assert(close(fd) == 0);
}

static void assert_file_equals(const char *path, const char *expected, size_t bytes)
{
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct stat info;
    assert(fstat(fd, &info) == 0);
    assert(info.st_size >= 0 && (uintmax_t)info.st_size == bytes);

    char buffer[8192];
    size_t offset = 0;
    while (offset < bytes) {
        size_t request = bytes - offset;
        if (request > sizeof buffer) {
            request = sizeof buffer;
        }
        ssize_t count = read(fd, buffer, request);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        assert(count > 0);
        assert((size_t)count <= request);
        assert(!memcmp(buffer, expected + offset, (size_t)count));
        offset += (size_t)count;
    }
    char extra;
    ssize_t count = read(fd, &extra, 1);
    assert(count == 0);
    assert(close(fd) == 0);
}

static void assert_path_missing(const char *path)
{
    struct stat info;
    assert(lstat(path, &info) != 0);
    assert(errno == ENOENT);
}

static void assert_symlink(const char *path)
{
    struct stat info;
    assert(lstat(path, &info) == 0);
    assert(S_ISLNK(info.st_mode));
}

static void hash_text(char *out, size_t size, const char *text)
{
    int written = snprintf(
        out,
        size,
        "%016llx",
        (unsigned long long)automation_content_hash(text, strlen(text))
    );
    assert(written == 16 && (size_t)written < size);
}

static char *make_payload(const char *label, size_t bytes)
{
    assert(bytes > strlen(label) + 2);
    char *text = malloc(bytes + 1);
    assert(text);
    size_t label_bytes = strlen(label);
    memcpy(text, label, label_bytes);
    for (size_t i = label_bytes; i + 1 < bytes; i++) {
        text[i] = (char)('A' + (i % 23));
    }
    text[bytes - 1] = '\n';
    text[bytes] = '\0';
    return text;
}

static int publish_child(const char *root, const char *text, const char *expected_hash, int gate)
{
    char release;
    ssize_t count;
    do {
        count = read(gate, &release, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1) {
        _exit(3);
    }
    close(gate);

    char error[160] = { 0 };
    int status = automation_asset_publish(
        root,
        "behaviors",
        "Concurrent.code",
        text,
        expected_hash,
        false,
        error,
        sizeof error
    );
    if (status == CHIBI_AUTOMATION_OK) {
        _exit(0);
    }
    if (status == CHIBI_AUTOMATION_CONFLICT) {
        _exit(1);
    }
    _exit(2);
}

static void assert_child_result(pid_t child, int *successes, int *conflicts)
{
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    if (WEXITSTATUS(status) == 0) {
        (*successes)++;
    } else if (WEXITSTATUS(status) == 1) {
        (*conflicts)++;
    } else {
        assert(false);
    }
}

static void remove_project(const char *root)
{
    char content[PATH_MAX], behaviors[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(behaviors, sizeof behaviors, content, "behaviors");
    assert(rmdir(behaviors) == 0);
    assert(rmdir(content) == 0);
    assert(rmdir(root) == 0);
}

static void test_concurrent_replacements(const char *root, const char *initial)
{
    char content[PATH_MAX], behaviors[PATH_MAX], asset[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(behaviors, sizeof behaviors, content, "behaviors");
    join_path(asset, sizeof asset, behaviors, "Concurrent.code");
    write_file(asset, initial, strlen(initial));

    char expected_hash[32];
    hash_text(expected_hash, sizeof expected_hash, initial);
    char *first = make_payload("writer-a:", 65536);
    char *second = make_payload("writer-b:", 65536);

    int gate[2];
    assert(pipe(gate) == 0);
    pid_t first_child = fork();
    assert(first_child >= 0);
    if (first_child == 0) {
        close(gate[1]);
        publish_child(root, first, expected_hash, gate[0]);
    }
    pid_t second_child = fork();
    assert(second_child >= 0);
    if (second_child == 0) {
        close(gate[1]);
        publish_child(root, second, expected_hash, gate[0]);
    }
    close(gate[0]);
    char release[2] = { 'a', 'b' };
    write_all(gate[1], release, sizeof release);
    assert(close(gate[1]) == 0);

    int successes = 0, conflicts = 0;
    assert_child_result(first_child, &successes, &conflicts);
    assert_child_result(second_child, &successes, &conflicts);
    assert(successes == 1 && conflicts == 1);

    struct stat info;
    assert(stat(asset, &info) == 0);
    assert((size_t)info.st_size == strlen(first));
    char *final = malloc((size_t)info.st_size + 1);
    assert(final);
    int fd = open(asset, O_RDONLY);
    assert(fd >= 0);
    size_t offset = 0;
    while (offset < (size_t)info.st_size) {
        ssize_t count = read(fd, final + offset, (size_t)info.st_size - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        assert(count > 0);
        offset += (size_t)count;
    }
    final[offset] = '\0';
    assert(close(fd) == 0);
    assert(!strcmp(final, first) || !strcmp(final, second));
    free(final);
    free(first);
    free(second);
    assert(unlink(asset) == 0);
}

static void test_replacement_and_create(const char *root, const char *initial)
{
    char content[PATH_MAX], behaviors[PATH_MAX], asset[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(behaviors, sizeof behaviors, content, "behaviors");

    join_path(asset, sizeof asset, behaviors, "Replacement.code");
    write_file(asset, initial, strlen(initial));
    char stale_hash[32], valid_hash[32];
    hash_text(stale_hash, sizeof stale_hash, "different content");
    hash_text(valid_hash, sizeof valid_hash, initial);
    char replacement[128];
    snprintf(replacement, sizeof replacement, "replacement:%s\n", initial);
    char error[160] = { 0 };
    assert(
        automation_asset_publish(root, "behaviors", "Replacement.code", replacement, stale_hash, false, error, sizeof error) ==
        CHIBI_AUTOMATION_CONFLICT
    );
    assert_file_equals(asset, initial, strlen(initial));
    assert(
        automation_asset_publish(root, "behaviors", "Replacement.code", replacement, valid_hash, false, error, sizeof error) ==
        CHIBI_AUTOMATION_OK
    );
    assert_file_equals(asset, replacement, strlen(replacement));
    assert(unlink(asset) == 0);

    join_path(asset, sizeof asset, behaviors, "CreateCollision.code");
    const char *existing = "existing-create\n";
    const char *attempt = "should-not-replace\n";
    write_file(asset, existing, strlen(existing));
    assert(
        automation_asset_publish(root, "behaviors", "CreateCollision.code", attempt, NULL, true, error, sizeof error) ==
        CHIBI_AUTOMATION_CONFLICT
    );
    assert_file_equals(asset, existing, strlen(existing));
    assert(unlink(asset) == 0);

    const char *created = "created-asset-complete\n";
    assert(
        automation_asset_publish(root, "behaviors", "Created.code", created, NULL, true, error, sizeof error) ==
        CHIBI_AUTOMATION_OK
    );
    join_path(asset, sizeof asset, behaviors, "Created.code");
    assert_file_equals(asset, created, strlen(created));
    assert(unlink(asset) == 0);
}

static void test_invalid_names(const char *root)
{
    char error[160] = { 0 };
    const char *names[] = { ".authoring.lock", ".authoring-123.tmp" };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        assert(
            automation_asset_publish(root, "behaviors", names[i], "hidden\n", NULL, true, error, sizeof error) ==
            CHIBI_AUTOMATION_INVALID_REQUEST
        );
    }
    assert(
        automation_asset_publish(root, "bad\\folder", "Safe.code", "invalid-folder\n", NULL, true, error, sizeof error) ==
        CHIBI_AUTOMATION_INVALID_REQUEST
    );
}

static void test_content_symlink(void)
{
    char root[] = "/tmp/chibi-asset-write-content-link-XXXXXX";
    make_root_and_content(root);
    char content[PATH_MAX], real_content[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    assert(rmdir(content) == 0);
    join_path(real_content, sizeof real_content, root, "real-content");
    assert(mkdir(real_content, 0755) == 0);
    assert(symlink("real-content", content) == 0);

    char error[160] = { 0 };
    assert(
        automation_asset_publish(root, "behaviors", "Linked.code", "must-not-write\n", NULL, true, error, sizeof error) ==
        CHIBI_AUTOMATION_IO_ERROR
    );
    assert_symlink(content);
    char target[PATH_MAX];
    join_path(target, sizeof target, real_content, "behaviors");
    assert_path_missing(target);
    assert(unlink(content) == 0);
    assert(rmdir(real_content) == 0);
    assert(rmdir(root) == 0);
}

static void test_folder_symlink(void)
{
    char root[] = "/tmp/chibi-asset-write-folder-link-XXXXXX";
    make_root_and_content(root);
    char content[PATH_MAX], folder[PATH_MAX], real_folder[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(real_folder, sizeof real_folder, root, "real-folder");
    assert(mkdir(real_folder, 0755) == 0);
    join_path(folder, sizeof folder, content, "behaviors");
    assert(symlink("../real-folder", folder) == 0);

    char error[160] = { 0 };
    assert(
        automation_asset_publish(root, "behaviors", "Linked.code", "must-not-write\n", NULL, true, error, sizeof error) ==
        CHIBI_AUTOMATION_IO_ERROR
    );
    assert_symlink(folder);
    char target[PATH_MAX];
    join_path(target, sizeof target, real_folder, "Linked.code");
    assert_path_missing(target);
    assert(unlink(folder) == 0);
    assert(rmdir(real_folder) == 0);
    assert(rmdir(content) == 0);
    assert(rmdir(root) == 0);
}

static void test_target_symlink(void)
{
    char root[] = "/tmp/chibi-asset-write-target-link-XXXXXX";
    make_project(root);
    char content[PATH_MAX], folder[PATH_MAX], target[PATH_MAX], other[PATH_MAX];
    join_path(content, sizeof content, root, "content");
    join_path(folder, sizeof folder, content, "behaviors");
    join_path(target, sizeof target, folder, "Linked.code");
    join_path(other, sizeof other, folder, "Other.code");
    const char *original = "target-through-link\n";
    write_file(other, original, strlen(original));
    assert(symlink("Other.code", target) == 0);

    char expected_hash[32], error[160] = { 0 };
    hash_text(expected_hash, sizeof expected_hash, original);
    assert(
        automation_asset_publish(
            root,
            "behaviors",
            "Linked.code",
            "must-not-follow\n",
            expected_hash,
            false,
            error,
            sizeof error
        ) == CHIBI_AUTOMATION_IO_ERROR
    );
    assert_symlink(target);
    assert_file_equals(other, original, strlen(original));
    assert(unlink(target) == 0);
    assert(unlink(other) == 0);
    remove_project(root);
}

static int folder_entries(const char *folder)
{
    DIR *directory = opendir(folder);
    assert(directory);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    closedir(directory);
    return count;
}

/* A batch publishes every file or none, and rolls back to the originals. */
static void test_batches(const char *root)
{
    char behaviors[PATH_MAX], a[PATH_MAX], b[PATH_MAX], c[PATH_MAX], moved[PATH_MAX];
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    join_path(a, sizeof a, behaviors, "A.code");
    join_path(b, sizeof b, behaviors, "B.code");
    join_path(c, sizeof c, behaviors, "C.code");
    join_path(moved, sizeof moved, behaviors, "Moved.code");
    write_file(a, "a\n", 2);
    write_file(b, "b\n", 2);
    write_file(c, "c\n", 2);
    char ha[32], hb[32], hc[32];
    hash_text(ha, sizeof ha, "a\n");
    hash_text(hb, sizeof hb, "b\n");
    hash_text(hc, sizeof hc, "c\n");
    automation_asset_change changes[] = {
        { "behaviors", "A.code", NULL, "a2\n", ha, false },
        { "behaviors", "B.code", "Moved.code", "b2\n", hb, false },
        { "behaviors", "C.code", NULL, NULL, hc, false },
    };
    char error[160];
    automation_asset_batch *batch = NULL;

    /* A stale hash or a taken name publishes nothing. */
    changes[2].expected_hash = hb;
    assert(
        automation_assets_publish(root, changes, 3, &batch, error, sizeof error) == CHIBI_AUTOMATION_CONFLICT
    );
    changes[2].expected_hash = hc;
    changes[1].rename_to = "C.code";
    assert(
        automation_assets_publish(root, changes, 3, &batch, error, sizeof error) == CHIBI_AUTOMATION_CONFLICT
    );
    changes[1].rename_to = "Moved.code";
    assert(!batch && folder_entries(behaviors) == 3);
    assert_file_equals(a, "a\n", 2);

    /* Published files roll back to the originals. */
    assert(
        automation_assets_publish(root, changes, 3, &batch, error, sizeof error) == CHIBI_AUTOMATION_OK
    );
    assert_file_equals(a, "a2\n", 3);
    assert_file_equals(moved, "b2\n", 3);
    assert_path_missing(b);
    assert_path_missing(c);
    assert(automation_assets_rollback(batch, error, sizeof error) == CHIBI_AUTOMATION_OK);
    assert_file_equals(a, "a\n", 2);
    assert_file_equals(b, "b\n", 2);
    assert_file_equals(c, "c\n", 2);
    assert_path_missing(moved);
    assert(folder_entries(behaviors) == 3);

    /* Committed files stay, and nothing is left beside them. */
    assert(
        automation_assets_publish(root, changes, 3, &batch, error, sizeof error) == CHIBI_AUTOMATION_OK
    );
    automation_assets_commit(batch);
    assert_file_equals(a, "a2\n", 3);
    assert_file_equals(moved, "b2\n", 3);
    assert_path_missing(c);
    assert(folder_entries(behaviors) == 2);
    assert(unlink(a) == 0 && unlink(moved) == 0);
}

/* A file that cannot move aside midway puts back the ones already placed. */
static void test_batch_failure(const char *root)
{
    if (geteuid() == 0) {
        return; /* permissions do not stop root */
    }
    char behaviors[PATH_MAX], events[PATH_MAX], a[PATH_MAX], e[PATH_MAX],
        metadata[PATH_MAX];
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    snprintf(events, sizeof events, "%s/content/events", root);
    assert(mkdir(events, 0755) == 0);
    join_path(a, sizeof a, behaviors, "A.code");
    join_path(e, sizeof e, events, "E.kdl");
    join_path(metadata, sizeof metadata, behaviors, "Moved.code.meta");
    write_file(a, "a\n", 2);
    write_file(e, "e\n", 2);
    char ha[32], he[32];
    hash_text(ha, sizeof ha, "a\n");
    hash_text(he, sizeof he, "e\n");
    automation_asset_change changes[] = {
        { "behaviors", "Moved.code.meta", NULL, "metadata\n", NULL, true },
        { "behaviors", "A.code", "Moved.code", "a2\n", ha, false },
        { "events", "E.kdl", NULL, NULL, he, false },
    };
    assert(chmod(events, 0555) == 0);
    char error[160];
    automation_asset_batch *batch = NULL;
    int status = automation_assets_publish(root, changes, 3, &batch, error, sizeof error);
    assert(chmod(events, 0755) == 0);
    assert(status == CHIBI_AUTOMATION_IO_ERROR && !batch);
    assert(!strstr(error, ".bak"));
    assert_file_equals(a, "a\n", 2);
    assert_file_equals(e, "e\n", 2);
    assert_path_missing(metadata);
    assert(folder_entries(behaviors) == 1 && folder_entries(events) == 1);
    assert(unlink(a) == 0 && unlink(e) == 0 && rmdir(events) == 0);
}

static void test_batch_create(const char *root)
{
    char folder[PATH_MAX], source[PATH_MAX], metadata[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/content/behaviors", root);
    join_path(source, sizeof source, folder, "Source.code");
    join_path(metadata, sizeof metadata, folder, "Source.code.meta");
    write_file(source, "old\n", 4);
    char hash[32], error[160];
    hash_text(hash, sizeof hash, "old\n");
    automation_asset_change changes[] = {
        { "behaviors", "Source.code", NULL, "new\n", hash, false },
        { "behaviors", "Source.code.meta", NULL, "metadata\n", NULL, true },
    };
    automation_asset_batch *batch = NULL;
    assert(
        automation_assets_publish(root, changes, 2, &batch, error, sizeof error) == CHIBI_AUTOMATION_OK
    );
    assert_file_equals(source, "new\n", 4);
    assert_file_equals(metadata, "metadata\n", 9);
    struct stat info;
    assert(!stat(metadata, &info) && (info.st_mode & 0777) == 0644);
    assert(automation_assets_rollback(batch, error, sizeof error) == CHIBI_AUTOMATION_OK);
    assert_file_equals(source, "old\n", 4);
    assert_path_missing(metadata);
    assert(folder_entries(folder) == 1);

    /* A target collision after source publication restores the whole batch. */
    automation_asset_change collision[] = { changes[0], changes[1], changes[1] };
    assert(
        automation_assets_publish(root, collision, 3, &batch, error, sizeof error) == CHIBI_AUTOMATION_CONFLICT
    );
    assert(!batch && folder_entries(folder) == 1);
    assert_file_equals(source, "old\n", 4);
    assert_path_missing(metadata);

    /* An existing sidecar aborts before replacing its paired source. */
    write_file(metadata, "existing\n", 9);
    assert(
        automation_assets_publish(root, changes, 2, &batch, error, sizeof error) == CHIBI_AUTOMATION_CONFLICT
    );
    assert(!batch && folder_entries(folder) == 2);
    assert_file_equals(source, "old\n", 4);
    assert_file_equals(metadata, "existing\n", 9);
    assert(!unlink(metadata));

    assert(
        automation_assets_publish(root, changes, 2, &batch, error, sizeof error) == CHIBI_AUTOMATION_OK
    );
    automation_assets_commit(batch);
    assert_file_equals(source, "new\n", 4);
    assert_file_equals(metadata, "metadata\n", 9);
    assert(folder_entries(folder) == 2);
    assert(!unlink(source) && !unlink(metadata));
}

void test_automation_metadata_write()
{
    char root[] = "/tmp/chibi-metadata-write-XXXXXX";
    make_project(root);
    test_batch_create(root);
    test_batch_failure(root);
    remove_project(root);
}

void test_automation_asset_write(void)
{
    char root[] = "/tmp/chibi-asset-write-XXXXXX";
    make_project(root);
    const char *initial = "{\"value\":\"original\"}\n";
    test_concurrent_replacements(root, initial);
    test_replacement_and_create(root, initial);
    test_invalid_names(root);
    test_batches(root);
    test_batch_create(root);
    test_batch_failure(root);
    remove_project(root);

    test_content_symlink();
    test_folder_symlink();
    test_target_symlink();
    puts("automation asset publication: complete bytes, CAS, collisions, and symlink guards pass");
}
