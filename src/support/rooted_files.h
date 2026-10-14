#ifndef CHIBI_ROOTED_FILES_H
#define CHIBI_ROOTED_FILES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rooted_handle rooted_files;
typedef struct rooted_handle rooted_stream;
typedef struct
{
    uint64_t device, identity[2], bytes;
    int64_t modified_seconds, changed_seconds;
    int modified_nanoseconds, changed_nanoseconds;
    unsigned permissions;
    bool regular, directory, link;
} rooted_file_info;
typedef struct
{
    char *name;
    rooted_file_info info;
} rooted_file_entry;
typedef enum
{
    ROOTED_STREAM_READ,
    ROOTED_STREAM_CREATE
} rooted_stream_mode;

/* UTF-8 paths. Root creation accepts a trusted host path; every subsequent path
 * is relative, without empty/dot/parent segments. Child reparse points and
 * symlinks are refused, including intermediate directories. Failures set errno.
 * Handles, returned bytes and entry arrays are owned by the caller. */
bool rooted_files_path_valid(const char *relative);
/* Resolves an existing regular file to its actual workspace-relative spelling.
 * Identity comparison also handles two absent names in an existing parent,
 * respecting that directory's case rules. Neither operation writes files. */
char *rooted_files_canonical_path(rooted_files *, const char *);
bool rooted_files_path_equal(rooted_files *, const char *, const char *);
rooted_files *rooted_files_open(const char *path);
/* Opens the existing parent of a trusted host file path; returns an owned
 * validated leaf. The caller owns both outputs. No directories are created. */
rooted_files *rooted_files_open_parent(const char *path, char **leaf);
void rooted_files_close(rooted_files *);
/* Owned host path of the opened directory, for display/host tool arguments. */
char *rooted_files_host_path(rooted_files *);
rooted_files *rooted_files_directory(rooted_files *, const char *relative, bool create);
/* Creates only the final directory exclusively; parents must already exist. */
rooted_files *rooted_files_directory_new(rooted_files *, const char *relative);
bool rooted_files_stat(rooted_files *, const char *, rooted_file_info *);
bool rooted_files_list(rooted_files *, const char *, int limit, rooted_file_entry **, int *count);
void rooted_file_entries_free(rooted_file_entry *, int count);
bool rooted_file_info_equal(const rooted_file_info *, const rooted_file_info *);
bool rooted_files_read(rooted_files *, const char *, size_t limit, unsigned char **, size_t *);
/* Stages, flushes and atomically publishes. Replace requires an exact expected
 * baseline; create refuses an existing destination. Source bytes are borrowed.
 * A baseline conflict sets EBUSY; resource limits set EFBIG. */
bool rooted_files_write(
    rooted_files *,
    const char *,
    const void *,
    size_t,
    size_t limit,
    bool replace,
    const void *expected,
    size_t expected_bytes
);
bool rooted_files_move(rooted_files *, const char *from, const char *to, bool replace);
/* Exclusive rename of one safe directory; never replaces a destination. */
bool rooted_files_move_directory(rooted_files *, const char *from, const char *to);
bool rooted_files_remove(rooted_files *, const char *);
/* Removes one empty directory. Links/reparse points and nonempty directories
 * are refused; this never recursively deletes a tree. */
bool rooted_files_remove_directory(rooted_files *, const char *);

/* Streaming is for bounded asynchronous imports/jobs. Creation is exclusive;
 * the caller owns stage cleanup and publication. External reads are only for
 * explicitly selected host files and still refuse a final symlink/reparse. */
rooted_stream *rooted_files_stream(rooted_files *, const char *, rooted_stream_mode);
rooted_stream *rooted_stream_external(const char *path);
int64_t rooted_stream_read(rooted_stream *, void *, size_t);
int64_t rooted_stream_write(rooted_stream *, const void *, size_t);
bool rooted_stream_flush(rooted_stream *);
bool rooted_stream_info(rooted_stream *, rooted_file_info *);
/* Finishes and releases a stream, reporting a close/handle error. */
bool rooted_stream_finish(rooted_stream *);
void rooted_stream_close(rooted_stream *);
#endif
