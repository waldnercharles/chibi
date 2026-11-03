; These calls exercise the emitted descriptor checks and real pointer-array
; helpers, including the owner check before the shared trace epoch shortcut.
@managed_runtime.visits = internal global i64 0
%managed_runtime.record = type { i32, { ptr, i64, i64 } }
@managed_runtime.code_live = internal global i64 0
@managed_runtime.layout = private constant %chibi.managed.layout { ptr @managed_runtime.trace_record, ptr @managed_runtime.validate_record, ptr @managed_runtime.code_live }
@managed_runtime.untraced_schema = private constant %chibi.managed.layout { ptr null, ptr @managed_runtime.validate_record, ptr null }
define internal void @managed_runtime.trace_record(ptr %heap, ptr %payload) {
entry:
  %link = getelementptr %managed_runtime.record, ptr %payload, i32 0, i32 1
  call void @chibi_managed.trace.pointer(ptr %heap, ptr %link)
  ret void
}
define internal i1 @managed_runtime.validate_record(i64 %offset, i64 %tag, i64 %bytes, i32 %storage) {
entry:
  %at.root = icmp eq i64 %offset, 0
  %root.tag = icmp eq i64 %tag, -700
  %root.bytes = icmp eq i64 %bytes, 32
  %root.storage = icmp eq i32 %storage, 32
  %root.identity = and i1 %at.root, %root.tag
  %root.shape = and i1 %root.bytes, %root.storage
  %root = and i1 %root.identity, %root.shape
  %number.tag = icmp eq i64 %tag, -123
  %number.bytes = icmp eq i64 %bytes, 4
  %number.storage = icmp eq i32 %storage, 6
  %number.identity = and i1 %at.root, %number.tag
  %number.shape = and i1 %number.bytes, %number.storage
  %number = and i1 %number.identity, %number.shape
  %at.link = icmp eq i64 %offset, 8
  %link.tag = icmp eq i64 %tag, -701
  %link.bytes = icmp eq i64 %bytes, 24
  %link.storage = icmp eq i32 %storage, 16
  %link.identity = and i1 %at.link, %link.tag
  %link.shape = and i1 %link.bytes, %link.storage
  %link = and i1 %link.identity, %link.shape
  %scalars = or i1 %root, %number
  %valid = or i1 %scalars, %link
  ret i1 %valid
}
define internal { ptr, i64, i64 } @managed_runtime.new_record(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %child = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %cell = call ptr @chibi_managed.new(ptr %ctx, ptr %heap, i64 32, ptr @managed_runtime.layout, ptr null, i32 32)
  %payload = getelementptr %chibi.managed.cell, ptr %cell, i32 1
  store i32 11, ptr %payload
  %link = getelementptr %managed_runtime.record, ptr %payload, i32 0, i32 1
  store { ptr, i64, i64 } %child, ptr %link
  %root = insertvalue { ptr, i64, i64 } zeroinitializer, ptr %cell, 0
  %value = insertvalue { ptr, i64, i64 } %root, i64 -700, 2
  ret { ptr, i64, i64 } %value
}
define internal i32 @managed_runtime.typed_layout(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new_record(ptr %ctx)
  %slot = alloca { ptr, i64, i64 }
  %root = alloca %chibi.managed.root
  store { ptr, i64, i64 } %value, ptr %slot
  call void @chibi_managed.push(ptr %heap, ptr %root, ptr %slot, ptr @chibi_managed.trace.pointer)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %whole = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %value, i64 -700, i64 32, i32 32)
  %code.rooted = load atomic i64, ptr @managed_runtime.code_live acquire, align 8
  %code.pinned = icmp eq i64 %code.rooted, 1
  %number.value = insertvalue { ptr, i64, i64 } %value, i64 -123, 2
  %number.address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %number.value, i64 -123, i64 4, i32 6)
  %number = load i32, ptr %number.address
  %number.valid = icmp eq i32 %number, 11
  %offset = insertvalue { ptr, i64, i64 } %value, i64 8, 1
  %link.value = insertvalue { ptr, i64, i64 } %offset, i64 -701, 2
  %link = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %link.value, i64 -701, i64 24, i32 16)
  %child = load { ptr, i64, i64 }, ptr %link
  %child.address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %child, i64 -123, i64 4, i32 6)
  %child.number = load i32, ptr %child.address
  %child.valid = icmp eq i32 %child.number, 7
  %values.valid = and i1 %number.valid, %child.valid
  call void @chibi_managed.pop(ptr %heap, ptr %root)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %code.final = load atomic i64, ptr @managed_runtime.code_live acquire, align 8
  %code.released = icmp eq i64 %code.final, 0
  %code.valid = and i1 %code.pinned, %code.released
  %valid = and i1 %values.valid, %code.valid
  %result = select i1 %valid, i32 0, i32 96
  ret i32 %result
}
define internal i32 @managed_runtime.bad_layout_storage(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new_record(ptr %ctx)
  %offset = insertvalue { ptr, i64, i64 } %value, i64 8, 1
  %link = insertvalue { ptr, i64, i64 } %offset, i64 -701, 2
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %link, i64 -701, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.untraced_layout(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %cell = call ptr @chibi_managed.new(ptr %ctx, ptr %heap, i64 32, ptr @managed_runtime.untraced_schema, ptr null, i32 32)
  %payload = getelementptr %chibi.managed.cell, ptr %cell, i32 1
  store i32 17, ptr %payload
  %value = insertvalue { ptr, i64, i64 } { ptr null, i64 0, i64 -700 }, ptr %cell, 0
  %slot = alloca { ptr, i64, i64 }
  %root = alloca %chibi.managed.root
  store { ptr, i64, i64 } %value, ptr %slot
  call void @chibi_managed.push(ptr %heap, ptr %root, ptr %slot, ptr @chibi_managed.trace.pointer)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %field = insertvalue { ptr, i64, i64 } %value, i64 -123, 2
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %field, i64 -123, i64 4, i32 6)
  %number = load i32, ptr %address
  %result = sub i32 %number, 17
  call void @chibi_managed.pop(ptr %heap, ptr %root)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  ret i32 %result
}
define internal i32 @managed_runtime.bad_layout_identity(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new_record(ptr %ctx)
  %offset = insertvalue { ptr, i64, i64 } %value, i64 8, 1
  %link = insertvalue { ptr, i64, i64 } %offset, i64 -702, 2
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %link, i64 -702, i64 24, i32 16)
  ret i32 0
}
define internal i32 @managed_runtime.bad_layout_root(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %value = call { ptr, i64, i64 } @managed_runtime.new_record(ptr %ctx)
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %value, i64 -700, i64 4, i32 6)
  ret i32 0
}
define internal i32 @managed_runtime.code_underflow(ptr %ctx) {
entry:
  store atomic i64 0, ptr @managed_runtime.code_live release, align 8
  call void @chibi_managed.code(ptr @managed_runtime.layout, i64 -1)
  ret i32 0
}
define internal i32 @managed_runtime.code_overflow(ptr %ctx) {
entry:
  store atomic i64 -1, ptr @managed_runtime.code_live release, align 8
  call void @chibi_managed.code(ptr @managed_runtime.layout, i64 1)
  ret i32 0
}
define internal i32 @managed_runtime.no_layout_drop(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %cell = call ptr @chibi_managed.new(ptr %ctx, ptr %heap, i64 32, ptr null, ptr @managed_runtime.trace_record, i32 32)
  ret i32 0
}
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
  %heap.sized = icmp eq i64 %heap.bytes, 64
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

