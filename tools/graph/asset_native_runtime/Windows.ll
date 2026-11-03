; The public facade's bounded service reader uses Unicode CRT descriptors.
; Persistent cache storage remains unavailable on Windows, as in the C API.
declare dllimport i32 @MultiByteToWideChar(i32, i32, ptr, i32, ptr, i32)
; Leave CRT binding to the consumer's /MT or /MD choice. All output is allocated
; and freed through that same CRT; file descriptors never cross this boundary.
declare i32 @_wopen(ptr, i32, ...)
declare i32 @_close(i32)
declare i32 @_read(i32, ptr, i32)
declare i64 @_filelengthi64(i32)
declare i64 @_get_osfhandle(i32)
declare dllimport i32 @GetFileType(ptr)

; Temporary directory names use core's BCrypt random source. COFF's standard
; dependency directive carries this system library to arbitrary C consumers.
!llvm.linker.options = !{!0}
!0 = !{!"/DEFAULTLIB:bcrypt.lib"}

define i32 @chibi_asset_service_open(ptr %path) {
entry:
  %wide = alloca [4096 x i16], align 2
  %count = call i32 @MultiByteToWideChar(i32 65001, i32 8, ptr %path, i32 -1, ptr %wide, i32 4096)
  %valid = icmp sgt i32 %count, 0
  br i1 %valid, label %open, label %failed
open:
  ; _O_BINARY | _O_NOINHERIT, _O_RDONLY: exact bytes, no reader handle
  ; escapes into another thread's concurrently launched service.
  %fd = call i32 (ptr, i32, ...) @_wopen(ptr %wide, i32 32896)
  %present = icmp sge i32 %fd, 0
  br i1 %present, label %inspect, label %failed
inspect:
  %native = call i64 @_get_osfhandle(i32 %fd)
  %handle = inttoptr i64 %native to ptr
  %kind = call i32 @GetFileType(ptr %handle)
  %disk = icmp eq i32 %kind, 1
  br i1 %disk, label %track, label %close
track:
  %value = zext i32 %fd to i64
  %tracked = call i32 @chibi_asset_resource_add(i64 %value, i32 0)
  %owned = icmp eq i32 %tracked, 0
  br i1 %owned, label %done, label %broken
done:
  ret i32 %fd
close:
  %closed = call i32 @_close(i32 %fd)
  br label %failed
failed:
  ret i32 -1
broken:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_asset_file_info(i32 %fd, ptr %output) {
entry:
  %size = call i64 @_filelengthi64(i32 %fd)
  %valid = icmp sge i64 %size, 0
  br i1 %valid, label %copy, label %failed
copy:
  store i64 %size, ptr %output, align 1
  %stamp = getelementptr i8, ptr %output, i32 8
  store i64 0, ptr %stamp, align 1
  ret i32 0
failed:
  ret i32 -1
}
define i32 @chibi_asset_file_read(i32 %fd, ptr %buffer, i32 %first, i32 %count) {
  %at = getelementptr i8, ptr %buffer, i32 %first
  %read = call i32 @_read(i32 %fd, ptr %at, i32 %count)
  ret i32 %read
}
define i32 @chibi_asset_file_close(i32 %fd) {
entry:
  %closed = call i32 @_close(i32 %fd)
  %value = zext i32 %fd to i64
  %removed = call i32 @chibi_asset_resource_remove(i64 %value, i32 0)
  %owned = icmp eq i32 %removed, 0
  br i1 %owned, label %done, label %broken
done:
  ret i32 %closed
broken:
  call void @llvm.trap()
  unreachable
}

; Only CRT file descriptors can be acquired by this target. Unsupported
; resource kinds are rejected before registration, never interpreted as handles.
define internal void @chibi_asset_native_close_resources(ptr %call) {
entry:
  %head.at = getelementptr %call, ptr %call, i32 0, i32 14
  br label %next
next:
  %node = load ptr, ptr %head.at
  %present = icmp ne ptr %node, null
  br i1 %present, label %read, label %done
read:
  %following = load ptr, ptr %node
  %value.at = getelementptr %resource, ptr %node, i32 0, i32 1
  %value = load i64, ptr %value.at
  %kind.at = getelementptr %resource, ptr %node, i32 0, i32 2
  %kind = load i32, ptr %kind.at
  %descriptor = icmp eq i32 %kind, 0
  br i1 %descriptor, label %close, label %broken
close:
  store ptr %following, ptr %head.at
  %fd = trunc i64 %value to i32
  %closed = call i32 @_close(i32 %fd)
  call void @chibi_asset_native_release(ptr %call, ptr %node)
  br label %next
broken:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
