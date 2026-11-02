; These calls exercise the emitted descriptor checks and real pointer-array
; helpers, including the owner check before the shared trace epoch shortcut.
@managed_runtime.visits = internal global i64 0
define internal void @managed_runtime.visit(ptr %heap, ptr %slot) {
entry:
  %before = load i64, ptr @managed_runtime.visits
  %after = add i64 %before, 1
  store i64 %after, ptr @managed_runtime.visits
  call void @chibi_managed.trace.pointer(ptr %heap, ptr %slot)
  ret void
}
define internal { ptr, i64, i64 } @managed_runtime.new(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %cell = call ptr @chibi_managed.new(ptr %ctx, ptr %heap, i64 4, ptr null, ptr null, i32 6)
  %payload = getelementptr %chibi.managed.cell, ptr %cell, i32 1
  store i32 7, ptr %payload
  %root = insertvalue { ptr, i64, i64 } zeroinitializer, ptr %cell, 0
  %value = insertvalue { ptr, i64, i64 } %root, i64 -123, 2
  ret { ptr, i64, i64 } %value
}
define internal i32 @managed_runtime.positive(ptr %ctx) {
entry:
  ; All admitted targets are 64-bit. Pin the high-count storage budgets.
  %heap.bytes = ptrtoint ptr getelementptr (%chibi.managed.heap, ptr null, i32 1) to i64
  %cell.bytes = ptrtoint ptr getelementptr (%chibi.managed.cell, ptr null, i32 1) to i64
  %root.bytes = ptrtoint ptr getelementptr (%chibi.managed.root, ptr null, i32 1) to i64
  %pointer.bytes = ptrtoint ptr getelementptr ({ ptr, i64, i64 }, ptr null, i32 1) to i64
  %heap.sized = icmp eq i64 %heap.bytes, 48
  %cell.sized = icmp eq i64 %cell.bytes, 64
  %root.sized = icmp eq i64 %root.bytes, 24
  %pointer.sized = icmp eq i64 %pointer.bytes, 24
  %owner.sized = and i1 %heap.sized, %cell.sized
  %values.sized = and i1 %root.sized, %pointer.sized
  %sized = and i1 %owner.sized, %values.sized
  br i1 %sized, label %descriptor, label %bad
descriptor:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %value, i64 -123, i64 4, i32 6)
  %number = load i32, ptr %address
  %correct = icmp eq i32 %number, 7
  br i1 %correct, label %arrays, label %bad
arrays:
  %array = call { i32, i32, ptr, ptr } @chibi_array.1040.allocate(ptr %ctx, i32 1, i32 1)
  %data = extractvalue { i32, i32, ptr, ptr } %array, 2
  %owner = extractvalue { i32, i32, ptr, ptr } %array, 3
  %data.word = ptrtoint ptr %data to i64
  %owner.word = ptrtoint ptr %owner to i64
  %prefix = sub i64 %data.word, %owner.word
  %prefixed = icmp eq i64 %prefix, 24
  br i1 %prefixed, label %share, label %bad
share:
  store { ptr, i64, i64 } %value, ptr %data
  %copy = call { i32, i32, ptr, ptr } @chibi_array.1040.copy(ptr %ctx, { i32, i32, ptr, ptr } %array)
  %slot = alloca { i32, i32, ptr, ptr }
  %alias = alloca { i32, i32, ptr, ptr }
  %root = alloca %chibi.managed.root
  store { i32, i32, ptr, ptr } %array, ptr %slot
  store { i32, i32, ptr, ptr } %copy, ptr %alias
  call void @chibi_managed.push(ptr %heap, ptr %root, ptr %slot, ptr @chibi_managed.trace.array)
  store i64 0, ptr @managed_runtime.visits
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  br label %aliases
aliases:
  %index = phi i32 [ 0, %share ], [ %next, %visit ]
  %done = icmp eq i32 %index, 10000
  br i1 %done, label %checked, label %visit
visit:
  call void @chibi_managed.trace.array(ptr %heap, ptr %slot)
  call void @chibi_managed.trace.array(ptr %heap, ptr %alias)
  %next = add i32 %index, 1
  br label %aliases
checked:
  %visits = load i64, ptr @managed_runtime.visits
  %once = icmp eq i64 %visits, 1
  br i1 %once, label %next_epoch, label %bad
next_epoch:
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %visits.after = load i64, ptr @managed_runtime.visits
  %twice = icmp eq i64 %visits.after, 2
  br i1 %twice, label %borrow, label %bad
borrow:
  ; A borrowed import must return and root its different owned descriptor.
  %borrowed.value = alloca { ptr, i64, i64 }
  store { ptr, i64, i64 } %value, ptr %borrowed.value
  %borrowed = insertvalue { i32, i32, ptr, ptr } { i32 1, i32 1, ptr null, ptr null }, ptr %borrowed.value, 2
  %imported = call { i32, i32, ptr, ptr } @chibi_array.1040.copy(ptr %ctx, { i32, i32, ptr, ptr } %borrowed)
  %imported.data = extractvalue { i32, i32, ptr, ptr } %imported, 2
  %imported.owner = extractvalue { i32, i32, ptr, ptr } %imported, 3
  %different = icmp ne ptr %imported.data, %borrowed.value
  %owned = icmp ne ptr %imported.owner, null
  %valid.import = and i1 %different, %owned
  br i1 %valid.import, label %release, label %bad
release:
  call void @chibi_array.1040.drop(ptr %ctx, { i32, i32, ptr, ptr } %imported)
  call void @chibi_managed.pop(ptr %heap, ptr %root)
  call void @chibi_array.1040.drop(ptr %ctx, { i32, i32, ptr, ptr } %copy)
  call void @chibi_array.1040.drop(ptr %ctx, { i32, i32, ptr, ptr } %array)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  ret i32 0
bad:
  ret i32 97
}
define internal i32 @managed_runtime.wrong_tag(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %value, i64 -124, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.wrong_storage(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %value, i64 -123, i64 4, i32 10)
  ret i32 0
}
define internal i32 @managed_runtime.wrong_range(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %offset = insertvalue { ptr, i64, i64 } %value, i64 1, 1
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %offset, i64 -123, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.nil(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } zeroinitializer, i64 -123, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.wrong_owner(ptr %ctx) {
entry:
  %other = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %other
  %value = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %other, { ptr, i64, i64 } %value, i64 -123, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.array_owner(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %array = call { i32, i32, ptr, ptr } @chibi_array.1040.allocate(ptr %ctx, i32 1, i32 1)
  %owner = extractvalue { i32, i32, ptr, ptr } %array, 3
  %epoch.at = getelementptr i8, ptr %owner, i64 8
  store i64 1, ptr %epoch.at
  %other = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %other
  %other.epoch = getelementptr %chibi.managed.heap, ptr %other, i32 0, i32 5
  store i64 1, ptr %other.epoch
  %slot = alloca { i32, i32, ptr, ptr }
  store { i32, i32, ptr, ptr } %array, ptr %slot
  call void @chibi_managed.trace.array(ptr %other, ptr %slot)
  ret i32 0
}
define internal i32 @managed_runtime.epoch_wrap(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %epoch = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 5
  store i64 -1, ptr %epoch
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  ret i32 0
}
