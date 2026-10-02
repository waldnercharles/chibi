#include "automation/asset_write.h"

#include "automation/controller.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

uint64_t automation_content_hash(const char *text, size_t bytes)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < bytes; i++) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int directory(int parent, const char *name, bool create)
{
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0 || !create || errno != ENOENT) {
        return fd;
    }
    if (mkdirat(parent, name, 0755) && errno != EEXIST) {
        return -1;
    }
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static int matches(int directory, const char *name, const char *expected, bool create, mode_t *mode)
{
    int fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) {
            return create ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_CONFLICT;
        }
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    struct stat info;
    if (fstat(fd, &info)) {
        close(fd);
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    if (create || !S_ISREG(info.st_mode)) {
        close(fd);
        return CHIBI_AUTOMATION_CONFLICT;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    char buffer[8192];
    ssize_t size;
    for (;;) {
        size = read(fd, buffer, sizeof buffer);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            break;
        }
        for (ssize_t i = 0; i < size; i++) {
            hash ^= (unsigned char)buffer[i];
            hash *= UINT64_C(1099511628211);
        }
    }
    int closed = close(fd);
    if (size < 0 || closed) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char actual[17];
    snprintf(actual, sizeof actual, "%016llx", (unsigned long long)hash);
    *mode = info.st_mode & 0777;
    return expected && !strcmp(expected, actual) ? CHIBI_AUTOMATION_OK
                                                 : CHIBI_AUTOMATION_CONFLICT;
}

static bool unsafe_name(const char *folder, const char *filename)
{
    return !folder[0] || !filename[0] || filename[0] == '.' ||
           strchr(folder, '/') || strchr(folder, '\\') || strchr(filename, '/') ||
           strstr(folder, "..") || strstr(filename, "..") || strchr(filename, '\\');
}

/* A new file of `text` beside the asset, named into `temporary`; false with
   `temporary` empty when it cannot be written. */
