#include "support/file_io.h"

#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

char *file_read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = (char *)xmalloc((size_t)length + 1);
    size_t read = fread(text, 1, (size_t)length, file);
    int closed = fclose(file);
    if (read != (size_t)length || closed != 0) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
}

#ifndef __EMSCRIPTEN__
char *file_executable_path()
{
    char executable[4096];
#if defined(__APPLE__)
    uint32_t size = sizeof executable;
    if (_NSGetExecutablePath(executable, &size) != 0) {
        fprintf(stderr, "Cannot locate engine executable\n");
        abort();
    }
    char resolved[4096];
    if (realpath(executable, resolved)) {
        memcpy(executable, resolved, strlen(resolved) + 1);
    }
#elif defined(_WIN32)
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof executable);
    if (!length || length >= sizeof executable) {
        fprintf(stderr, "Cannot locate engine executable\n");
        abort();
    }
    for (char *p = executable; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
#else
    ssize_t length = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (length < 0 || (size_t)length >= sizeof executable - 1) {
        fprintf(stderr, "Cannot locate engine executable\n");
        abort();
    }
    executable[length] = '\0';
#endif
    return xstrdup(executable);
}
char *file_executable_dir()
{
    char *executable = file_executable_path();
    char *slash = strrchr(executable, '/');
    if (!slash) {
        fprintf(stderr, "Engine executable has no directory\n");
        abort();
    }
    *slash = '\0';
    return executable;
}
#endif
