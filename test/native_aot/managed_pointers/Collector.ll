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
@managed_test.code_live = internal global i64 0
@managed_test.callback_heap = internal global ptr null
@managed_test.allocate_collects = internal global i1 false
@managed_test.release_collects = internal global i1 false
@managed_test.recursive_calls = internal global i32 0
@managed_test.release_constructs = internal global i1 false
@managed_test.allocate_constructs = internal global i1 false
@managed_test.layout = private constant %chibi.managed.layout { ptr @managed_test.trace, ptr null, ptr @managed_test.code_live }
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
  %requested = load i1, ptr @managed_test.allocate_collects
  br i1 %requested, label %collect, label %allocate
collect:
  %heap = load ptr, ptr @managed_test.callback_heap
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  br label %allocate
allocate:
  %construct = load i1, ptr @managed_test.allocate_constructs
  br i1 %construct, label %new.cell, label %reserve
new.cell:
  store i1 false, ptr @managed_test.allocate_constructs
  %owner = load ptr, ptr @managed_test.callback_heap
  %nested = call ptr @chibi_managed.new(ptr %ctx, ptr %owner, i64 0, ptr null, ptr null, i32 0)
  br label %reserve
reserve:
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
  %requested = load i1, ptr @managed_test.release_collects
  br i1 %requested, label %collect, label %release
collect:
  %heap = load ptr, ptr @managed_test.callback_heap
  %before = load i32, ptr @managed_test.recursive_calls
  %next.call = add i32 %before, 1
  store i32 %next.call, ptr @managed_test.recursive_calls
  call void @chibi_managed.collect(ptr %ctx, ptr %heap)
  br label %release
release:
  %construct = load i1, ptr @managed_test.release_constructs
  br i1 %construct, label %new.cell, label %give
new.cell:
  %owner = load ptr, ptr @managed_test.callback_heap
  %invalid = call ptr @chibi_managed.new(ptr %ctx, ptr %owner, i64 0, ptr null, ptr null, i32 0)
  call void @managed_test.check(i1 false, i32 308)
  br label %give
give:
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
  %code = load atomic i64, ptr @managed_test.code_live acquire, align 8
  %mapped = icmp ugt i64 %code, 0
  call void @managed_test.check(i1 %mapped, i32 18)
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
                                    ptr @managed_test.layout, ptr @managed_test.drop, i32 0)
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
  %code.rooted = load atomic i64, ptr @managed_test.code_live acquire, align 8
  %code.pinned = icmp eq i64 %code.rooted, 2
  call void @managed_test.check(i1 %code.pinned, i32 16)
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
  %code.final = load atomic i64, ptr @managed_test.code_live acquire, align 8
  %code.released = icmp eq i64 %code.final, 0
  call void @managed_test.check(i1 %code.released, i32 17)
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

; Callback requests do not collect transient values, and a below-threshold
; safe point must service the request once callback execution has returned.
define internal i32 @managed_test.callback_defers() {
entry:
  %heap = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  %bytes = ptrtoint ptr getelementptr (%chibi.managed.heap, ptr null, i32 1) to i64
  %budget = icmp eq i64 %bytes, 64
  call void @managed_test.check(i1 %budget, i32 300)
  %depth.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 7
  %collecting.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 8
  %requested.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 9
  %depth.offset = ptrtoint ptr getelementptr (%chibi.managed.heap, ptr null, i32 0, i32 7) to i64
  %collecting.offset = ptrtoint ptr getelementptr (%chibi.managed.heap, ptr null, i32 0, i32 8) to i64
  %requested.offset = ptrtoint ptr getelementptr (%chibi.managed.heap, ptr null, i32 0, i32 9) to i64
  %depth.layout = icmp eq i64 %depth.offset, 56
  %collecting.layout = icmp eq i64 %collecting.offset, 60
  %requested.layout = icmp eq i64 %requested.offset, 61
  %flags.layout = and i1 %collecting.layout, %requested.layout
  %layout = and i1 %depth.layout, %flags.layout
  call void @managed_test.check(i1 %layout, i32 301)
  store ptr %heap, ptr @managed_test.callback_heap
  store i1 true, ptr @managed_test.allocate_collects
  %cell = call ptr @chibi_managed.new(ptr @managed_test.context, ptr %heap, i64 0, ptr null, ptr null, i32 0)
  store i1 false, ptr @managed_test.allocate_collects
  call void @managed_test.count(i64 1, i32 302)
  %requested = load i1, ptr %requested.at
  %depth = load i32, ptr %depth.at
  %outside = icmp eq i32 %depth, 0
  %deferred = and i1 %requested, %outside
  call void @managed_test.check(i1 %deferred, i32 303)
  call void @chibi_managed.callback_enter(ptr %heap)
  call void @chibi_managed.callback_enter(ptr %heap)
  call void @chibi_managed.safepoint(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 1, i32 304)
  call void @chibi_managed.callback_exit(ptr %heap)
  call void @chibi_managed.callback_exit(ptr %heap)
  store i1 true, ptr @managed_test.release_collects
  call void @chibi_managed.safepoint(ptr @managed_test.context, ptr %heap)
  store i1 false, ptr @managed_test.release_collects
  call void @managed_test.count(i64 0, i32 305)
  %called = load i32, ptr @managed_test.recursive_calls
  %once = icmp eq i32 %called, 1
  call void @managed_test.check(i1 %once, i32 306)
  %pending = load i1, ptr %requested.at
  %collecting = load i1, ptr %collecting.at
  %busy = or i1 %pending, %collecting
  %idle = xor i1 %busy, true
  call void @managed_test.check(i1 %idle, i32 307)
  store ptr null, ptr @managed_test.callback_heap
  ret i32 0
}
define internal i32 @managed_test.callback_underflow() {
  %heap = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  call void @chibi_managed.callback_exit(ptr %heap)
  ret i32 0
}
define internal i32 @managed_test.callback_overflow() {
  %heap = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  %at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 7
  store i32 -1, ptr %at
  call void @chibi_managed.callback_enter(ptr %heap)
  ret i32 0
}

define internal i32 @managed_test.callback_construct_sweep() {
  %heap = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  store ptr %heap, ptr @managed_test.callback_heap
  %cell = call ptr @chibi_managed.new(ptr @managed_test.context, ptr %heap, i64 0, ptr null, ptr null, i32 0)
  store i1 true, ptr @managed_test.release_constructs
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  ret i32 0
}

define internal i32 @managed_test.callback_nested_constructor() {
  %heap = alloca %chibi.managed.heap
  store %chibi.managed.heap zeroinitializer, ptr %heap
  store ptr %heap, ptr @managed_test.callback_heap
  store i1 true, ptr @managed_test.allocate_constructs
  %cell = call ptr @chibi_managed.new(ptr @managed_test.context, ptr %heap, i64 0, ptr null, ptr null, i32 0)
  call void @managed_test.count(i64 2, i32 309)
  %used.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 3
  %used = load i64, ptr %used.at
  %sized = icmp eq i64 %used, 128
  call void @managed_test.check(i1 %sized, i32 310)
  call void @chibi_managed.collect(ptr @managed_test.context, ptr %heap)
  call void @managed_test.count(i64 0, i32 311)
  %after = load i64, ptr %used.at
  %empty = icmp eq i64 %after, 0
  call void @managed_test.check(i1 %empty, i32 312)
  store ptr null, ptr @managed_test.callback_heap
  ret i32 0
}
