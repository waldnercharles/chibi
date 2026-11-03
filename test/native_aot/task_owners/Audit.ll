; Instrument the real standalone context without replacing its heap owner.
; Poison freed storage and require complete reclamation before entry returns.
@task_owner.live = internal global i64 0
@task_owner.total = internal global i64 0
@task_owner.peak = internal global i64 0
@task_owner.original.allocate = internal global ptr null
@task_owner.original.release = internal global ptr null

; Read-only test seam for allocation growth assertions in source fixtures.
define i64 @chibi_task_owner_allocations() {
entry:
  %total = load i64, ptr @task_owner.total
  ret i64 %total
}

define internal ptr @task_owner.allocate(ptr %ctx, i64 %bytes) {
entry:
  %total = load i64, ptr @task_owner.total
  %allocated = add i64 %total, 1
  store i64 %allocated, ptr @task_owner.total
  %count = load i64, ptr @task_owner.live
  %next = add i64 %count, 1
  store i64 %next, ptr @task_owner.live
  %peak = load i64, ptr @task_owner.peak
  %higher = icmp ugt i64 %next, %peak
  %maximum = select i1 %higher, i64 %next, i64 %peak
  store i64 %maximum, ptr @task_owner.peak
  %reserved = add i64 %bytes, 16
  %allocate = load ptr, ptr @task_owner.original.allocate
  %allocation = call ptr %allocate(ptr %ctx, i64 %reserved)
  %memory = getelementptr i8, ptr %allocation, i64 16
  store i64 %bytes, ptr %allocation
  ret ptr %memory
}

define internal void @task_owner.release(ptr %ctx, ptr %memory) {
entry:
  %count = load i64, ptr @task_owner.live
  %next = sub i64 %count, 1
  store i64 %next, ptr @task_owner.live
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  %release = load ptr, ptr @task_owner.original.release
  call void %release(ptr %ctx, ptr %allocation)
  ret void
}

define i32 @chibi_scalar(ptr %ctx) {
entry:
  %allocate.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 2
  %release.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 4
  %allocate = load ptr, ptr %allocate.at
  %release = load ptr, ptr %release.at
  store ptr %allocate, ptr @task_owner.original.allocate
  store ptr %release, ptr @task_owner.original.release
  store ptr @task_owner.allocate, ptr %allocate.at
  store ptr @task_owner.release, ptr %release.at
  %result = call i32 @task_owner.entry(ptr %ctx)
  store ptr %allocate, ptr %allocate.at
  store ptr %release, ptr %release.at
  %live = load i64, ptr @task_owner.live
  %peak = load i64, ptr @task_owner.peak
  %total = load i64, ptr @task_owner.total
  %balanced = icmp eq i64 %live, 0
  ; Repeated awaits and cancellations must release each iteration's owners.
  ; This ceiling is independent of the 64 repeated task creations in Main.
  %bounded = icmp ule i64 %peak, 2048
  %valid = and i1 %balanced, %bounded
  %failure.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 1
  %failure = load ptr, ptr %failure.at
  %checked.failure = select i1 %valid, ptr %failure, ptr null
  store ptr %checked.failure, ptr %failure.at
  %checked = select i1 %valid, i32 %result, i32 99
  ret i32 %checked
}
