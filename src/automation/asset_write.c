#include "automation/asset_write.h"

#include "automation/controller.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
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
    int root_fd = -1, content_fd = -1, folder_fd = -1, file_fd = -1;
    int result = CHIBI_AUTOMATION_IO_ERROR;
    char temporary[96] = { 0 };
    mode_t mode = 0644;
    if (!folder[0] || !filename[0] || filename[0] == '.' || strchr(folder, '/') ||
        strchr(folder, '\\') || strchr(filename, '/') || strstr(folder, "..") ||
        strstr(filename, "..") || strchr(filename, '\\')) {
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
    static atomic_uint sequence;
    for (int attempt = 0; attempt < 64; attempt++) {
        snprintf(
            temporary,
            sizeof temporary,
            ".authoring-%ld-%u.tmp",
            (long)getpid(),
            atomic_fetch_add(&sequence, 1)
        );
        file_fd = openat(folder_fd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file_fd >= 0 || errno != EEXIST) {
            break;
        }
        temporary[0] = 0;
    }
    if (file_fd < 0) {
        temporary[0] = 0;
        goto done;
    }
    size_t length = strlen(text), written = 0;
    while (written < length) {
        ssize_t count = write(file_fd, text + written, length - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            goto done;
        }
        written += (size_t)count;
    }
    if (fchmod(file_fd, mode) || fsync(file_fd)) {
        goto done;
    }
    if (close(file_fd)) {
        file_fd = -1;
        goto done;
    }
    file_fd = -1;
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
    if (file_fd >= 0) {
        close(file_fd);
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