static bool write_temporary(int folder_fd, const char *text, mode_t mode, char *temporary, size_t size)
{
    static atomic_uint sequence;
    int file_fd = -1;
    for (int attempt = 0; attempt < 64 && file_fd < 0; attempt++) {
        snprintf(temporary, size, ".authoring-%ld-%u.tmp", (long)getpid(), atomic_fetch_add(&sequence, 1));
        file_fd = openat(folder_fd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file_fd < 0 && errno != EEXIST) {
            break;
        }
    }
    if (file_fd < 0) {
        temporary[0] = 0;
        return false;
    }
    size_t length = strlen(text), written = 0;
    bool ok = true;
    while (ok && written < length) {
        ssize_t count = write(file_fd, text + written, length - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        ok = count > 0;
        written += ok ? (size_t)count : 0;
    }
    ok = ok && !fchmod(file_fd, mode) && !fsync(file_fd);
    ok = !close(file_fd) && ok;
    if (!ok) {
        unlinkat(folder_fd, temporary, 0);
        temporary[0] = 0;
    }
    return ok;
}

int automation_asset_publish(
    const char *root,
    const char *folder,
    const char *filename,
    const char *text,
    const char *expected_hash,
    bool create,
    char *error,
    size_t error_size
)
{
    assert(root && folder && filename && text && error && error_size);
    int root_fd = -1, content_fd = -1, folder_fd = -1;
    int result = CHIBI_AUTOMATION_IO_ERROR;
    char temporary[96] = { 0 };
    mode_t mode = 0644;
    if (unsafe_name(folder, filename)) {
        snprintf(error, error_size, "invalid asset destination");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0) {
        goto done;
    }
    content_fd = directory(root_fd, "content", create);
    if (content_fd < 0) {
        goto done;
    }
    folder_fd = directory(content_fd, folder, create);
    if (folder_fd < 0) {
        goto done;
    }
    if (flock(folder_fd, LOCK_EX)) {
        goto done;
    }
    result = matches(folder_fd, filename, expected_hash, create, &mode);
    if (result != CHIBI_AUTOMATION_OK) {
        goto done;
    }
    result = CHIBI_AUTOMATION_IO_ERROR;
    if (!write_temporary(folder_fd, text, mode, temporary, sizeof temporary)) {
        goto done;
    }
    result = matches(folder_fd, filename, expected_hash, create, &mode);
    if (result != CHIBI_AUTOMATION_OK) {
        goto done;
    }
    int published = create ? linkat(folder_fd, temporary, folder_fd, filename, 0)
                           : renameat(folder_fd, temporary, folder_fd, filename);
    if (published) {
        result = create && errno == EEXIST ? CHIBI_AUTOMATION_CONFLICT
                                           : CHIBI_AUTOMATION_IO_ERROR;
    }

done:
    if (result != CHIBI_AUTOMATION_OK) {
        snprintf(error, error_size, "%s", result == CHIBI_AUTOMATION_CONFLICT ? "asset changed or already exists; inspect its current hash and retry" : "could not atomically publish asset");
    }
    if (temporary[0] && folder_fd >= 0) {
        unlinkat(folder_fd, temporary, 0);
    }
    if (folder_fd >= 0) {
        close(folder_fd);
    }
    if (content_fd >= 0) {
        close(content_fd);
    }
    if (root_fd >= 0) {
        close(root_fd);
    }
    return result;
}

/* ---------- Batches ---------- */

typedef struct
{
    int folder; /* its folder among the batch's */
    char *filename;
    char *target; /* where the new text goes; NULL when deleted */
    char temporary[96];
    char backup[96]; /* the original, moved aside */
    bool moved, placed;
} batch_file;

struct automation_asset_batch
{
    int root_fd, content_fd;
    char **folders;
    int *folder_fds;
    batch_file *files;
};

static int folder_order(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void batch_close(automation_asset_batch *batch)
{
    for (int i = 0; i < acount(batch->files); i++) {
        batch_file *file = &batch->files[i];
        if (file->temporary[0]) {
            unlinkat(batch->folder_fds[file->folder], file->temporary, 0);
        }
        free(file->filename);
        free(file->target);
    }
    for (int i = 0; i < acount(batch->folders); i++) {
        if (batch->folder_fds[i] >= 0) {
            close(batch->folder_fds[i]); /* releases its lock */
        }
        free(batch->folders[i]);
    }
    if (batch->content_fd >= 0) {
        close(batch->content_fd);
    }
    if (batch->root_fd >= 0) {
        close(batch->root_fd);
    }
    afree(batch->files);
    afree(batch->folders);
    free(batch->folder_fds);
    free(batch);
}

static int folder_of(const automation_asset_batch *batch, const char *folder)
{
    for (int i = 0; i < acount(batch->folders); i++) {
        if (!strcmp(batch->folders[i], folder)) {
            return i;
        }
    }
    return -1;
}

/* Opens and locks each folder the batch touches, in name order so two
   batches never wait on each other. */
static int lock_folders(automation_asset_batch *batch, const automation_asset_change *changes, int count)
{
    for (int i = 0; i < count; i++) {
        if (folder_of(batch, changes[i].folder) < 0) {
            apush(batch->folders, xstrdup(changes[i].folder));
        }
    }
    qsort(batch->folders, (size_t)acount(batch->folders), sizeof *batch->folders, folder_order);
    batch->folder_fds = xmalloc(
        ((size_t)acount(batch->folders) + 1) * sizeof *batch->folder_fds
    );
    int status = CHIBI_AUTOMATION_OK;
    for (int i = 0; i < acount(batch->folders); i++) {
        batch->folder_fds[i] = status == CHIBI_AUTOMATION_OK
                                 ? directory(batch->content_fd, batch->folders[i], false)
                                 : -1;
        if (status == CHIBI_AUTOMATION_OK &&
            (batch->folder_fds[i] < 0 || flock(batch->folder_fds[i], LOCK_EX))) {
            status = CHIBI_AUTOMATION_IO_ERROR;
        }
    }
    return status;
}

/* Every original still has its hash and every new name is free. */
static int check_files(automation_asset_batch *batch, const automation_asset_change *changes, mode_t *modes)
{
    for (int i = 0; i < acount(batch->files); i++) {
        const batch_file *file = &batch->files[i];
        int fd = batch->folder_fds[file->folder];
        int status = matches(fd, file->filename, changes[i].expected_hash, false, &modes[i]);
        struct stat info;
        if (status == CHIBI_AUTOMATION_OK && changes[i].rename_to &&
            fstatat(fd, changes[i].rename_to, &info, AT_SYMLINK_NOFOLLOW) != -1) {
            status = CHIBI_AUTOMATION_CONFLICT;
        }
        if (status != CHIBI_AUTOMATION_OK) {
            return status;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

static const char UNRESTORED[] = "could not restore every asset; originals are "
                                 "kept beside them as .authoring-*.bak";

/* Puts each original back and removes each new file; false when one stays. */
static bool undo_files(automation_asset_batch *batch)
{
    bool restored = true;
    for (int i = acount(batch->files) - 1; i >= 0; i--) {
        batch_file *file = &batch->files[i];
        int fd = batch->folder_fds[file->folder];
        if (file->placed && !unlinkat(fd, file->target, 0)) {
            file->placed = false;
        }
        if (file->moved && !renameat(fd, file->backup, fd, file->filename)) {
            file->moved = false;
            file->backup[0] = 0;
        }
        restored &= !file->placed && !file->moved;
    }
    return restored;
}

/* Moves each original aside, then links its new text into place, which
   fails rather than replace a file that appeared since the check. When one
   fails the others go back; *restored is false when one could not. */
static int place_files(automation_asset_batch *batch, bool *restored)
{
    static atomic_uint sequence;
    for (int i = 0; i < acount(batch->files); i++) {
        batch_file *file = &batch->files[i];
        int fd = batch->folder_fds[file->folder];
        snprintf(
            file->backup,
            sizeof file->backup,
            ".authoring-%ld-%u.bak",
            (long)getpid(),
            atomic_fetch_add(&sequence, 1)
        );
        file->moved = !renameat(fd, file->filename, fd, file->backup);
        file->placed = file->moved && file->target &&
                       !linkat(fd, file->temporary, fd, file->target, 0);
        if (!file->moved || (file->target && !file->placed)) {
            int status = errno == EEXIST ? CHIBI_AUTOMATION_CONFLICT
                                         : CHIBI_AUTOMATION_IO_ERROR;
            *restored = undo_files(batch);
            return *restored ? status : CHIBI_AUTOMATION_IO_ERROR;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

int automation_assets_publish(
    const char *root,
    const automation_asset_change *changes,
    int count,
    automation_asset_batch **out,
    char *error,
    size_t error_size
)
{
    assert(root && changes && count > 0 && out && error && error_size);
    *out = NULL;
    for (int i = 0; i < count; i++) {
        if (unsafe_name(changes[i].folder, changes[i].filename) ||
            (changes[i].rename_to && unsafe_name(changes[i].folder, changes[i].rename_to)) ||
            !changes[i].expected_hash) {
            snprintf(error, error_size, "invalid asset destination");
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
    }
    automation_asset_batch *batch = xcalloc(1, sizeof *batch);
    batch->root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    batch->content_fd = batch->root_fd >= 0
                          ? directory(batch->root_fd, "content", false)
                          : -1;
    int status = batch->content_fd >= 0 ? lock_folders(batch, changes, count)
                                        : CHIBI_AUTOMATION_IO_ERROR;
    mode_t *modes = xcalloc((size_t)count, sizeof *modes);
    for (int i = 0; status == CHIBI_AUTOMATION_OK && i < count; i++) {
        const char *target = changes[i].rename_to ? changes[i].rename_to
                                                  : changes[i].filename;
        apush(
            batch->files,
            ((batch_file){ .folder = folder_of(batch, changes[i].folder),
                           .filename = xstrdup(changes[i].filename),
                           .target = changes[i].text ? xstrdup(target) : NULL })
        );
    }
    status = status == CHIBI_AUTOMATION_OK ? check_files(batch, changes, modes) : status;
    for (int i = 0; status == CHIBI_AUTOMATION_OK && i < count; i++) {
        batch_file *file = &batch->files[i];
        if (changes[i].text && !write_temporary(
                                   batch->folder_fds[file->folder],
                                   changes[i].text,
                                   modes[i],
                                   file->temporary,
                                   sizeof file->temporary
                               )) {
            status = CHIBI_AUTOMATION_IO_ERROR;
        }
    }
    status = status == CHIBI_AUTOMATION_OK ? check_files(batch, changes, modes) : status;
    bool restored = true;
    status = status == CHIBI_AUTOMATION_OK ? place_files(batch, &restored) : status;
    free(modes);
    if (status != CHIBI_AUTOMATION_OK) {
        snprintf(
            error,
            error_size,
            "%s",
            !restored ? UNRESTORED
            : status == CHIBI_AUTOMATION_CONFLICT
                ? "an asset changed or a new name is taken; inspect "
                  "current "
                  "hashes and retry"
                : "could not publish every asset"
        );
        batch_close(batch);
        return status;
    }
    *out = batch;
    return CHIBI_AUTOMATION_OK;
}

void automation_assets_commit(automation_asset_batch *batch)
{
    for (int i = 0; i < acount(batch->files); i++) {
        batch_file *file = &batch->files[i];
        int fd = batch->folder_fds[file->folder];
        if (file->placed) {
            unlinkat(fd, file->temporary, 0);
            file->temporary[0] = 0;
        }
        if (file->moved) {
            unlinkat(fd, file->backup, 0);
        }
    }
    batch_close(batch);
}

int automation_assets_rollback(automation_asset_batch *batch, char *error, size_t error_size)
{
    bool restored = undo_files(batch);
    batch_close(batch);
    if (!restored) {
        snprintf(error, error_size, "%s", UNRESTORED);
    }
    return restored ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_IO_ERROR;
}
