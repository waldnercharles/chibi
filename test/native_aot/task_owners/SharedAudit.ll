; The real image owns the allocator callbacks. Preserve their ABI while
; counting/poisoning every allocation, and require balance before dlclose.
@task_owner.shared.live = internal global i64 0
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @task_owner.shared.closed, ptr null }]
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
define internal ptr @task_owner.shared.allocate(ptr %ctx, i64 %bytes) {
  %reserved = add i64 %bytes, 16
  %allocation = call ptr @chibi_allocate(ptr %ctx, i64 %reserved)
  store i64 %bytes, ptr %allocation
  %memory = getelementptr i8, ptr %allocation, i64 16
  %live = load i64, ptr @task_owner.shared.live
  %next = add i64 %live, 1
  store i64 %next, ptr @task_owner.shared.live
  ret ptr %memory
}
define internal void @task_owner.shared.release(ptr %ctx, ptr %memory) {
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  %live = load i64, ptr @task_owner.shared.live
  %next = sub i64 %live, 1
  store i64 %next, ptr @task_owner.shared.live
  call void @chibi_release(ptr %ctx, ptr %allocation)
  ret void
}
define internal void @task_owner.shared.closed() {
  %live = load i64, ptr @task_owner.shared.live
  %balanced = icmp eq i64 %live, 0
  br i1 %balanced, label %done, label %failed
failed:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
