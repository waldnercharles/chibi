@boundary.root = internal global %chibi.managed.root zeroinitializer
@boundary.value = internal global $P zeroinitializer

define internal ptr @boundary.heap() {
entry:
  %heap.at = getelementptr $CTX, ptr @chibi_context, i32 0, i32 $HEAP
  %heap = load ptr, ptr %heap.at
  ret ptr %heap
}
define internal ptr @boundary.address() {
entry:
  %heap = call ptr @boundary.heap()
  %value = load $P, ptr @boundary.value
  %address = call ptr @chibi_managed.address(ptr %heap, $P %value, i64 $TAG, i64 ptrtoint (ptr getelementptr ($A, ptr null, i32 1) to i64), i32 $STORAGE)
  ret ptr %address
}
define i32 @boundary_create(ptr %incoming, i32 %empty) {
entry:
  %fresh = icmp ne i32 %empty, 0
  br i1 %fresh, label %allocate, label %received
allocate:
  %allocated = call $A @chibi_array.$ARRAY.allocate(ptr @chibi_context, i32 $COUNT, i32 $COUNT)
  %data = extractvalue $A %allocated, 2
  br label %zero
zero:
  %index = phi i32 [ 0, %allocate ], [ %next, %store ]
  %done = icmp eq i32 %index, $COUNT
  br i1 %done, label %ready, label %store
store:
  %at = getelementptr $ROW, ptr %data, i32 %index
  store $ROW zeroinitializer, ptr %at
  %next = add i32 %index, 1
  br label %zero
ready:
  br label %construct
received:
  %given = load $A, ptr %incoming
  store $A zeroinitializer, ptr %incoming
  br label %construct
construct:
  %rows = phi $A [ %allocated, %ready ], [ %given, %received ]
  %value = call $P $CTOR(ptr @chibi_context, $A %rows)
  store $P %value, ptr @boundary.value
  %heap = call ptr @boundary.heap()
  call void @chibi_managed.push(ptr %heap, ptr @boundary.root, ptr @boundary.value, ptr @chibi_managed.trace.pointer)
  ; Constructor borrows its array parameter and acquires the cell's own edge.
  call void @chibi_array.$ARRAY.drop(ptr @chibi_context, $A %rows)
  ret i32 0
}
define i32 @boundary_pointer(ptr %out) {
entry:
  %value = load $P, ptr @boundary.value
  store $P %value, ptr %out
  ret i32 0
}
define i32 @boundary_check(i32 %expected.length) {
entry:
  %address = call ptr @boundary.address()
  %rows = load $A, ptr %address
  %length = extractvalue $A %rows, 0
  %valid = icmp eq i32 %length, %expected.length
  br i1 %valid, label %start, label %bad
start:
  %data = extractvalue $A %rows, 2
  br label %head
head:
  %index = phi i32 [ 0, %start ], [ %next, %good ]
  %total = phi i32 [ 0, %start ], [ %sum, %good ]
  %done = icmp eq i32 %index, %length
  br i1 %done, label %exit, label %row
row:
  %at = getelementptr $ROW, ptr %data, i32 %index
  %checked = call i32 @boundary.row(ptr %at)
  %failed = icmp slt i32 %checked, 0
  br i1 %failed, label %failure, label %good
good:
  %sum = add i32 %total, %checked
  %next = add i32 %index, 1
  br label %head
failure:
  ret i32 %checked
bad:
  ret i32 -34
exit:
  ret i32 %total
}
define i32 @boundary_release() {
entry:
  %heap = call ptr @boundary.heap()
  call void @chibi_managed.pop(ptr %heap, ptr @boundary.root)
  store $P zeroinitializer, ptr @boundary.value
  ret i32 0
}
define ptr @boundary_counted(ptr %incoming, i32 %published) {
entry:
  %stored = icmp ne i32 %published, 0
  br i1 %stored, label %resolve, label %given
resolve:
  %address = call ptr @boundary.address()
  br label %read
given:
  br label %read
read:
  %source = phi ptr [ %address, %resolve ], [ %incoming, %given ]
  %rows = load $A, ptr %source
  %data = extractvalue $A %rows, 2
$COUNTED
  %text = load { ptr, i32, ptr }, ptr %text.at
  %owner = extractvalue { ptr, i32, ptr } %text, 2
  ret ptr %owner
}
