; Each managed cell owns one optional external allocation and one graph edge.
; Poisoned frees and exact live counts prove reclamation before process exit.
declare ptr @malloc(i64)
declare void @free(ptr)
declare i32 @printf(ptr, ...)
declare void @abort() noreturn
declare void @llvm.trap()
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
%managed_test.node = type { ptr, ptr, i64 }
@managed_test.live = internal global i64 0
@managed_test.peak = internal global i64 0
@managed_test.failure = private constant [30 x i8] c"managed pointer assertion %d\0A\00"
@managed_test.context = internal global { ptr, ptr, ptr, ptr, ptr } {
  ptr null, ptr null, ptr @managed_test.allocate, ptr null, ptr @managed_test.release
}
define internal void @managed_test.check(i1 %pass, i32 %site) {
entry:
  br i1 %pass, label %exit, label %bad
bad:
  %ignored = call i32 (ptr, ...) @printf(ptr @managed_test.failure, i32 %site)
  call void @abort()
  unreachable
exit:
  ret void
}
define internal void @managed_test.count(i64 %wanted, i32 %site) {
entry:
  %actual = load i64, ptr @managed_test.live
  %same = icmp eq i64 %actual, %wanted
  call void @managed_test.check(i1 %same, i32 %site)
  ret void
}
define internal ptr @managed_test.allocate(ptr %ctx, i64 %bytes) {
entry:
  %size = add i64 %bytes, 16
  %allocation = call ptr @malloc(i64 %size)
  %good = icmp ne ptr %allocation, null
  call void @managed_test.check(i1 %good, i32 1)
  store i64 %bytes, ptr %allocation
  %memory = getelementptr i8, ptr %allocation, i64 16
  %live = load i64, ptr @managed_test.live
  %next = add i64 %live, 1
  store i64 %next, ptr @managed_test.live
  %peak = load i64, ptr @managed_test.peak
  %higher = icmp ugt i64 %next, %peak
  %maximum = select i1 %higher, i64 %next, i64 %peak
  store i64 %maximum, ptr @managed_test.peak
  ret ptr %memory
}
define internal void @managed_test.release(ptr %ctx, ptr %memory) {
entry:
  %allocation = getelementptr i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  %live = load i64, ptr @managed_test.live
  %next = sub i64 %live, 1
  store i64 %next, ptr @managed_test.live
  call void @free(ptr %allocation)
  ret void
}
define internal void @managed_test.root(ptr %heap, ptr %slot) {
entry:
  %cell = load ptr, ptr %slot
  call void @chibi_managed.mark(ptr %heap, ptr %cell)
  ret void
}
define internal void @managed_test.trace(ptr %heap, ptr %payload) {
entry:
  %child = load ptr, ptr %payload
  call void @chibi_managed.mark(ptr %heap, ptr %child)
  ret void
}
define internal void @managed_test.drop(ptr %ctx, ptr %payload) {
entry:
  ; Never inspect the managed edge: a cycle peer may already have been swept.
  %external.at = getelementptr %managed_test.node, ptr %payload, i32 0, i32 1
  %external = load ptr, ptr %external.at
  %empty = icmp eq ptr %external, null
  br i1 %empty, label %exit, label %release
release:
  call void @managed_test.release(ptr %ctx, ptr %external)
  br label %exit
exit:
  ret void
}
define internal ptr @managed_test.new(ptr %heap, i64 %value, i1 %external) {
entry:
  %bytes = ptrtoint ptr getelementptr (%managed_test.node, ptr null, i32 1) to i64
  %cell = call ptr @chibi_managed.new(ptr @managed_test.context, ptr %heap, i64 %bytes,
                                    ptr @managed_test.trace, ptr @managed_test.drop)
  %payload = getelementptr %chibi.managed.cell, ptr %cell, i32 1
  %value.at = getelementptr %managed_test.node, ptr %payload, i32 0, i32 2
  store i64 %value, ptr %value.at
  br i1 %external, label %allocate, label %exit
allocate:
  %owner = call ptr @managed_test.allocate(ptr @managed_test.context, i64 8)
  %owner.at = getelementptr %managed_test.node, ptr %payload, i32 0, i32 1
  store ptr %owner, ptr %owner.at
  br label %exit
exit:
  ret ptr %cell
}
define internal i32 @managed_test.positive() {
entry:
  %heap = alloca %chibi.managed.heap
  %other = alloca %chibi.managed.heap
  %root = alloca %chibi.managed.root
  %second = alloca %chibi.managed.root
  %slot = alloca ptr
  %second.slot = alloca ptr
  store %chibi.managed.heap zeroinitializer, ptr %heap
  store %chibi.managed.heap zeroinitializer, ptr %other
  store ptr null, ptr %slot
  store ptr null, ptr %second.slot
  call void @chibi_managed.push(ptr %heap, ptr %root, ptr %slot, ptr @managed_test.root)
  %a = call ptr @managed_test.new(ptr %heap, i64 11, i1 true)
  %b = call ptr @managed_test.new(ptr %heap, i64 22, i1 true)
  %unrooted = call ptr @managed_test.new(ptr %heap, i64 33, i1 true)
  %ap = getelementptr %chibi.managed.cell, ptr %a, i32 1
  %bp = getelementptr %chibi.managed.cell, ptr %b, i32 1
  %up = getelementptr %chibi.managed.cell, ptr %unrooted, i32 1
  store ptr %b, ptr %ap
  store ptr %a, ptr %bp
  store ptr %unrooted, ptr %up
  store ptr %a, ptr %slot
  store ptr %b, ptr %second.slot
  call void @chibi_managed.push(ptr %heap, ptr %second, ptr %second.slot, ptr @managed_test.root)
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 4, i32 2)
  %av.at = getelementptr %managed_test.node, ptr %ap, i32 0, i32 2
  %av = load i64, ptr %av.at
  %av.good = icmp eq i64 %av, 11
  call void @managed_test.check(i1 %av.good, i32 3)
  store ptr null, ptr %slot
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 4, i32 4)
  call void @chibi_managed.pop(ptr %heap, ptr %second)
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 0, i32 5)
  ; A returned cell survives its creating function's stack.
  %escaped = call ptr @managed_test.new(ptr %heap, i64 99, i1 true)
  store ptr %escaped, ptr %slot
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 2, i32 6)
  %escaped.payload = getelementptr %chibi.managed.cell, ptr %escaped, i32 1
  %escaped.at = getelementptr %managed_test.node, ptr %escaped.payload, i32 0, i32 2
  %escaped.value = load i64, ptr %escaped.at
  %escaped.good = icmp eq i64 %escaped.value, 99
  call void @managed_test.check(i1 %escaped.good, i32 7)
  ; Independent heaps sharing one allocator collect only their own cells.
  %separate = call ptr @managed_test.new(ptr %other, i64 88, i1 true)
  store ptr %separate, ptr %second.slot
  call void @chibi_managed.push(ptr %other, ptr %second, ptr %second.slot, ptr @managed_test.root)
  store ptr null, ptr %slot
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 2, i32 8)
  call void @chibi_managed.pop(ptr %other, ptr %second)
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %other)
  call void @managed_test.count(i64 0, i32 9)
  store i64 0, ptr @managed_test.peak
  br label %churn
