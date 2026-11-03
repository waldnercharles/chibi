; Count and poison both images' actual allocations. The host owns the counter
; until every image and allocation is gone, so counted Text may survive the
; allocating image without leaving a pointer to unmapped audit globals.
@boundary.counter = internal global ptr null
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
define i32 @boundary_set_counter(ptr %counter) {
  store ptr %counter, ptr @boundary.counter
  ret i32 0
}
define i64 @boundary_live() {
  %counter = load ptr, ptr @boundary.counter
  %live = load i64, ptr %counter
  ret i64 %live
}
define internal ptr @boundary.allocate(ptr %ctx, i64 %bytes) {
  %reserved = add i64 %bytes, 16
  %allocation = call ptr @chibi_allocate(ptr %ctx, i64 %reserved)
  store i64 %bytes, ptr %allocation
  %counter = load ptr, ptr @boundary.counter
  %origin = getelementptr i8, ptr %allocation, i64 8
  store ptr %counter, ptr %origin
  %live = load i64, ptr %counter
  %next = add i64 %live, 1
  store i64 %next, ptr %counter
  %memory = getelementptr i8, ptr %allocation, i64 16
  ret ptr %memory
}
define internal void @boundary.release(ptr %ctx, ptr %memory) {
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  %origin = getelementptr i8, ptr %allocation, i64 8
  %counter = load ptr, ptr %origin
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  %live = load i64, ptr %counter
  %next = sub i64 %live, 1
  store i64 %next, ptr %counter
  call void @chibi_release(ptr %ctx, ptr %allocation)
  ret void
}
