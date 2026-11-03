/* Temporary boundary diagnostics for the frozen 72598 Windows C facade.
 * Only the copied archive's external Win32 symbol names are redirected here.
 * Keep each API's result and last-error state unchanged. */
#include <stdio.h>
#include <windows.h>

static int enabled;
void sdk_probe_enable(void)
{
    enabled = 1;
}

static void path_text(const wchar_t *path)
{
    char bytes[16384];
    if (!path) {
        fputs("NULL", stderr);
        return;
    }
    int count = WideCharToMultiByte(CP_UTF8, 0, path, -1, bytes, (int)sizeof(bytes), NULL, NULL);
    fputs(count > 0 ? bytes : "<conversion-failed>", stderr);
}

DWORD WINAPI probe_GetModuleFileNameW(HMODULE module, LPWSTR path, DWORD size)
{
    DWORD count = GetModuleFileNameW(module, path, size), error = GetLastError();
    if (enabled) {
        fprintf(stderr, "[sdk-probe] GetModuleFileNameW count=%lu error=%lu path=", count, error);
        if (count && count < size) {
            path_text(path);
        }
        fputc('\n', stderr);
    }
    SetLastError(error);
    return count;
}
DWORD(WINAPI *probe_imp_GetModuleFileNameW)(HMODULE, LPWSTR, DWORD) = probe_GetModuleFileNameW;

HANDLE WINAPI probe_CreateFileW(
    LPCWSTR path,
    DWORD access,
    DWORD share,
    LPSECURITY_ATTRIBUTES security,
    DWORD creation,
    DWORD flags,
    HANDLE template_file
)
{
    HANDLE result = CreateFileW(path, access, share, security, creation, flags, template_file);
    DWORD error = GetLastError();
    if (enabled) {
        fprintf(stderr, "[sdk-probe] CreateFileW success=%d error=%lu path=", result != INVALID_HANDLE_VALUE, error);
        path_text(path);
        fputc('\n', stderr);
    }
    SetLastError(error);
    return result;
}

DWORD WINAPI probe_GetFinalPathNameByHandleW(HANDLE file, LPWSTR path, DWORD size, DWORD flags)
{
    DWORD count = GetFinalPathNameByHandleW(file, path, size, flags),
          error = GetLastError();
    if (enabled) {
        fprintf(stderr, "[sdk-probe] GetFinalPathNameByHandleW capacity=%lu count=%lu error=%lu path=", size, count, error);
        if (count && count < size) {
            path_text(path);
        }
        fputc('\n', stderr);
    }
    SetLastError(error);
    return count;
}

DWORD WINAPI probe_GetFileAttributesW(LPCWSTR path)
{
    DWORD result = GetFileAttributesW(path), error = GetLastError();
    if (enabled) {
        fprintf(stderr, "[sdk-probe] GetFileAttributesW attributes=%lu error=%lu path=", result, error);
        path_text(path);
        fputc('\n', stderr);
    }
    SetLastError(error);
    return result;
}

int WINAPI probe_WideCharToMultiByte(
    UINT page,
    DWORD flags,
    LPCWCH wide,
    int length,
    LPSTR bytes,
    int size,
    LPCCH fallback,
    LPBOOL used
)
{
    int result = WideCharToMultiByte(page, flags, wide, length, bytes, size, fallback, used);
    DWORD error = GetLastError();
    if (enabled) {
        fprintf(stderr, "[sdk-probe] WideCharToMultiByte units=%d capacity=%d result=%d error=%lu\n", length, size, result, error);
    }
    SetLastError(error);
    return result;
}

int WINAPI probe_CompareStringOrdinal(LPCWCH left, int left_size, LPCWCH right, int right_size, BOOL ignore_case)
{
    int result = CompareStringOrdinal(left, left_size, right, right_size, ignore_case);
    DWORD error = GetLastError();
    if (enabled && result == CSTR_EQUAL) {
        fputs("[sdk-probe] HasEnv matched the requested name\n", stderr);
    }
    SetLastError(error);
    return result;
}