churn:
  %i = phi i32 [ 0, %entry ], [ %next, %allocate ]
  %done = icmp eq i32 %i, 100000
  br i1 %done, label %churn.done, label %allocate
allocate:
  %discarded = call ptr @managed_test.new(ptr %heap, i64 0, i1 false)
  %discarded.payload = getelementptr %chibi.managed.cell, ptr %discarded, i32 1
  store ptr %discarded, ptr %discarded.payload
  call void @chibi_managed.safepoint(ptr @managed_test.context, ptr %heap)
  %next = add i32 %i, 1
  br label %churn
churn.done:
  %peak = load i64, ptr @managed_test.peak
  %bounded = icmp ule i64 %peak, 1024
  call void @managed_test.check(i1 %bounded, i32 10)
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 0, i32 11)
  br label %chain
chain:
  %j = phi i32 [ 0, %churn.done ], [ %j.next, %chain.node ]
  %head = phi ptr [ null, %churn.done ], [ %node, %chain.node ]
  %built = icmp eq i32 %j, 10000
  br i1 %built, label %chain.done, label %chain.node
chain.node:
  %node = call ptr @managed_test.new(ptr %heap, i64 0, i1 false)
  %node.payload = getelementptr %chibi.managed.cell, ptr %node, i32 1
  store ptr %head, ptr %node.payload
  store ptr %node, ptr %slot
  call void @chibi_managed.safepoint(ptr @managed_test.context, ptr %heap)
  %j.next = add i32 %j, 1
  br label %chain
chain.done:
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 10000, i32 12)
  store ptr null, ptr %slot
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 0, i32 13)
  call void @chibi_managed.pop(ptr %heap, ptr %root)
  %head.after = load ptr, ptr %heap
  %empty = icmp eq ptr %head.after, null
  call void @managed_test.check(i1 %empty, i32 14)
  %used.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 3
  %used = load i64, ptr %used.at
  %zero = icmp eq i64 %used, 0
  call void @managed_test.check(i1 %zero, i32 15)
  ret i32 0
}
define internal i32 @managed_test.cross_owner() {
entry:
  %heap = alloca %chibi.managed.heap
  %other = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  store %chibi.managed.heap zeroinitializer, ptr %other
  %cell = call ptr @managed_test.new(ptr %other, i64 1, i1 false)
  call void @chibi_managed.mark(ptr %heap, ptr %cell)
  ret i32 0
}
define internal i32 @managed_test.wrong_pop() {
entry:
  %heap = alloca %chibi.managed.heap
  %root = alloca %chibi.managed.root
  store %chibi.managed.heap zeroinitializer, ptr %heap
  store %chibi.managed.root zeroinitializer, ptr %root
  call void @chibi_managed.pop(ptr %heap, ptr %root)
  ret i32 0
}
