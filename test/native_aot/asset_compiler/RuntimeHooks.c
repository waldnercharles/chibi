/* ABI boundary checks use real Wasm libc descriptors and longjmp. */
#include "authoring/asset_compiler_wasm.h"

#include <dirent.h>
#include <emscripten.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int chibi_asset_cached(void);
extern int chibi_asset_option_length(int);
extern int chibi_asset_option_copy(int, unsigned char *, int);
extern int chibi_asset_identity_copy(unsigned char *, int);
extern uint64_t chibi_asset_salt_length(void);
extern int chibi_asset_salt_read(uint64_t, unsigned char *, int);
extern int chibi_asset_resource_add(uint64_t, int);
extern int chibi_asset_resource_remove(uint64_t, int);
extern int chibi_asset_publish(const unsigned char *, int);
extern int chibi_native_scope_begin(uint64_t);
extern void chibi_native_step(void);
extern void chibi_native_reference_unavailable(void);
extern void chibi_native_array_mutation_unavailable(void);
extern unsigned char *asset_compiler_wasm_compile_cached(
    const char *,
    size_t,
    const char *,
    size_t *,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
static void require(bool passed)
{
    if (!passed) {
        abort();
    }
}
extern int chibi_asset_file_open(const char *);
extern int chibi_asset_file_close(int);
extern int chibi_asset_file_rename(const char *, const char *);
extern int chibi_asset_file_temporary(unsigned char *);
extern int chibi_asset_file_lock(const char *);
extern uint64_t chibi_asset_directory_open(const char *);
static int mode, owned_fd, owned_dir_fd, owned_lock, owned_temporary;
static unsigned char temporary_path[64];
static char long_salt[131074];
static bool stopped;
static const char identity[] = "0123456789abcdef0123456789abcdef0123456789abcde"
                               "f0123456789abcdef";
const char *asset_compiler_wasm_identity(void)
{
    return identity;
}
void chibi_format_text(void *context, void *text, int count, void *arguments)
{
    (void)context;
    (void)text;
    (void)count;
    (void)arguments;
    abort();
}
static bool cancelled(void *user)
{
    require(user == &stopped);
    return stopped;
}

int chibi_scalar(void *context)
{
    (void)context;
    unsigned char copied[64];
    require(chibi_asset_option_length(0) == -1); /* No desktop program option. */
    require(chibi_asset_option_length(1) == 0); /* Explicit empty cache directory. */
    require(chibi_asset_option_copy(1, copied, 1) == -1);
    require(chibi_asset_option_copy(1, copied, 0) == 0);
    require(chibi_asset_cached() == (mode & 1));
    if (mode & 1) {
        const char *salt = mode == 1 ? long_salt : "salt";
        size_t count = strlen(salt);
        require(chibi_asset_option_length(2) == (int)count);
        require(chibi_asset_salt_length() == count);
        for (size_t first = 0; first < count; first += sizeof(copied)) {
            size_t take = count - first;
            if (take > sizeof(copied)) {
                take = sizeof(copied);
            }
            require(chibi_asset_salt_read(first, copied, (int)take) == 0);
            require(!memcmp(copied, salt + first, take));
        }
        require(chibi_asset_salt_read(count, copied, 0) == 0);
        require(chibi_asset_salt_read(count, copied, 1) == -1);
        require(chibi_asset_salt_read(UINT64_MAX, copied, 1) == -1);
    } else {
        require(chibi_asset_option_length(2) == -1);
        require(chibi_asset_salt_length() == UINT64_MAX);
        require(chibi_asset_salt_read(0, copied, 0) == -1);
    }
    require(chibi_asset_identity_copy(copied, 63) == -1);
    require(chibi_asset_identity_copy(copied, 64) == 0 && !memcmp(copied, identity, 64));
    require(chibi_asset_resource_add(UINT64_MAX, 0) == -1);
    require(chibi_asset_resource_add(UINT64_C(4294967296), 1) == -1);
    require(chibi_asset_resource_add(0, 1) == -1);
    require(chibi_asset_resource_add(0, 4) == -1);
    int created = open("/resource", O_RDWR | O_CREAT, 0600);
    require(created >= 0);
    int closed = close(created);
    require(closed == 0);
    // These are the production uninstrumented acquire-and-track boundaries.
    owned_fd = chibi_asset_file_open("/resource");
    require(owned_fd >= 0);
    uint64_t directory = chibi_asset_directory_open("/");
    require(directory != 0);
    owned_dir_fd = dirfd((DIR *)(uintptr_t)directory);
    require(owned_dir_fd >= 0);
    owned_lock = chibi_asset_file_lock("/.lock");
    require(owned_lock >= 0);
    strcpy((char *)temporary_path, "/.tmp-XXXXXX");
    owned_temporary = chibi_asset_file_temporary(temporary_path);
    require(owned_temporary >= 0);
    if (mode == 0) {
        // A dirty callback can reuse the old name immediately after rename.
        // This new file belongs to that callback, not to the old temporary.
        EM_ASM(
            {
                Module['chibiAssetCacheDirty'] = function()
                {
                    FS.writeFile(UTF8ToString($0), new Uint8Array([77]));
                };
            },
            temporary_path
        );
        int renamed = chibi_asset_file_rename((const char *)temporary_path, "/published");
        EM_ASM({ delete Module['chibiAssetCacheDirty']; });
        require(renamed == 0);
    }
    require(chibi_asset_resource_remove((unsigned)owned_fd, 1) == -1);
    if (mode == 1) {
        require(chibi_asset_file_close(owned_fd) == 0);
        require(chibi_asset_resource_remove((unsigned)owned_fd, 0) == -1);
    }
    if (mode == 2) {
        require(chibi_native_scope_begin(1) == 0);
        chibi_native_step();
        chibi_native_step();
        abort();
    }
    if (mode == 3) {
        stopped = true;
        chibi_native_step();
        abort();
    }
    if (mode == 4) {
        return 2;
    }
    if (mode == 5) {
        chibi_native_reference_unavailable();
        abort();
    }
    if (mode == 6) {
        chibi_native_array_mutation_unavailable();
        abort();
    }
    return chibi_asset_publish((const unsigned char *)"ok", 2);
}

int main(void)
{
    memset(long_salt, 's', sizeof(long_salt) - 1);
    for (mode = 0; mode != 8; ++mode) {
        stopped = false;
        asset_compiler_options options = { .cache_directory = "",
                                           .cancelled = cancelled,
                                           .user = &stopped };
        asset_compiler_diagnostic diagnostic;
        size_t length = 99;
        unsigned char *output = mode & 1
                                  ? asset_compiler_wasm_compile_cached(
                                        "{}",
                                        2,
                                        mode == 1 ? long_salt : "salt",
                                        &length,
                                        &options,
                                        &diagnostic
                                    )
                                  : asset_compiler_wasm_compile("{}", 2, &length, &options, &diagnostic);
        if (mode < 2 || mode == 7) {
            require(output && length == 2 && !memcmp(output, "ok", 2));
        } else {
            require(!output && length == 0 && diagnostic.message[0]);
        }
        free(output);
        errno = 0;
        require(fcntl(owned_fd, F_GETFD) == -1 && errno == EBADF);
        errno = 0;
        require(fcntl(owned_dir_fd, F_GETFD) == -1 && errno == EBADF);
        errno = 0;
        require(fcntl(owned_lock, F_GETFD) == -1 && errno == EBADF);
        errno = 0;
        require(fcntl(owned_temporary, F_GETFD) == -1 && errno == EBADF);
        if (mode == 0) {
            require(access((const char *)temporary_path, F_OK) == 0);
            int removed = unlink((const char *)temporary_path);
            require(removed == 0);
        } else {
            require(access((const char *)temporary_path, F_OK) == -1);
        }
        require(asset_compiler_wasm_test_allocations() == 0);
    }
    puts("PASS Wasm call options, logical-place recovery and resource cleanup after normal return, failure, quota and cancellation");
    return 0;
}
