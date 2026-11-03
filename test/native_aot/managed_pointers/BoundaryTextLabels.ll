; These inspections allocate nothing and run only while the old root is live.
define i32 @boundary_labels_count() {
entry:
  %address = call ptr @boundary.address()
  %rows = load $A, ptr %address
  %data = extractvalue $A %rows, 2
  %labels.at = getelementptr $ROW, ptr %data, i32 0, i32 0
  %labels = load $A, ptr %labels.at
  %length = extractvalue $A %labels, 0
  ret i32 %length
}
define internal ptr @boundary.labels.owner(ptr %data, i32 %index) {
entry:
  %row = getelementptr $ROW, ptr %data, i32 %index
  %labels.at = getelementptr $ROW, ptr %row, i32 0, i32 0
  %labels = load $A, ptr %labels.at
  %owner = extractvalue $A %labels, 3
  ret ptr %owner
}
define i64 @boundary_first_backing() {
entry:
  %address = call ptr @boundary.address()
  %rows = load $A, ptr %address
  %data = extractvalue $A %rows, 2
  %owner = call ptr @boundary.labels.owner(ptr %data, i32 0)
  %identity = ptrtoint ptr %owner to i64
  ret i64 %identity
}
define i32 @boundary_aliases() {
entry:
  %address = call ptr @boundary.address()
  %rows = load $A, ptr %address
  %length = extractvalue $A %rows, 0
  %wide = icmp eq i32 %length, 128
  br i1 %wide, label %start, label %bad
start:
  %data = extractvalue $A %rows, 2
  br label %head
head:
  %index = phi i32 [ 0, %start ], [ %next, %same ]
  %done = icmp eq i32 %index, 64
  br i1 %done, label %good, label %pair
pair:
  %reverse = sub i32 127, %index
  %left = call ptr @boundary.labels.owner(ptr %data, i32 %index)
  %right = call ptr @boundary.labels.owner(ptr %data, i32 %reverse)
  %owned = icmp ne ptr %left, null
  %shared = icmp eq ptr %left, %right
  %valid = and i1 %owned, %shared
  br i1 %valid, label %same, label %bad
same:
  %next = add i32 %index, 1
  br label %head
bad:
  ret i32 1
good:
  ret i32 0
}
