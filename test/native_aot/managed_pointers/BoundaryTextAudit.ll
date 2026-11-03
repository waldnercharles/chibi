; Every nonempty descriptor must own its bytes before the host may unload an
; image. This checker allocates nothing and never changes a descriptor.
@boundary.literal = private constant [14 x i8] c"writer\00literal"
@boundary.counted = private constant [3 x i8] c"A\00Z"
define internal i32 @boundary.text(ptr %address) {
entry:
  %value = load { ptr, i32, ptr }, ptr %address
  %length = extractvalue { ptr, i32, ptr } %value, 1
  %empty = icmp eq i32 %length, 0
  br i1 %empty, label %done, label %ownership
ownership:
  %owner = extractvalue { ptr, i32, ptr } %value, 2
  %owned = icmp ne ptr %owner, null
  br i1 %owned, label %content, label %borrowed
borrowed:
  ret i32 -33
content:
  %literal = icmp eq i32 %length, 14
  %counted = icmp eq i32 %length, 3
  %known = or i1 %literal, %counted
  br i1 %known, label %start, label %bad
start:
  %expected = select i1 %literal, ptr @boundary.literal, ptr @boundary.counted
  %data = extractvalue { ptr, i32, ptr } %value, 0
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
