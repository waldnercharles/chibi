; The constructor and all managed layouts come from the actual old source.
@owned_record.root = internal global %chibi.managed.root zeroinitializer
@owned_record.value = internal global $P zeroinitializer
@owned_record.first = private constant [17 x i8] c"new image literal"
@owned_record.second = private constant [20 x i8] c"second image literal"

define internal ptr @owned_record.heap() {
  %heap.at = getelementptr $CTX, ptr @chibi_context, i32 0, i32 $HEAP
  %heap = load ptr, ptr %heap.at
  ret ptr %heap
}
define i32 @owned_record_create(ptr %out) {
  %value = call $P $CTOR(ptr @chibi_context)
  store $P %value, ptr @owned_record.value
  store $P %value, ptr %out
  %heap = call ptr @owned_record.heap()
  call void @chibi_managed.push(ptr %heap, ptr @owned_record.root, ptr @owned_record.value, ptr @chibi_managed.trace.pointer)
  ret i32 0
}
; Inspect every byte only after checking the complete descriptor's owner.
; Empty Text requires no allocation, but its expected length remains checked.
define internal i32 @owned_record.text(ptr %at, ptr %expected, i32 %wanted) {
entry:
  %text = load { ptr, i32, ptr }, ptr %at
  %length = extractvalue { ptr, i32, ptr } %text, 1
  %valid.length = icmp eq i32 %length, %wanted
  br i1 %valid.length, label %sized, label %bad
sized:
  %empty = icmp eq i32 %length, 0
  br i1 %empty, label %done, label %ownership
ownership:
  %owner = extractvalue { ptr, i32, ptr } %text, 2
  %owned = icmp ne ptr %owner, null
  br i1 %owned, label %start, label %borrowed
borrowed:
  ret i32 -33
start:
  %data = extractvalue { ptr, i32, ptr } %text, 0
  br label %head
head:
  %index = phi i32 [ 0, %start ], [ %next, %equal ]
  %finished = icmp eq i32 %index, %length
  br i1 %finished, label %done, label %byte
byte:
  %actual.at = getelementptr i8, ptr %data, i32 %index
  %expected.at = getelementptr i8, ptr %expected, i32 %index
  %actual.byte = load i8, ptr %actual.at
  %expected.byte = load i8, ptr %expected.at
  %same = icmp eq i8 %actual.byte, %expected.byte
  br i1 %same, label %equal, label %bad
equal:
  %next = add i32 %index, 1
  br label %head
bad:
  ret i32 -34
done:
  ret i32 %length
}
define i32 @owned_record_check(i32 %wanted.number) {
  %heap = call ptr @owned_record.heap()
  %value = load $P, ptr @owned_record.value
  %address = call ptr @chibi_managed.address(ptr %heap, $P %value, i64 $TAG, i64 ptrtoint (ptr getelementptr ($ROW, ptr null, i32 1) to i64), i32 $STORAGE)
  %pointer.at = getelementptr $ROW, ptr %address, i32 0, i32 0
  %pointer = load $P, ptr %pointer.at
  %pointer.tag = extractvalue $P %pointer, 2
  %number.at = call ptr @chibi_managed.address(ptr %heap, $P %pointer, i64 %pointer.tag, i64 ptrtoint (ptr getelementptr ($NUMBER, ptr null, i32 1) to i64), i32 $STORAGE)
  %number = load i32, ptr %number.at
  %number.valid = icmp eq i32 %number, %wanted.number
  %first.at = getelementptr $ROW, ptr %address, i32 0, i32 1
  %second.at = getelementptr $ROW, ptr %address, i32 0, i32 2
  %empty.at = getelementptr $ROW, ptr %address, i32 0, i32 3
  %first = call i32 @owned_record.text(ptr %first.at, ptr @owned_record.first, i32 17)
  %second = call i32 @owned_record.text(ptr %second.at, ptr @owned_record.second, i32 20)
  %empty = call i32 @owned_record.text(ptr %empty.at, ptr null, i32 0)
  %first.borrowed = icmp eq i32 %first, -33
  %second.borrowed = icmp eq i32 %second, -33
  %borrowed = or i1 %first.borrowed, %second.borrowed
  %first.valid = icmp eq i32 %first, 17
  %second.valid = icmp eq i32 %second, 20
  %empty.valid = icmp eq i32 %empty, 0
  %texts.valid = and i1 %first.valid, %second.valid
  %values.valid = and i1 %empty.valid, %number.valid
  %valid = and i1 %texts.valid, %values.valid
  %checked = select i1 %valid, i32 37, i32 -34
  %result = select i1 %borrowed, i32 -33, i32 %checked
  ret i32 %result
}
define i32 @owned_record_release() {
  %heap = call ptr @owned_record.heap()
  call void @chibi_managed.pop(ptr %heap, ptr @owned_record.root)
  store $P zeroinitializer, ptr @owned_record.value
  ret i32 0
}
