; Check the runtime allocator boundary without a C test host. The native link's
; start module calls this entry, which supplies a counting allocation context.
@chibi_task_test_live = internal global i64 0
@chibi_task_test_ctx = internal global { ptr, ptr, ptr, ptr, ptr } { ptr null, ptr null, ptr @chibi_task_test_allocate, ptr null, ptr @chibi_task_test_release }
declare ptr @malloc(i64)
declare void @free(ptr)

define internal ptr @chibi_task_test_allocate(ptr %ctx, i64 %bytes) {
entry:
  %count = load i64, ptr @chibi_task_test_live
  %next = add i64 %count, 1
  store i64 %next, ptr @chibi_task_test_live
  %memory = call ptr @malloc(i64 %bytes)
  ret ptr %memory
}

define internal void @chibi_task_test_release(ptr %ctx, ptr %memory) {
entry:
  %count = load i64, ptr @chibi_task_test_live
  %next = sub i64 %count, 1
  store i64 %next, ptr @chibi_task_test_live
  call void @free(ptr %memory)
  ret void
}

define i32 @chibi_scalar(ptr %ctx) {
entry:
  %result = call i32 @chibi_task_test_entry(ptr @chibi_task_test_ctx)
  %failure.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr @chibi_task_test_ctx, i32 0, i32 1
  %failure = load ptr, ptr %failure.at
  %destination = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 1
  %live = load i64, ptr @chibi_task_test_live
  %balanced = icmp eq i64 %live, 0
  %checked.failure = select i1 %balanced, ptr %failure, ptr null
  store ptr %checked.failure, ptr %destination
  %checked = select i1 %balanced, i32 %result, i32 99
  ret i32 %checked
}