; Non-LIFO frame cancellation must not disturb other suspended roots, and a
; spawning function restoring its stack boundary must leave those roots live.
define internal i32 @managed_runtime.persistent(ptr %ctx) {
entry:
  %heap = call ptr @chibi_managed.owner(ptr %ctx)
  %node.bytes = ptrtoint ptr getelementptr (%chibi.managed.persistent, ptr null, i32 1) to i64
  %node.sized = icmp eq i64 %node.bytes, 32
  br i1 %node.sized, label %initialize, label %bad
initialize:
  %first = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %second = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %third = call { ptr, i64, i64 } @managed_runtime.new(ptr %ctx)
  %a = alloca { ptr, i64, i64 }
  %b = alloca { ptr, i64, i64 }
  %c = alloca { ptr, i64, i64 }
  store { ptr, i64, i64 } %first, ptr %a
  store { ptr, i64, i64 } %second, ptr %b
  store { ptr, i64, i64 } %third, ptr %c
  %ra = alloca %chibi.managed.persistent
  %rb = alloca %chibi.managed.persistent
  %rc = alloca %chibi.managed.persistent
  store %chibi.managed.persistent zeroinitializer, ptr %ra
  store %chibi.managed.persistent zeroinitializer, ptr %rb
  store %chibi.managed.persistent zeroinitializer, ptr %rc
  %stack = alloca %chibi.managed.root
  %stack.head = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 1
  %previous = load ptr, ptr %stack.head
  call void @chibi_managed.push(ptr %heap, ptr %stack, ptr %b, ptr @chibi_managed.trace.pointer)
  call void @chibi_managed.hold(ptr %heap, ptr %ra, ptr %a, ptr @chibi_managed.trace.pointer)
  call void @chibi_managed.hold(ptr %heap, ptr %rb, ptr %b, ptr @chibi_managed.trace.pointer)
  call void @chibi_managed.hold(ptr %heap, ptr %rc, ptr %c, ptr @chibi_managed.trace.pointer)
  store ptr %previous, ptr %stack.head
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %used.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 3
  %all.bytes = load i64, ptr %used.at
  %all.live = icmp eq i64 %all.bytes, 204
  br i1 %all.live, label %middle, label %bad
middle:
  call void @chibi_managed.unhold(ptr %rb)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %pair.bytes = load i64, ptr %used.at
  %pair.live = icmp eq i64 %pair.bytes, 136
  br i1 %pair.live, label %tail, label %bad
tail:
  call void @chibi_managed.unhold(ptr %ra)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %last.bytes = load i64, ptr %used.at
  %last.live = icmp eq i64 %last.bytes, 68
  %address = call ptr @chibi_managed.address(ptr %heap, { ptr, i64, i64 } %third, i64 -123, i64 4, i32 6)
  %value = load i32, ptr %address
  %correct = icmp eq i32 %value, 7
  %survived = and i1 %last.live, %correct
  br i1 %survived, label %head, label %bad
head:
  call void @chibi_managed.unhold(ptr %rc)
  call void @chibi_managed.unhold(ptr %rc)
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  %empty.bytes = load i64, ptr %used.at
  %empty = icmp eq i64 %empty.bytes, 0
  br i1 %empty, label %done, label %bad
done:
  ret i32 0
bad:
  ret i32 98
}
