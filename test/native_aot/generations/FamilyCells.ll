; Trusted backend fixture. Transporting a cell as an opaque retained word tests
; the host's image lease without admitting collector pointers to source retain.
@family.old = private constant i1 @OLD@
@family.pointer = private global { ptr, i64, i64 } zeroinitializer
@family.root = private global %chibi.managed.persistent zeroinitializer
@family.layout = private constant %chibi.managed.layout { ptr null, ptr null, ptr @chibi_generation_cells }
@family.ready = private constant [18 x i8] c"family-cell-ready\00"
@family.dropped = private constant [20 x i8] c"family-cell-dropped\00"
@family.mode = private constant [3 x i8] c"wb\00"
declare ptr @fopen(ptr, ptr)
declare i32 @fclose(ptr)
declare i32 @access(ptr, i32)

define private void @family.mark(ptr %path) {
  %stream = call ptr @fopen(ptr %path, ptr @family.mode)
  %opened = icmp ne ptr %stream, null
  br i1 %opened, label %close, label %trap
close:
  %closed = call i32 @fclose(ptr %stream)
  %ok = icmp eq i32 %closed, 0
  br i1 %ok, label %exit, label %trap
exit:
  ret void
trap:
  call void @llvm.trap()
  unreachable
}
define private void @family.drop(ptr %ctx, ptr %payload) {
  call void @family.mark(ptr @family.dropped)
  ret void
}
define i32 @chibi_host_call_Frame() {
entry:
  %heap = call ptr @chibi_managed.owner(ptr @chibi_context)
  %old = load i1, ptr @family.old
  %cell = load ptr, ptr @family.pointer
  %empty = icmp eq ptr %cell, null
  br i1 %old, label %previous, label %incoming
previous:
  br i1 %empty, label %create, label %wait
create:
  %fresh = call ptr @chibi_managed.new(ptr @chibi_context, ptr %heap, i64 4, ptr @family.layout, ptr @family.drop, i32 1)
  %payload = getelementptr %chibi.managed.cell, ptr %fresh, i32 1
  store i32 42, ptr %payload
  store { ptr, i64, i64 } { ptr null, i64 0, i64 123 }, ptr @family.pointer
  store ptr %fresh, ptr @family.pointer
  call void @chibi_managed.hold(ptr %heap, ptr @family.root, ptr @family.pointer, ptr @chibi_managed.trace.pointer)
  call void @family.mark(ptr @family.ready)
  br label %wait
wait:
  ret i32 0
incoming:
  br i1 %empty, label %trap, label %check
check:
  %layout.at = getelementptr %chibi.managed.cell, ptr %cell, i32 0, i32 2
  %layout = load ptr, ptr %layout.at
  %counter.at = getelementptr %chibi.managed.layout, ptr %layout, i32 0, i32 2
  %counter = load ptr, ptr %counter.at
  %before = load atomic i64, ptr %counter acquire, align 8
  %pinned = icmp eq i64 %before, 1
  %value.at = getelementptr %chibi.managed.cell, ptr %cell, i32 1
  %value = load i32, ptr %value.at
  %same = icmp eq i32 %value, 42
  %valid = and i1 %pinned, %same
  br i1 %valid, label %drop, label %trap
drop:
  call void @chibi_managed.unhold(ptr %heap, ptr @family.root)
  store { ptr, i64, i64 } zeroinitializer, ptr @family.pointer
  call void @chibi_managed.collect(ptr @chibi_context, ptr %heap)
  %after = load atomic i64, ptr %counter acquire, align 8
  %released = icmp eq i64 %after, 0
  %marker = call i32 @access(ptr @family.dropped, i32 0)
  %dropped = icmp eq i32 %marker, 0
  %used.at = getelementptr %chibi.managed.heap, ptr %heap, i32 0, i32 3
  %used = load i64, ptr %used.at
  %drained = icmp eq i64 %used, 0
  %done = and i1 %released, %dropped
  %balanced = and i1 %done, %drained
  br i1 %balanced, label %exit, label %trap
exit:
  ret i32 7
trap:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_host_retained_count() { ret i32 1 }
define i32 @chibi_host_retained_type(i32 %index) { ret i32 4 }
define i64 @chibi_host_retained_hash(i32 %index) { ret i64 42 }
define i32 @chibi_host_retained_ready(i32 %index) { ret i32 1 }
define i64 @chibi_host_retained_get(i32 %index) {
  %cell = load ptr, ptr @family.pointer
  %word = ptrtoint ptr %cell to i64
  ret i64 %word
}
define i32 @chibi_host_retained_set(i32 %index, i64 %word) {
  %heap = call ptr @chibi_managed.owner(ptr @chibi_context)
  %cell = inttoptr i64 %word to ptr
  store { ptr, i64, i64 } { ptr null, i64 0, i64 123 }, ptr @family.pointer
  store ptr %cell, ptr @family.pointer
  call void @chibi_managed.hold(ptr %heap, ptr @family.root, ptr @family.pointer, ptr @chibi_managed.trace.pointer)
  ret i32 1
}
define i32 @chibi_host_retained_set_as(i32 %index, i32 %from, i64 %word) { ret i32 0 }
define i32 @chibi_host_retained_release() {
  %heap = call ptr @chibi_managed.owner(ptr @chibi_context)
  call void @chibi_managed.unhold(ptr %heap, ptr @family.root)
  store { ptr, i64, i64 } zeroinitializer, ptr @family.pointer
  ret i32 0
}
