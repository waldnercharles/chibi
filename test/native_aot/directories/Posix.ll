; Test-only wrappers retain real directory handles and inject deterministic
; open, partial-read and close failures. No C shim or process-wide interposer.
@mode = internal global i32 0
@reads = internal global i32 0
@active = internal global i32 0
declare ptr @opendir(ptr)
declare ptr @readdir(ptr)
declare i32 @closedir(ptr)
declare ptr @@ERRNO@()

define i32 @chibi_directory_mode(i32 %mode) {
  %is_query = icmp eq i32 %mode, -1
  br i1 %is_query, label %report, label %select
report:
  %active = load i32, ptr @active
  ret i32 %active
select:
  call void @directory_poll.reset()
  store i32 %mode, ptr @mode
  store i32 0, ptr @reads
  ret i32 %mode
}
define i64 @chibi_directory_open(ptr %path) {
  %mode = load i32, ptr @mode
  %fail = icmp eq i32 %mode, 1
  br i1 %fail, label %failed, label %open
failed:
  %error = call ptr @@ERRNO@()
  call void @directory_poll.request(i32 13)
  store i32 13, ptr %error
  ret i64 0
open:
  %directory = call ptr @opendir(ptr %path)
  %valid = icmp ne ptr %directory, null
  %opened = zext i1 %valid to i32
  %active = load i32, ptr @active
  %next = add i32 %active, %opened
  store i32 %next, ptr @active
  %result = ptrtoint ptr %directory to i64
  ret i64 %result
}
define i64 @chibi_directory_read(i64 %handle) {
  %mode = load i32, ptr @mode
  %empty = icmp eq i32 %mode, 3
  br i1 %empty, label %end, label %check
end:
  %zero = call ptr @@ERRNO@()
  call void @directory_poll.request(i32 0)
  store i32 0, ptr %zero
  ret i64 0
check:
  %count = load i32, ptr @reads
  %ready = icmp uge i32 %count, 4
  %partial = icmp eq i32 %mode, 2
  %fail = and i1 %ready, %partial
  br i1 %fail, label %failed, label %read
failed:
  %error = call ptr @@ERRNO@()
  call void @directory_poll.request(i32 5)
  store i32 5, ptr %error
  ret i64 0
read:
  %directory = inttoptr i64 %handle to ptr
  %entry = call ptr @readdir(ptr %directory)
  %next = add i32 %count, 1
  store i32 %next, ptr @reads
  %missing = icmp eq ptr %entry, null
  br i1 %missing, label %finished, label %done
finished:
  %error.at = call ptr @@ERRNO@()
  %error.code = load i32, ptr %error.at
  call void @directory_poll.request(i32 %error.code)
  br label %done
done:
  %result = ptrtoint ptr %entry to i64
  ret i64 %result
}
define i32 @chibi_directory_close(i64 %handle) {
  %directory = inttoptr i64 %handle to ptr
  %result = call i32 @closedir(ptr %directory)
  %valid = icmp eq i32 %result, 0
  %closed = zext i1 %valid to i32
  %active = load i32, ptr @active
  %next = sub i32 %active, %closed
  store i32 %next, ptr @active
  %mode = load i32, ptr @mode
  %fail = icmp eq i32 %mode, 4
  %reported = select i1 %fail, i32 -1, i32 %result
  ret i32 %reported
}

define internal i32 @directory_poll.replacement(i32 %error) {
  %clear = icmp ne i32 %error, 0
  %replacement = select i1 %clear, i32 0, i32 77
  ret i32 %replacement
}
define internal void @directory_poll.set_error(i32 %value) {
  %error = call ptr @@ERRNO@()
  store i32 %value, ptr %error
  ret void
}

; Freeze the callback/collection assertion before the actual TLS error read.
define i64 @chibi_directory_errno_copy(ptr %destination, i64 %source, i64 %bytes) {
  call void @directory_poll.capture()
  %address = inttoptr i64 %source to ptr
  %error = load i32, ptr %address, align 1
  store i32 %error, ptr %destination, align 1
  %result = ptrtoint ptr %destination to i64
  ret i64 %result
}
