; This provider is linked into the host, never into either guest image.
define i64 @TestClockProviderAddress() {
  ret i64 ptrtoint (ptr @clock.test.provider to i64)
}
define i64 @TestClockDomain(ptr %grant) {
  %domain = load i64, ptr %grant
  ret i64 %domain
}
define i64 @TestClockAddress(ptr %memory) {
  %address = ptrtoint ptr %memory to i64
  ret i64 %address
}
define i64 @TestClockCalls(ptr %memory) {
  %at = getelementptr i64, ptr %memory, i32 4
  %calls = load i64, ptr %at
  ret i64 %calls
}
define i32 @TestClockCollector(ptr %memory, i64 %callback) {
  %at = getelementptr i64, ptr %memory, i32 5
  %address = inttoptr i64 %callback to ptr
  store ptr %address, ptr %at
  ret i32 0
}
define i64 @TestClockHeapUsed(ptr %heap) {
  %at = getelementptr i64, ptr %heap, i32 3
  %used = load i64, ptr %at
  ret i64 %used
}
define i32 @TestClockConfigure(ptr %memory, i64 %time, i64 %increment, i32 %status, i32 %error) {
  %increment.at = getelementptr i64, ptr %memory, i32 1
  %status.at = getelementptr i64, ptr %memory, i32 2
  %error.at = getelementptr i64, ptr %memory, i32 3
  %status.word = sext i32 %status to i64
  %error.word = sext i32 %error to i64
  store i64 %time, ptr %memory
  store i64 %increment, ptr %increment.at
  store i64 %status.word, ptr %status.at
  store i64 %error.word, ptr %error.at
  ret i32 0
}
define private i32 @clock.test.provider(ptr %state, ptr %nanoseconds, ptr %error) {
  %collector.at = getelementptr i64, ptr %state, i32 5
  %collector = load ptr, ptr %collector.at
  %reenter = icmp ne ptr %collector, null
  br i1 %reenter, label %collect, label %sample
collect:
  %collected = call i32 %collector()
  %success = icmp eq i32 %collected, 0
  br i1 %success, label %sample, label %trap
trap:
  call void @llvm.trap()
  unreachable
sample:
  %increment.at = getelementptr i64, ptr %state, i32 1
  %status.at = getelementptr i64, ptr %state, i32 2
  %error.at = getelementptr i64, ptr %state, i32 3
  %calls.at = getelementptr i64, ptr %state, i32 4
  %time = load i64, ptr %state
  %increment = load i64, ptr %increment.at
  %status.word = load i64, ptr %status.at
  %error.word = load i64, ptr %error.at
  %calls = load i64, ptr %calls.at
  %next.time = add i64 %time, %increment
  %next.calls = add i64 %calls, 1
  store i64 %next.time, ptr %state
  store i64 %next.calls, ptr %calls.at
  store i64 %time, ptr %nanoseconds
  %code = trunc i64 %error.word to i32
  store i32 %code, ptr %error
  %status = trunc i64 %status.word to i32
  ret i32 %status
}
declare void @llvm.trap()
