@mode = internal global i32 0
@reads = internal global i32 0
@active = internal global i32 0
declare dllimport i64 @FindFirstFileW(ptr, ptr)
declare dllimport i32 @FindNextFileW(i64, ptr)
declare dllimport i32 @FindClose(i64)
declare dllimport void @SetLastError(i32)

define i32 @chibi_directory_mode(i32 %mode) {
  %is_query = icmp eq i32 %mode, -1
  br i1 %is_query, label %report, label %select
report:
  %active = load i32, ptr @active
  ret i32 %active
select:
  store i32 %mode, ptr @mode
  store i32 0, ptr @reads
  ret i32 %mode
}
define i64 @chibi_directory_open(ptr %pattern, ptr %data) {
  %mode = load i32, ptr @mode
  %denied = icmp eq i32 %mode, 1
  br i1 %denied, label %failed, label %check_empty
failed:
  call void @SetLastError(i32 5)
  ret i64 -1
check_empty:
  %empty = icmp eq i32 %mode, 3
  br i1 %empty, label %end, label %open
end:
  call void @SetLastError(i32 2)
  ret i64 -1
open:
  %handle = call i64 @FindFirstFileW(ptr %pattern, ptr %data)
  %valid = icmp ne i64 %handle, -1
  %opened = zext i1 %valid to i32
  %active = load i32, ptr @active
  %next = add i32 %active, %opened
  store i32 %next, ptr @active
  store i32 1, ptr @reads
  ret i64 %handle
}
define i32 @chibi_directory_read(i64 %handle, ptr %data) {
  %mode = load i32, ptr @mode
  %count = load i32, ptr @reads
  %ready = icmp uge i32 %count, 4
  %partial = icmp eq i32 %mode, 2
  %fail = and i1 %ready, %partial
  br i1 %fail, label %failed, label %read
failed:
  call void @SetLastError(i32 5)
  ret i32 0
read:
  %result = call i32 @FindNextFileW(i64 %handle, ptr %data)
  %next = add i32 %count, 1
  store i32 %next, ptr @reads
  ret i32 %result
}
define i32 @chibi_directory_close(i64 %handle) {
  %result = call i32 @FindClose(i64 %handle)
  %valid = icmp ne i32 %result, 0
  %closed = zext i1 %valid to i32
  %active = load i32, ptr @active
  %next = sub i32 %active, %closed
  store i32 %next, ptr @active
  %mode = load i32, ptr @mode
  %fail = icmp eq i32 %mode, 4
  %reported = select i1 %fail, i32 0, i32 %result
  ret i32 %reported
}
