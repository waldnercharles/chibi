; A real seven-slot caller, with private callback state at byte28. The adapter
; must keep this pointer intact and restore its borrowed grant after every call.
target triple = "wasm32-unknown-emscripten"
%context = type { ptr, ptr, ptr, ptr, ptr, ptr, ptr, i32 }
%grant = type { i64, ptr, ptr }
@caller = internal global %context { ptr @chibi_format_text, ptr null, ptr @chibi_allocate, ptr @index_failure, ptr @release, ptr null, ptr @outer, i32 73 }
@legacy = internal global { [20 x i8], i32 } { [20 x i8] c"01234567890123456789", i32 73 }
@legacy.expected = private constant [20 x i8] c"01234567890123456789"
@grant = internal global %grant zeroinitializer
@outer = internal global %grant zeroinitializer
@platform = internal global %grant zeroinitializer
@invalid = internal global %grant { i64 1, ptr null, ptr null }
@mode = internal global i32 0
@sample = internal global i64 9007199254740993
@status = internal global i32 0
@error = internal global i32 5
@provider.calls = internal global i32 0
@program.calls = internal global i32 0
@allocations = internal global i32 0
@live = internal global i32 0

declare void @llvm.trap()
declare ptr @malloc(i32)
declare void @free(ptr)
declare i32 @memcmp(ptr, ptr, i32)
declare void @chibi_format_text(ptr, ptr, i32, ptr)
declare i32 @chibi_clock_context_size()
declare i64 @chibi_clock_context_abi()
declare i32 @chibi_clock_program(ptr, i32, ptr, ptr)
declare i32 @chibi_clock_grant_init(ptr, ptr, ptr)

