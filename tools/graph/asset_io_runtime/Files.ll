; POSIX cache/file boundary. Ownership is published before returning to authored
; CHECK sites; close and untrack happen together without an intervening callback.
target triple = "@TRIPLE@"

declare i32 @open(ptr, i32, ...)
declare i32 @close(i32)
declare i32 @fstat(i32, ptr)
declare i32 @lstat(ptr, ptr)
declare i32 @stat(ptr, ptr)
declare i32 @mkdir(ptr, i32)
declare i32 @unlink(ptr)
declare i32 @rename(ptr, ptr)
declare i32 @mkstemp(ptr)
declare i32 @fsync(i32)
declare i32 @flock(i32, i32)
declare ptr @opendir(ptr)
declare ptr @readdir(ptr)
declare i32 @closedir(ptr)
declare ptr @chibi_asset_errno()
declare i@WORD@ @read(i32, ptr, i@WORD@)
declare i@WORD@ @write(i32, ptr, i@WORD@)
declare i@WORD@ @strnlen(ptr, i@WORD@)
declare ptr @chibi_asset_call_context()
declare ptr @chibi_allocate(ptr, i64)
declare i32 @chibi_asset_resource_add(i64, i32)
declare i32 @chibi_asset_resource_remove(i64, i32)
declare i32 @chibi_asset_temp_remove(ptr)
declare void @llvm.trap() cold noreturn nounwind
declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1 immarg)

; file info is 16 little-endian bytes, size then nonnegative mtime seconds.
define internal i32 @chibi_asset_info(ptr %stat, ptr %output) {
  %mode.at = getelementptr i8, ptr %stat, i32 @MODE_OFFSET@
  %mode = load i@MODE_BITS@, ptr %mode.at, align 1
  %kind = and i@MODE_BITS@ %mode, @MODE_MASK@
  %regular = icmp eq i@MODE_BITS@ %kind, @MODE_REGULAR@
  br i1 %regular, label %values, label %other
values:
  %size.at = getelementptr i8, ptr %stat, i32 @SIZE_OFFSET@
  %size = load i64, ptr %size.at, align 1
  %stamp.at = getelementptr i8, ptr %stat, i32 @STAMP_OFFSET@
  %stamp = load i64, ptr %stamp.at, align 1
  %positive = icmp sge i64 %stamp, 0
  %effective = select i1 %positive, i64 %stamp, i64 0
  store i64 %size, ptr %output, align 1
  %output.stamp = getelementptr i8, ptr %output, i32 8
  store i64 %effective, ptr %output.stamp, align 1
  ret i32 0
other:
  ret i32 -1
}

