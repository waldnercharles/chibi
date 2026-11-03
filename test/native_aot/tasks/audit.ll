; Check the runtime allocator boundary without a C test host. The native link's
; start module calls this entry, which supplies a counting allocation context.
@chibi_task_test_live = internal global i64 0
@chibi_task_test_peak = internal global i64 0
@chibi_task_test_limit = internal constant i64 0
@chibi_task_test_bytes = internal global i64 0
@chibi_task_test_peak_bytes = internal global i64 0
@chibi_task_test_ctx = internal global { ptr, ptr, ptr, ptr, ptr } { ptr null, ptr null, ptr @chibi_task_test_allocate, ptr null, ptr @chibi_task_test_release }
declare ptr @malloc(i64)
declare void @free(ptr)

define internal ptr @chibi_task_test_allocate(ptr %ctx, i64 %bytes) {
entry:
  %count = load i64, ptr @chibi_task_test_live
  %next = add i64 %count, 1
  store i64 %next, ptr @chibi_task_test_live
  %peak = load i64, ptr @chibi_task_test_peak
  %higher = icmp ugt i64 %next, %peak
  %maximum = select i1 %higher, i64 %next, i64 %peak
  store i64 %maximum, ptr @chibi_task_test_peak
  %live.bytes = load i64, ptr @chibi_task_test_bytes
  %next.bytes = add i64 %live.bytes, %bytes
  store i64 %next.bytes, ptr @chibi_task_test_bytes
  %peak.bytes = load i64, ptr @chibi_task_test_peak_bytes
  %more.bytes = icmp ugt i64 %next.bytes, %peak.bytes
  %maximum.bytes = select i1 %more.bytes, i64 %next.bytes, i64 %peak.bytes
  store i64 %maximum.bytes, ptr @chibi_task_test_peak_bytes
  %size = add i64 %bytes, 16
  %allocation = call ptr @malloc(i64 %size)
  store i64 %bytes, ptr %allocation
  %memory = getelementptr i8, ptr %allocation, i64 16
  ret ptr %memory
}

define internal void @chibi_task_test_release(ptr %ctx, ptr %memory) {
entry:
  %count = load i64, ptr @chibi_task_test_live
  %next = sub i64 %count, 1
  store i64 %next, ptr @chibi_task_test_live
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  %live.bytes = load i64, ptr @chibi_task_test_bytes
  %next.bytes = sub i64 %live.bytes, %bytes
  store i64 %next.bytes, ptr @chibi_task_test_bytes
  call void @free(ptr %allocation)
  ret void
}

define i32 @chibi_scalar(ptr %ctx) {
entry:
  %result = call i32 @chibi_task_test_entry(ptr @chibi_task_test_ctx)
  %failure.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr @chibi_task_test_ctx, i32 0, i32 1
  %failure = load ptr, ptr %failure.at
  %destination = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 1
  %live = load i64, ptr @chibi_task_test_live
  %empty = icmp eq i64 %live, 0
  %peak = load i64, ptr @chibi_task_test_peak
  %limit = load i64, ptr @chibi_task_test_limit
  %unbounded = icmp eq i64 %limit, 0
  %within = icmp ule i64 %peak, %limit
  %peak.bytes = load i64, ptr @chibi_task_test_peak_bytes
  %within.bytes = icmp ule i64 %peak.bytes, 65536
  %within.both = and i1 %within, %within.bytes
  %bounded = or i1 %unbounded, %within.both
  %bytes = load i64, ptr @chibi_task_test_bytes
  %zero.bytes = icmp eq i64 %bytes, 0
  %empty.both = and i1 %empty, %zero.bytes
  %balanced = and i1 %empty.both, %bounded
  %checked.failure = select i1 %balanced, ptr %failure, ptr null
  store ptr %checked.failure, ptr %destination
  %checked = select i1 %balanced, i32 %result, i32 99
  ret i32 %checked
}