define internal void @require(i1 %condition) {
  br i1 %condition, label %done, label %failed
failed:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
define internal void @callback_context(ptr %ctx) {
  %same = icmp eq ptr %ctx, @caller
  call void @require(i1 %same)
  %tail.at = getelementptr %context, ptr %ctx, i32 0, i32 7
  %tail = load i32, ptr %tail.at
  %valid = icmp eq i32 %tail, 73
  call void @require(i1 %valid)
  ret void
}
define ptr @chibi_allocate(ptr %ctx, i64 %bytes) {
  call void @callback_context(ptr %ctx)
  %bounded = icmp ule i64 %bytes, 2147483647
  call void @require(i1 %bounded)
  %size = trunc i64 %bytes to i32
  %memory = call ptr @malloc(i32 %size)
  %present = icmp ne ptr %memory, null
  call void @require(i1 %present)
  %before = load i32, ptr @allocations
  %after = add i32 %before, 1
  store i32 %after, ptr @allocations
  %used = load i32, ptr @live
  %next = add i32 %used, 1
  store i32 %next, ptr @live
  ret ptr %memory
}
define internal void @release(ptr %ctx, ptr %memory) {
  call void @callback_context(ptr %ctx)
  call void @free(ptr %memory)
  %used = load i32, ptr @live
  %valid = icmp sgt i32 %used, 0
  call void @require(i1 %valid)
  %next = sub i32 %used, 1
  store i32 %next, ptr @live
  ret void
}
define internal void @index_failure(ptr %ctx, i32 %index, i32 %length) {
  call void @llvm.trap()
  unreachable
}
define void @chibi_native_step() { ret void }
define void @chibi_native_failure(i32 %kind) noreturn {
  call void @llvm.trap()
  unreachable
}
define void @chibi_native_reference_unavailable() noreturn {
  call void @llvm.trap()
  unreachable
}
define i32 @TestClockMode() {
  %before = load i32, ptr @program.calls
  %after = add i32 %before, 1
  store i32 %after, ptr @program.calls
  %value = load i32, ptr @mode
  ret i32 %value
}
define i64 @TestClockExpectedDomain() {
  %value = load i64, ptr @grant
  ret i64 %value
}
define internal i32 @provider(ptr %state, ptr %nanoseconds, ptr %error.out) {
  %same = icmp eq ptr %state, @provider.calls
  call void @require(i1 %same)
  %before = load i32, ptr %state
  %after = add i32 %before, 1
  store i32 %after, ptr %state
  %value = load i64, ptr @sample
  %code = load i32, ptr @error
  store i64 %value, ptr %nanoseconds
  store i32 %code, ptr %error.out
  %result = load i32, ptr @status
  ret i32 %result
}

; Source failures return through the same context. No result escapes on failure,
; and the next call must clear that failure without losing the caller's state.
define internal void @run(i32 %mode.value, i32 %provider.status, ptr %chosen, i32 %expected.status, i32 %expected.value) {
  store i32 %mode.value, ptr @mode
  store i32 %provider.status, ptr @status
  %output = alloca i32
  store i32 -99, ptr %output
  %result = call i32 @chibi_clock_program(ptr @caller, i32 28, ptr %chosen, ptr %output)
  %status.valid = icmp eq i32 %result, %expected.status
  call void @require(i1 %status.valid)
  %value = load i32, ptr %output
  %value.valid = icmp eq i32 %value, %expected.value
  call void @require(i1 %value.valid)
  %grant.at = getelementptr %context, ptr @caller, i32 0, i32 6
  %restored = load ptr, ptr %grant.at
  %grant.valid = icmp eq ptr %restored, @outer
  call void @require(i1 %grant.valid)
  %failure.at = getelementptr %context, ptr @caller, i32 0, i32 1
  %failure = load ptr, ptr %failure.at
  %failed = icmp ne ptr %failure, null
  %expected.failed = icmp eq i32 %expected.status, 2
  %failure.valid = icmp eq i1 %failed, %expected.failed
  call void @require(i1 %failure.valid)
  %used = load i32, ptr @live
  %empty = icmp eq i32 %used, 0
  call void @require(i1 %empty)
  call void @callback_context(ptr @caller)
  ret void
}

define i32 @clock_test() {
  %size = call i32 @chibi_clock_context_size()
  %abi = call i64 @chibi_clock_context_abi()
  %sized = icmp eq i32 %size, 28
  %versioned = icmp eq i64 %abi, 2
  call void @require(i1 %sized)
  call void @require(i1 %versioned)
  %initialized = call i32 @chibi_clock_grant_init(ptr @grant, ptr @provider, ptr @provider.calls)
  %outer.initialized = call i32 @chibi_clock_grant_init(ptr @outer, ptr @provider, ptr @provider.calls)
  %platform.initialized = call i32 @chibi_clock_grant_init(ptr @platform, ptr null, ptr null)
  %initial.status = or i32 %initialized, %outer.initialized
  %all.status = or i32 %initial.status, %platform.initialized
  %initialized.ok = icmp eq i32 %all.status, 0
  call void @require(i1 %initialized.ok)
  %domain = load i64, ptr @grant
  %outer.domain = load i64, ptr @outer
  %named = icmp ne i64 %domain, 0
  %distinct = icmp ne i64 %domain, %outer.domain
  call void @require(i1 %named)
  call void @require(i1 %distinct)
  ; The ungranted call cannot inherit the nonnull outer grant.
  call void @run(i32 0, i32 0, ptr null, i32 0, i32 0)
  %denied.calls = load i32, ptr @provider.calls
  %denied = icmp eq i32 %denied.calls, 0
  call void @require(i1 %denied)
  call void @run(i32 1, i32 0, ptr @grant, i32 0, i32 0)
  call void @run(i32 2, i32 2, ptr @grant, i32 0, i32 0)
  call void @run(i32 3, i32 1, ptr @grant, i32 0, i32 0)
  store i64 -1, ptr @sample
  call void @run(i32 4, i32 0, ptr @grant, i32 0, i32 0)
  store i64 9007199254740993, ptr @sample
  call void @run(i32 6, i32 0, ptr @platform, i32 0, i32 0)
  call void @run(i32 7, i32 0, ptr null, i32 0, i32 0)
  call void @run(i32 5, i32 -1, ptr @grant, i32 2, i32 -99)
  call void @run(i32 1, i32 0, ptr @grant, i32 0, i32 0)
  call void @run(i32 5, i32 3, ptr @grant, i32 2, i32 -99)
  call void @run(i32 1, i32 0, ptr @grant, i32 0, i32 0)
  %unallocated = load i32, ptr @allocations
  %zero.allocations = icmp eq i32 %unallocated, 0
  call void @require(i1 %zero.allocations)
  call void @run(i32 9, i32 0, ptr null, i32 0, i32 42)
  %allocations = load i32, ptr @allocations
  %allocated = icmp sgt i32 %allocations, 0
  call void @require(i1 %allocated)
  ; External-input rejection must not read even a byte of the old five-slot
  ; caller, or dereference a deliberately unreadable grant after a bad extent.
  %output = alloca i32
  store i32 -99, ptr %output
  %calls.before = load i32, ptr @program.calls
  %saved.failure.at = getelementptr %context, ptr @caller, i32 0, i32 1
  store ptr inttoptr (i32 1 to ptr), ptr %saved.failure.at
  %old = call i32 @chibi_clock_program(ptr @legacy, i32 20, ptr inttoptr (i32 2147483640 to ptr), ptr %output)
  %old.rejected = icmp eq i32 %old, 1
  call void @require(i1 %old.rejected)
  %legacy.same = call i32 @memcmp(ptr @legacy, ptr @legacy.expected, i32 20)
  %untouched = icmp eq i32 %legacy.same, 0
  call void @require(i1 %untouched)
  %tail.at = getelementptr { [20 x i8], i32 }, ptr @legacy, i32 0, i32 1
  %tail = load i32, ptr %tail.at
  %tail.same = icmp eq i32 %tail, 73
  call void @require(i1 %tail.same)
  %bad = call i32 @chibi_clock_program(ptr @caller, i32 28, ptr @invalid, ptr %output)
  %bad.rejected = icmp eq i32 %bad, 1
  call void @require(i1 %bad.rejected)
  %null = call i32 @chibi_clock_program(ptr null, i32 28, ptr @grant, ptr %output)
  %null.rejected = icmp eq i32 %null, 1
  call void @require(i1 %null.rejected)
  %no.output = call i32 @chibi_clock_program(ptr @caller, i32 28, ptr @grant, ptr null)
  %no.output.rejected = icmp eq i32 %no.output, 1
  call void @require(i1 %no.output.rejected)
  %calls.after = load i32, ptr @program.calls
  %no.call = icmp eq i32 %calls.before, %calls.after
  call void @require(i1 %no.call)
  %unchanged.result = load i32, ptr %output
  %not.published = icmp eq i32 %unchanged.result, -99
  call void @require(i1 %not.published)
  %failure.after = load ptr, ptr %saved.failure.at
  %failure.untouched = icmp eq ptr %failure.after, inttoptr (i32 1 to ptr)
  call void @require(i1 %failure.untouched)
  %saved.grant.at = getelementptr %context, ptr @caller, i32 0, i32 6
  %grant.after = load ptr, ptr %saved.grant.at
  %grant.untouched = icmp eq ptr %grant.after, @outer
  call void @require(i1 %grant.untouched)
  call void @callback_context(ptr @caller)
  ret i32 0
}
