; Test-only callback audit against the actual standalone execution family.
; ABI3 has its owner at context field5 and requested flag at heap byte61.
@chibi_context = external global { ptr, ptr, ptr, ptr, ptr, ptr }
@directory_poll.enabled = internal global i1 false
@directory_poll.waiting = internal global i1 false
@directory_poll.release = internal global ptr null
@directory_poll.error = internal global i32 0
@directory_poll.releases = internal global i32 0
@directory_poll.bytes = internal global i64 0
@directory_poll.captured = internal global i1 false

define internal ptr @directory_poll.heap() {
  %at = getelementptr { ptr, ptr, ptr, ptr, ptr, ptr }, ptr @chibi_context, i32 0, i32 5
  %heap = load ptr, ptr %at
  ret ptr %heap
}
define i32 @chibi_directory_poll_install() {
  %at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr @chibi_context, i32 0, i32 4
  %original = load ptr, ptr %at
  store ptr %original, ptr @directory_poll.release
  store ptr @directory_poll.drop, ptr %at
  store i1 true, ptr @directory_poll.enabled
  ret i32 0
}
define internal void @directory_poll.reset() {
  store i1 false, ptr @directory_poll.waiting
  store i32 0, ptr @directory_poll.releases
  store i1 false, ptr @directory_poll.captured
  ret void
}
define internal void @directory_poll.request(i32 %error) {
  %enabled = load i1, ptr @directory_poll.enabled
  br i1 %enabled, label %request, label %done
request:
  %heap = call ptr @directory_poll.heap()
  %bytes.at = getelementptr i8, ptr %heap, i64 24
  %bytes = load i64, ptr %bytes.at
  store i64 %bytes, ptr @directory_poll.bytes
  %next.error = call i32 @directory_poll.replacement(i32 %error)
  store i32 %next.error, ptr @directory_poll.error
  store i1 true, ptr @directory_poll.waiting
  %pending = getelementptr i8, ptr %heap, i64 61
  store i1 true, ptr %pending
  br label %done
done:
  ret void
}
define internal void @directory_poll.drop(ptr %ctx, ptr %memory) {
  %original = load ptr, ptr @directory_poll.release
  call void %original(ptr %ctx, ptr %memory)
  %waiting = load i1, ptr @directory_poll.waiting
  br i1 %waiting, label %clobber, label %done
clobber:
  %before = load i32, ptr @directory_poll.releases
  %next = add i32 %before, 1
  store i32 %next, ptr @directory_poll.releases
  %error = load i32, ptr @directory_poll.error
  call void @directory_poll.set_error(i32 %error)
  br label %done
done:
  ret void
}
define internal void @directory_poll.capture() {
  %enabled = load i1, ptr @directory_poll.enabled
  br i1 %enabled, label %capture, label %done
capture:
  %releases = load i32, ptr @directory_poll.releases
  %called = icmp ugt i32 %releases, 0
  %before = load i64, ptr @directory_poll.bytes
  %heap = call ptr @directory_poll.heap()
  %bytes.at = getelementptr i8, ptr %heap, i64 24
  %after = load i64, ptr %bytes.at
  %collected = icmp ult i64 %after, %before
  %observed = and i1 %called, %collected
  store i1 %observed, ptr @directory_poll.captured
  br label %done
done:
  ret void
}
define i32 @chibi_directory_poll_observed() {
  %captured = load i1, ptr @directory_poll.captured
  %result = zext i1 %captured to i32
  ret i32 %result
}
