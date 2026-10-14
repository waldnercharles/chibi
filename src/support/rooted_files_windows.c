#ifdef _WIN32
#include "support/rooted_files_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <winternl.h>

static bool windows_error(DWORD code)
{
    SetLastError(code);
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS: errno = EEXIST; break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_WRITE_PROTECT: errno = EACCES; break;
        case ERROR_DIRECTORY: errno = ENOTDIR; break;
        case ERROR_INVALID_NAME:
        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_PATHNAME: errno = EINVAL; break;
        case ERROR_NOT_SAME_DEVICE: errno = EXDEV; break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: errno = ENOSPC; break;
        case ERROR_TOO_MANY_OPEN_FILES: errno = EMFILE; break;
        default: errno = EIO; break;
    }
    return false;
}
static wchar_t *wide(const char *text)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!length || length > 32767) {
        errno = EILSEQ;
        return NULL;
    }
    wchar_t *out = xmalloc((size_t)length * sizeof *out);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, length)) {
        free(out);
        errno = EILSEQ;
        return NULL;
    }
    return out;
}
static char *utf8(const wchar_t *text, int length)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        errno = EILSEQ;
        return NULL;
    }
    char *out = xmalloc((size_t)bytes + 1);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, out, bytes, NULL, NULL)) {
        free(out);
        errno = EILSEQ;
        return NULL;
    }
    out[bytes] = 0;
    return out;
}
bool rooted_native_name_valid(const char *name)
{
    size_t size = strlen(name);
    if (!size || name[size - 1] == '.' || name[size - 1] == ' ') {
        errno = EINVAL;
        return false;
    }
    for (const unsigned char *at = (const unsigned char *)name; *at; at++) {
        if (*at < 32 || strchr("<>:\"/\\|?*", *at)) {
            errno = EINVAL;
            return false;
        }
    }
    char base[9] = "";
    size_t at = 0;
    while (name[at] && name[at] != '.' && at < sizeof base - 1) {
        base[at] = (char)toupper((unsigned char)name[at]);
        at++;
    }
    if (!strcmp(base, "CON") || !strcmp(base, "PRN") || !strcmp(base, "AUX") ||
        !strcmp(base, "NUL") ||
        ((at == 4) && (!memcmp(base, "COM", 3) || !memcmp(base, "LPT", 3)) &&
         base[3] >= '1' && base[3] <= '9')) {
        errno = EINVAL;
        return false;
    }
    return true;
}
static wchar_t *host_path(const char *path)
{
    wchar_t *input = wide(path);
    if (!input) {
        return NULL;
    }
    if (!wcsncmp(input, L"\\\\.\\", 4)) {
        free(input);
        errno = EINVAL;
        return NULL;
    }
    if (!wcsncmp(input, L"\\\\?\\", 4)) {
        bool drive = iswalpha(input[4]) && input[5] == L':' && input[6] == L'\\';
        bool unc = !_wcsnicmp(input + 4, L"UNC\\", 4);
        if (!drive && !unc) {
            free(input);
            errno = EINVAL;
            return NULL;
        }
        return input;
    }
    DWORD bytes = GetFullPathNameW(input, 0, NULL, NULL);
    if (!bytes || bytes > 32760) {
        free(input);
        windows_error(GetLastError());
        return NULL;
    }
    wchar_t *full = xmalloc(((size_t)bytes + 1) * sizeof *full);
    DWORD length = GetFullPathNameW(input, bytes + 1, full, NULL);
    free(input);
    if (!length || length > bytes) {
        free(full);
        windows_error(GetLastError());
        return NULL;
    }
    bool unc = full[0] == L'\\' && full[1] == L'\\';
    const wchar_t *prefix = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    const wchar_t *tail = full + (unc ? 2 : 0);
    size_t size = wcslen(prefix) + wcslen(tail) + 1;
    wchar_t *out = xmalloc(size * sizeof *out);
    wcscpy(out, prefix);
    wcscat(out, tail);
    free(full);
    return out;
}
static rooted_files *wrap(HANDLE handle, bool directory)
{
    rooted_files *out = xmalloc(sizeof *out);
    *out = (rooted_files){ .native = (intptr_t)handle, .directory = directory };
    return out;
}
static bool handle_info(HANDLE handle, rooted_file_info *out)
{
    FILE_BASIC_INFO basic;
    FILE_STANDARD_INFO standard;
    FILE_ID_INFO identity;
    if (GetFileType(handle) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof basic) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof standard)) {
        return windows_error(GetLastError());
    }
    if (standard.EndOfFile.QuadPart < 0) {
        errno = EIO;
        return false;
    }
    *out = (rooted_file_info){
        .bytes = (uint64_t)standard.EndOfFile.QuadPart,
        .modified_seconds = basic.LastWriteTime.QuadPart / 10000000,
        .modified_nanoseconds = (int)(basic.LastWriteTime.QuadPart % 10000000) * 100,
        .changed_seconds = basic.ChangeTime.QuadPart / 10000000,
        .changed_nanoseconds = (int)(basic.ChangeTime.QuadPart % 10000000) * 100,
        .permissions = basic.FileAttributes,
        .directory = (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        .link = (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
    };
    out->regular = !out->directory && !out->link;
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof identity)) {
        out->device = identity.VolumeSerialNumber;
        memcpy(out->identity, identity.FileId.Identifier, sizeof out->identity);
    } else {
        BY_HANDLE_FILE_INFORMATION fallback;
        if (!GetFileInformationByHandle(handle, &fallback)) {
            return windows_error(GetLastError());
        }
        out->device = fallback.dwVolumeSerialNumber;
        out->identity[0] = ((uint64_t)fallback.nFileIndexHigh << 32) |
                           fallback.nFileIndexLow;
    }
    return true;
}
rooted_files *rooted_native_root(const char *path)
{
    wchar_t *name = host_path(path);
    if (!name) {
        return NULL;
    }
    HANDLE handle = CreateFileW(
        name,
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );
    DWORD error = GetLastError();
    free(name);
    if (handle == INVALID_HANDLE_VALUE) {
        windows_error(error);
        return NULL;
    }
    rooted_file_info status;
    if (!handle_info(handle, &status) || !status.directory || status.link) {
        CloseHandle(handle);
        errno = EINVAL;
        return NULL;
    }
    return wrap(handle, true);
}
char *rooted_files_host_path(rooted_files *file)
{
    DWORD count = GetFinalPathNameByHandleW((HANDLE)file->native, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count > 32767) {
        windows_error(GetLastError());
        return NULL;
    }
    wchar_t *path = xmalloc(((size_t)count + 1) * sizeof *path);
    DWORD length = GetFinalPathNameByHandleW((HANDLE)file->native, path, count + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length > count) {
        free(path);
        windows_error(GetLastError());
        return NULL;
    }
    char *out = utf8(path, (int)length);
    free(path);
    if (!out) {
        return NULL;
    }
    if (!strncmp(out, "\\\\?\\UNC\\", 8)) {
        memmove(out + 2, out + 8, strlen(out + 8) + 1);
        out[0] = out[1] = '\\';
    } else if (!strncmp(out, "\\\\?\\", 4)) {
        memmove(out, out + 4, strlen(out + 4) + 1);
    }
    for (char *at = out; *at; at++) {
        if (*at == '\\') {
            *at = '/';
        }
    }
    return out;
}
rooted_files *rooted_native_duplicate(rooted_files *file)
{
    HANDLE handle;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            (HANDLE)file->native,
            GetCurrentProcess(),
            &handle,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS
        )) {
        windows_error(GetLastError());
        return NULL;
    }
    return wrap(handle, file->directory);
}
/* One literal child component relative to an already held directory handle.
 * FILE_OPEN_REPARSE_POINT opens the reparse object itself; handle inspection
 * rejects it before any content access or further traversal. */
