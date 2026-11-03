/* ABI boundary checks use real Wasm libc descriptors and longjmp. */
#include "authoring/asset_compiler_wasm.h"

#include <dirent.h>
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
extern int chibi_asset_resource_add(uint64_t, int);
extern int chibi_asset_resource_remove(uint64_t, int);
extern int chibi_asset_publish(const unsigned char *, int);
extern int chibi_native_scope_begin(uint64_t);
extern void chibi_native_step(void);
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
static int mode, owned_fd, owned_dir_fd;
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
        require(chibi_asset_option_length(2) == 4);
        require(chibi_asset_option_copy(2, copied, 4) == 0 && !memcmp(copied, "salt", 4));
    } else {
        require(chibi_asset_option_length(2) == -1);
    }
    require(chibi_asset_identity_copy(copied, 63) == -1);
    require(chibi_asset_identity_copy(copied, 64) == 0 && !memcmp(copied, identity, 64));
    require(chibi_asset_resource_add(UINT64_MAX, 0) == -1);
    require(chibi_asset_resource_add(UINT64_C(4294967296), 1) == -1);
    require(chibi_asset_resource_add(0, 1) == -1);
    require(chibi_asset_resource_add(0, 2) == -1);
    owned_fd = open("/resource", O_RDWR | O_CREAT, 0600);
    require(owned_fd >= 0 && chibi_asset_resource_add((unsigned)owned_fd, 0) == 0);
    DIR *directory = opendir("/");
    require(directory);
    owned_dir_fd = dirfd(directory);
    require(owned_dir_fd >= 0 && chibi_asset_resource_add((uintptr_t)directory, 1) == 0);
    require(chibi_asset_resource_remove((unsigned)owned_fd, 1) == -1);
    if (mode == 1) {
        require(close(owned_fd) == 0);
        require(chibi_asset_resource_remove((unsigned)owned_fd, 0) == 0);
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
    return chibi_asset_publish((const unsigned char *)"ok", 2);
}

int main(void)
{
    for (mode = 0; mode != 5; ++mode) {
        stopped = false;
        asset_compiler_options options = { .cache_directory = "",
                                           .cancelled = cancelled,
                                           .user = &stopped };
        asset_compiler_diagnostic diagnostic;
        size_t length = 99;
        unsigned char *
            output = mode & 1
                       ? asset_compiler_wasm_compile_cached("{}", 2, "salt", &length, &options, &diagnostic)
                       : asset_compiler_wasm_compile("{}", 2, &length, &options, &diagnostic);
        if (mode < 2) {
            require(output && length == 2 && !memcmp(output, "ok", 2));
        } else {
            require(!output && length == 0 && diagnostic.message[0]);
        }
        free(output);
        errno = 0;
        require(fcntl(owned_fd, F_GETFD) == -1 && errno == EBADF);
        errno = 0;
        require(fcntl(owned_dir_fd, F_GETFD) == -1 && errno == EBADF);
        require(asset_compiler_wasm_test_allocations() == 0);
    }
    puts("PASS Wasm call options and resource cleanup after normal return, failure, quota and cancellation");
    return 0;
}
