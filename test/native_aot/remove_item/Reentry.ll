; Arm only the first equality scratch allocation. Older array allocation
; operations have their own reentry contracts and are outside this probe.
@value_equal_test.armed = internal global i1 false
@value_equal_test.collected = internal global i32 0

define i32 @chibi_value_equal_test_arm() {
entry:
  store i1 true, ptr @value_equal_test.armed
  ret i32 0
}

define internal ptr @managed_source.allocate(ptr %ctx, i64 %bytes) {
entry:
  %armed = load i1, ptr @value_equal_test.armed
  store i1 false, ptr @value_equal_test.armed
  br i1 %armed, label %collect, label %allocate
collect:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %before = load i32, ptr @value_equal_test.collected
  %next = add i32 %before, 1
  store i32 %next, ptr @value_equal_test.collected
  br label %allocate
allocate:
  %memory = call ptr @managed_source.allocate_base(ptr %ctx, i64 %bytes)
  ret ptr %memory
}