static HANDLE child_handle(
    rooted_files *parent,
    const char *name,
    ACCESS_MASK access,
    ULONG disposition,
    bool create_directory,
    bool *directory
)
{
    if (!rooted_native_name_valid(name)) {
        return INVALID_HANDLE_VALUE;
    }
    wchar_t *text = wide(name);
    if (!text) {
        return INVALID_HANDLE_VALUE;
    }
    size_t bytes = wcslen(text) * sizeof *text;
    UNICODE_STRING unicode = { .Length = (USHORT)bytes,
                               .MaximumLength = (USHORT)(bytes + sizeof *text),
                               .Buffer = text };
    OBJECT_ATTRIBUTES attributes = { .Length = sizeof attributes,
                                     .RootDirectory = (HANDLE)parent->native,
                                     .ObjectName = &unicode,
                                     .Attributes = OBJ_CASE_INSENSITIVE };
    IO_STATUS_BLOCK io;
    HANDLE handle = INVALID_HANDLE_VALUE;
    NTSTATUS status = NtCreateFile(
        &handle,
        access | SYNCHRONIZE,
        &attributes,
        &io,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        disposition,
        FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT |
            (create_directory ? FILE_DIRECTORY_FILE : 0),
        NULL,
        0
    );
    free(text);
    if (status < 0) {
        windows_error(RtlNtStatusToDosError(status));
        return INVALID_HANDLE_VALUE;
    }
    rooted_file_info info;
    if (!handle_info(handle, &info) || info.link) {
        CloseHandle(handle);
        errno = ELOOP;
        return INVALID_HANDLE_VALUE;
    }
    *directory = info.directory;
    return handle;
}
rooted_files *rooted_native_child(
    rooted_files *parent,
    const char *name,
    bool directory,
    rooted_create create,
    unsigned permissions
)
{
    (void)permissions;
    bool actual;
    ACCESS_MASK access = directory ? FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES
                       : create ? GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE
                                : GENERIC_READ;
    HANDLE handle = child_handle(
        parent,
        name,
        access,
        create ? create == ROOTED_CREATE_OPEN ? FILE_OPEN_IF : FILE_CREATE : FILE_OPEN,
        directory && create,
        &actual
    );
    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    if (actual != directory) {
        CloseHandle(handle);
        errno = directory ? ENOTDIR : EINVAL;
        return NULL;
    }
    return wrap(handle, directory);
}
bool rooted_native_stat(rooted_files *parent, const char *name, rooted_file_info *out)
{
    bool directory;
    HANDLE handle = child_handle(parent, name, FILE_READ_ATTRIBUTES, FILE_OPEN, false, &directory);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = handle_info(handle, out);
    CloseHandle(handle);
    return ok;
}
rooted_stream *rooted_stream_external(const char *path)
{
    wchar_t *name = host_path(path);
    if (!name) {
        return NULL;
    }
    HANDLE handle = CreateFileW(
        name,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );
    DWORD error = GetLastError();
    free(name);
    if (handle == INVALID_HANDLE_VALUE) {
        windows_error(error);
        return NULL;
    }
    rooted_file_info info;
    if (!handle_info(handle, &info) || !info.regular) {
        CloseHandle(handle);
        errno = EINVAL;
        return NULL;
    }
    return wrap(handle, false);
}
bool rooted_stream_finish(rooted_stream *file)
{
    assert(file);
    bool ok = CloseHandle((HANDLE)file->native) != 0;
    DWORD error = GetLastError();
    free(file);
    return ok || windows_error(error);
}
void rooted_stream_close(rooted_stream *file)
{
    rooted_stream_finish(file);
}
void rooted_files_close(rooted_files *file)
{
    rooted_stream_close(file);
}
int64_t rooted_stream_read(rooted_stream *file, void *out, size_t size)
{
    DWORD bytes;
    if (!ReadFile((HANDLE)file->native, out, (DWORD)(size > INT_MAX ? INT_MAX : size), &bytes, NULL)) {
        windows_error(GetLastError());
        return -1;
    }
    return bytes;
}
int64_t rooted_stream_write(rooted_stream *file, const void *data, size_t size)
{
    DWORD bytes;
    if (!WriteFile((HANDLE)file->native, data, (DWORD)(size > INT_MAX ? INT_MAX : size), &bytes, NULL)) {
        windows_error(GetLastError());
        return -1;
    }
    return bytes;
}
bool rooted_stream_flush(rooted_stream *file)
{
    return FlushFileBuffers((HANDLE)file->native) || windows_error(GetLastError());
}
bool rooted_stream_info(rooted_stream *file, rooted_file_info *out)
{
    return handle_info((HANDLE)file->native, out);
}
bool rooted_native_permissions(rooted_stream *file, unsigned permissions)
{
    FILE_BASIC_INFO info = { .FileAttributes = permissions &
                                               ~(FILE_ATTRIBUTE_DIRECTORY |
                                                 FILE_ATTRIBUTE_REPARSE_POINT |
                                                 FILE_ATTRIBUTE_READONLY) };
    if (!info.FileAttributes) {
        info.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    return SetFileInformationByHandle((HANDLE)file->native, FileBasicInfo, &info, sizeof info) ||
           windows_error(GetLastError());
}
bool rooted_native_list(rooted_files *parent, int limit, rooted_file_entry **out, int *count)
{
    *out = NULL;
    *count = 0;
    union
    {
        max_align_t alignment;
        unsigned char bytes[65536];
    } buffer;
    bool restart = true, ok = true;
    for (;;) {
        if (!GetFileInformationByHandleEx(
                (HANDLE)parent->native,
                restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo,
                buffer.bytes,
                sizeof buffer.bytes
            )) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                windows_error(error);
                ok = false;
            }
            break;
        }
        restart = false;
        size_t offset = 0;
        for (;;) {
            FILE_ID_BOTH_DIR_INFO *row = (FILE_ID_BOTH_DIR_INFO *)(buffer.bytes + offset);
            assert(
                offset + offsetof(FILE_ID_BOTH_DIR_INFO, FileName) + row->FileNameLength <=
                sizeof buffer.bytes
            );
            char *name = utf8(row->FileName, (int)(row->FileNameLength / sizeof(wchar_t)));
            if (!name) {
                ok = false;
                break;
            }
            if (strcmp(name, ".") && strcmp(name, "..")) {
                if (*count >= limit) {
                    free(name);
                    errno = EFBIG;
                    ok = false;
                    break;
                }
                rooted_file_info info = {
                    .identity = { (uint64_t)row->FileId.QuadPart, 0 },
                    .bytes = (uint64_t)row->EndOfFile.QuadPart,
                    .modified_seconds = row->LastWriteTime.QuadPart / 10000000,
                    .modified_nanoseconds = (int)(row->LastWriteTime.QuadPart % 10000000) * 100,
                    .changed_seconds = row->ChangeTime.QuadPart / 10000000,
                    .changed_nanoseconds = (int)(row->ChangeTime.QuadPart % 10000000) * 100,
                    .permissions = row->FileAttributes,
                    .directory = (row->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                    .link = (row->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
                };
                info.regular = !info.directory && !info.link;
                *out = xrealloc(*out, (size_t)(*count + 1) * sizeof **out);
                (*out)[(*count)++] = (rooted_file_entry){ .name = name, .info = info };
            } else {
                free(name);
            }
            if (!row->NextEntryOffset) {
                break;
            }
            offset += row->NextEntryOffset;
            assert(offset < sizeof buffer.bytes);
        }
        if (!ok) {
            break;
        }
    }
    if (!ok) {
        rooted_file_entries_free(*out, *count);
        *out = NULL;
        *count = 0;
    }
    return ok;
}
bool rooted_native_move(rooted_files *from, const char *old, rooted_files *to, const char *name, bool replace)
{
    if (!rooted_native_name_valid(name)) {
        return false;
    }
    bool directory;
    HANDLE file = child_handle(from, old, DELETE | FILE_READ_ATTRIBUTES, FILE_OPEN, false, &directory);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    wchar_t *wide_name = wide(name);
    if (!wide_name) {
        CloseHandle(file);
        return false;
    }
    size_t bytes = wcslen(wide_name) * sizeof *wide_name;
    size_t size = offsetof(FILE_RENAME_INFO, FileName) + bytes;
    FILE_RENAME_INFO *info = xcalloc(1, size);
    info->ReplaceIfExists = replace;
    info->RootDirectory = (HANDLE)to->native;
    info->FileNameLength = (DWORD)bytes;
    memcpy(info->FileName, wide_name, bytes);
    bool ok = SetFileInformationByHandle(file, FileRenameInfo, info, (DWORD)size) != 0;
    DWORD error = GetLastError();
    free(info);
    free(wide_name);
    CloseHandle(file);
    return ok || windows_error(error);
}
static bool remove_child(rooted_files *parent, const char *name, bool expected_directory)
{
    bool directory;
    HANDLE file = child_handle(parent, name, DELETE | FILE_READ_ATTRIBUTES, FILE_OPEN, expected_directory, &directory);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (directory != expected_directory) {
        CloseHandle(file);
        errno = EINVAL;
        return false;
    }
    FILE_DISPOSITION_INFO info = { .DeleteFile = TRUE };
    bool ok = SetFileInformationByHandle(file, FileDispositionInfo, &info, sizeof info) !=
              0;
    DWORD error = GetLastError();
    CloseHandle(file);
    return ok || windows_error(error);
}
bool rooted_native_remove(rooted_files *parent, const char *name)
{
    return remove_child(parent, name, false);
}
bool rooted_native_remove_directory(rooted_files *parent, const char *name)
{
    return remove_child(parent, name, true);
}
#endif

bool rooted_native_name_equal(rooted_files *parent, const char *left, const char *right)
{
    FILE_CASE_SENSITIVE_INFO info = { 0 };
    bool sensitive = GetFileInformationByHandleEx(
                         (HANDLE)parent->native,
                         FileCaseSensitiveInfo,
                         &info,
                         sizeof info
                     ) &&
                     (info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR);
    wchar_t *a = wide(left), *b = wide(right);
    bool equal = a && b && CompareStringOrdinal(a, -1, b, -1, !sensitive) == CSTR_EQUAL;
    free(a);
    free(b);
    return equal;
}