define i32 @chibi_asset_file_info(i32 %fd, ptr %output) {
  %stat = alloca [@STAT_BYTES@ x i8], align 16
  %status = call i32 @fstat(i32 %fd, ptr %stat)
  %valid = icmp eq i32 %status, 0
  br i1 %valid, label %read, label %failed
read:
  %result = call i32 @chibi_asset_info(ptr %stat, ptr %output)
  ret i32 %result
failed:
  ret i32 -1
}
define i32 @chibi_asset_name_info(ptr %path, ptr %output) {
  %stat = alloca [@STAT_BYTES@ x i8], align 16
  %status = call i32 @lstat(ptr %path, ptr %stat)
  %valid = icmp eq i32 %status, 0
  br i1 %valid, label %read, label %failed
read:
  %result = call i32 @chibi_asset_info(ptr %stat, ptr %output)
  ret i32 %result
failed:
  ret i32 -1
}
define i32 @chibi_asset_file_open(ptr %path) {
  %fd = call i32 @chibi_asset_file_open_flags(ptr %path, i32 @READ_FLAGS@)
  ret i32 %fd
}
define i32 @chibi_asset_service_open(ptr %path) {
  %fd = call i32 @chibi_asset_file_open_flags(ptr %path, i32 @FOLLOW_FLAGS@)
  ret i32 %fd
}
define internal i32 @chibi_asset_file_open_flags(ptr %path, i32 %flags) {
  %fd = call i32 (ptr, i32, ...) @open(ptr %path, i32 %flags)
  %present = icmp sge i32 %fd, 0
  br i1 %present, label %check, label %failed
check:
  %info = alloca [16 x i8], align 8
  %status = call i32 @chibi_asset_file_info(i32 %fd, ptr %info)
  %regular = icmp eq i32 %status, 0
  br i1 %regular, label %track, label %close
track:
  %value = zext i32 %fd to i64
  %tracked = call i32 @chibi_asset_resource_add(i64 %value, i32 0)
  %owned = icmp eq i32 %tracked, 0
  br i1 %owned, label %done, label %broken
done:
  ret i32 %fd
close:
  %closed = call i32 @close(i32 %fd)
  br label %failed
failed:
  ret i32 -1
broken:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_asset_file_read(i32 %fd, ptr %buffer, i32 %first, i32 %count) {
  %at = getelementptr i8, ptr %buffer, i32 %first
  %size = zext i32 %count to i@WORD@
  %wide = call i@WORD@ @read(i32 %fd, ptr %at, i@WORD@ %size)
  %result = trunc i@WORD@ %wide to i32
  ret i32 %result
}
define i32 @chibi_asset_file_write(i32 %fd, ptr %buffer, i32 %first, i32 %count) {
  %at = getelementptr i8, ptr %buffer, i32 %first
  %size = zext i32 %count to i@WORD@
  %wide = call i@WORD@ @write(i32 %fd, ptr %at, i@WORD@ %size)
  %result = trunc i@WORD@ %wide to i32
  ret i32 %result
}
define i32 @chibi_asset_file_close(i32 %fd) {
  %closed = call i32 @close(i32 %fd)
  %value = zext i32 %fd to i64
  %removed = call i32 @chibi_asset_resource_remove(i64 %value, i32 0)
  %ordinary = icmp eq i32 %removed, 0
  br i1 %ordinary, label %done, label %lock
lock:
  %unlocked = call i32 @chibi_asset_resource_remove(i64 %value, i32 2)
  %owned = icmp eq i32 %unlocked, 0
  br i1 %owned, label %release, label %broken
release:
  call void @chibi_asset_publication_release()
  br label %done
done:
  ret i32 %closed
broken:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_asset_directory_make(ptr %path) {
  %made = call i32 @mkdir(ptr %path, i32 448)
  %created = icmp eq i32 %made, 0
  br i1 %created, label %done, label %exists
exists:
  %error.at = call ptr @chibi_asset_errno()
  %error = load i32, ptr %error.at
  %already = icmp eq i32 %error, @EXISTS_ERROR@
  br i1 %already, label %inspect, label %failed
inspect:
  %info = alloca [@STAT_BYTES@ x i8], align 16
  %found = call i32 @stat(ptr %path, ptr %info)
  %valid = icmp eq i32 %found, 0
  br i1 %valid, label %kind, label %failed
kind:
  %mode.at = getelementptr i8, ptr %info, i32 @MODE_OFFSET@
  %mode = load i@MODE_BITS@, ptr %mode.at, align 1
  %type = and i@MODE_BITS@ %mode, @MODE_MASK@
  %directory = icmp eq i@MODE_BITS@ %type, @MODE_DIRECTORY@
  br i1 %directory, label %done, label %failed
done:
  ret i32 0
failed:
  ret i32 -1
}
define i32 @chibi_asset_file_remove(ptr %path) {
  %removed = call i32 @unlink(ptr %path)
  %done = icmp eq i32 %removed, 0
  br i1 %done, label %success, label %missing
missing:
  %at = call ptr @chibi_asset_errno()
  %error = load i32, ptr %at
  %absent = icmp eq i32 %error, @MISSING_ERROR@
  br i1 %absent, label %untrack.missing, label %failure
untrack.missing:
  %missing.temp = call i32 @chibi_asset_temp_remove(ptr %path)
  ret i32 0
failure:
  ret i32 -1
success:
  %removed.temp = call i32 @chibi_asset_temp_remove(ptr %path)
  call void @chibi_asset_cache_changed()
  ret i32 0
}
define i32 @chibi_asset_file_rename(ptr %from, ptr %to) {
  %result = call i32 @rename(ptr %from, ptr %to)
  %renamed = icmp eq i32 %result, 0
  br i1 %renamed, label %changed, label %done
changed:
  %renamed.temp = call i32 @chibi_asset_temp_remove(ptr %from)
  call void @chibi_asset_cache_changed()
  br label %done
done:
  ret i32 %result
}
define i32 @chibi_asset_file_sync(i32 %fd) {
  %result = call i32 @fsync(i32 %fd)
  ret i32 %result
}

define i64 @chibi_asset_directory_open(ptr %path) {
  %directory = call ptr @opendir(ptr %path)
  %present = icmp ne ptr %directory, null
  br i1 %present, label %track, label %failed
track:
  %address = ptrtoint ptr %directory to i@WORD@
  %value = zext i@WORD@ %address to i64
  %tracked = call i32 @chibi_asset_resource_add(i64 %value, i32 1)
  %owned = icmp eq i32 %tracked, 0
  br i1 %owned, label %done, label %broken
done:
  ret i64 %value
failed:
  ret i64 0
broken:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_asset_directory_next(i64 %value, ptr %buffer, i32 %capacity) {
  %address = trunc i64 %value to i@WORD@
  %directory = inttoptr i@WORD@ %address to ptr
  %error.at = call ptr @chibi_asset_errno()
  store i32 0, ptr %error.at
  %entry = call ptr @readdir(ptr %directory)
  %present = icmp ne ptr %entry, null
  br i1 %present, label %name, label %ended
name:
  %bytes = getelementptr i8, ptr %entry, i32 @NAME_OFFSET@
  %wide.capacity = zext i32 %capacity to i@WORD@
  %length = call i@WORD@ @strnlen(ptr %bytes, i@WORD@ %wide.capacity)
  %fits = icmp ult i@WORD@ %length, %wide.capacity
  br i1 %fits, label %copy, label %failed
copy:
  %count = trunc i@WORD@ %length to i32
  call void @llvm.memcpy.p0.p0.i32(ptr %buffer, ptr %bytes, i32 %count, i1 false)
  ret i32 %count
ended:
  %error = load i32, ptr %error.at
  %clean = icmp eq i32 %error, 0
  %result = select i1 %clean, i32 0, i32 -1
  ret i32 %result
failed:
  ret i32 -1
}
define i32 @chibi_asset_directory_close(i64 %value) {
  %address = trunc i64 %value to i@WORD@
  %directory = inttoptr i@WORD@ %address to ptr
  %closed = call i32 @closedir(ptr %directory)
  %removed = call i32 @chibi_asset_resource_remove(i64 %value, i32 1)
  %owned = icmp eq i32 %removed, 0
  br i1 %owned, label %done, label %broken
done:
  ret i32 %closed
broken:
  call void @llvm.trap()
  unreachable
}

define i32 @chibi_asset_file_temporary(ptr %path) {
  %fd = call i32 @mkstemp(ptr %path)
  %valid = icmp sge i32 %fd, 0
  br i1 %valid, label %track, label %failed
track:
  %value = zext i32 %fd to i64
  %tracked = call i32 @chibi_asset_resource_add(i64 %value, i32 0)
  %length = call i@WORD@ @strnlen(ptr %path, i@WORD@ 4097)
  %bounded = icmp ule i@WORD@ %length, 4096
  %owned = icmp eq i32 %tracked, 0
  %safe = and i1 %bounded, %owned
  br i1 %safe, label %copy, label %broken
copy:
  %call = call ptr @chibi_asset_call_context()
  %count = add i@WORD@ %length, 1
  %wide = zext i@WORD@ %count to i64
  %saved = call ptr @chibi_allocate(ptr %call, i64 %wide)
  %narrow.count = trunc i@WORD@ %count to i32
  call void @llvm.memcpy.p0.p0.i32(ptr %saved, ptr %path, i32 %narrow.count, i1 false)
  %address = ptrtoint ptr %saved to i@WORD@
  %address.wide = zext i@WORD@ %address to i64
  %path.tracked = call i32 @chibi_asset_resource_add(i64 %address.wide, i32 3)
  %path.owned = icmp eq i32 %path.tracked, 0
  br i1 %path.owned, label %done, label %broken
done:
  ret i32 %fd
failed:
  ret i32 -1
broken:
  call void @llvm.trap()
  unreachable
}

define i32 @chibi_asset_file_lock(ptr %path) {
  %entered = call i1 @chibi_asset_publication_enter()
  br i1 %entered, label %open, label %failed
open:
  %fd = call i32 (ptr, i32, ...) @open(ptr %path, i32 @LOCK_FLAGS@, i32 384)
  %valid = icmp sge i32 %fd, 0
  br i1 %valid, label %inspect, label %release
inspect:
  %info = alloca [16 x i8], align 8
  %status = call i32 @chibi_asset_file_info(i32 %fd, ptr %info)
  %regular = icmp eq i32 %status, 0
  br i1 %regular, label %lock, label %close
lock:
  %locked = call i32 @flock(i32 %fd, i32 6)
  %acquired = icmp eq i32 %locked, 0
  br i1 %acquired, label %track, label %close
track:
  %value = zext i32 %fd to i64
  %tracked = call i32 @chibi_asset_resource_add(i64 %value, i32 2)
  %owned = icmp eq i32 %tracked, 0
  br i1 %owned, label %done, label %broken
done:
  ret i32 %fd
close:
  %closed = call i32 @close(i32 %fd)
  br label %release
release:
  call void @chibi_asset_publication_release()
  br label %failed
failed:
  ret i32 -1
broken:
  call void @llvm.trap()
  unreachable
}
