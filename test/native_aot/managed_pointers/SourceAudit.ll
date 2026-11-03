; Instrument the real standalone context without replacing its heap owner.
; Poison freed storage and require complete reclamation before entry returns.
@managed_source.live = internal global i64 0
@managed_source.total = internal global i64 0
@managed_source.peak = internal global i64 0
@managed_source.report = private constant [44 x i8] c"managed source live %ld peak %ld total %ld\0A\00"
declare ptr @malloc(i64)
declare void @free(ptr)
declare i32 @dprintf(i32, ptr, ...)

define internal ptr @managed_source.allocate(ptr %ctx, i64 %bytes) {
entry:
  %total = load i64, ptr @managed_source.total
  %allocated = add i64 %total, 1
  store i64 %allocated, ptr @managed_source.total
  %count = load i64, ptr @managed_source.live
  %next = add i64 %count, 1
  store i64 %next, ptr @managed_source.live
  %peak = load i64, ptr @managed_source.peak
  %higher = icmp ugt i64 %next, %peak
  %maximum = select i1 %higher, i64 %next, i64 %peak
  store i64 %maximum, ptr @managed_source.peak
  %reserved = add i64 %bytes, 16
  %allocation = call ptr @malloc(i64 %reserved)
  %memory = getelementptr i8, ptr %allocation, i64 16
  store i64 %bytes, ptr %allocation
  ret ptr %memory
}

define internal void @managed_source.release(ptr %ctx, ptr %memory) {
entry:
  %count = load i64, ptr @managed_source.live
  %next = sub i64 %count, 1
  store i64 %next, ptr @managed_source.live
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  call void @free(ptr %allocation)
  ret void
}

define i32 @chibi_scalar(ptr %ctx) {
entry:
  %allocate.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 2
  %release.at = getelementptr { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 4
  %allocate = load ptr, ptr %allocate.at
  %release = load ptr, ptr %release.at
  store ptr @managed_source.allocate, ptr %allocate.at
  store ptr @managed_source.release, ptr %release.at
  %result = call i32 @managed_source.entry(ptr %ctx)
  store ptr %allocate, ptr %allocate.at
  store ptr %release, ptr %release.at
  %live = load i64, ptr @managed_source.live
  %peak = load i64, ptr @managed_source.peak
  %total = load i64, ptr @managed_source.total
  %printed = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @managed_source.report, i64 %live, i64 %peak, i64 %total)
  %balanced = icmp eq i64 %live, 0
  ; The threshold is 64 KiB and cells are at least 65 bytes. This bound also
  ; permits the few live array owners without growing with loop iterations.
  %bounded = icmp ule i64 %peak, 2048
  %valid = and i1 %balanced, %bounded
  %checked = select i1 %valid, i32 %result, i32 99
  ret i32 %checked
}
