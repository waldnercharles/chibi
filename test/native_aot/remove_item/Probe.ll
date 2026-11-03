attributes #0 = { nounwind }
declare void @llvm.trap()
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
define internal void @chibi_scalar.copy(ptr %to, ptr %from, i64 %count) {
  call void @llvm.memcpy.p0.p0.i64(ptr %to, ptr %from, i64 %count, i1 false)
  ret void
}
define internal void @chibi_scalar.zero(ptr %to, i64 %count) {
  %previous = load i64, ptr @zeroed
  %next = add i64 %previous, %count
  store i64 %next, ptr @zeroed
  call void @llvm.memset.p0.i64(ptr %to, i8 0, i64 %count, i1 false)
  ret void
}

declare ptr @malloc(i64)
declare void @free(ptr)
@live = internal global i64 0
@allocations = internal global i64 0
@peak = internal global i64 0
@zeroed = internal global i64 0
@bytes.a = private constant [3 x i8] c"A\00B"
@bytes.b = private constant [3 x i8] c"A\00B"
@numbers.a = private constant [2 x float] [float 1.0, float 2.0]
@numbers.b = private constant [2 x float] [float 1.0, float 2.0]
@numbers.bad = private constant [2 x float] [float 1.0, float 3.0]
@numbers.nan = private constant [1 x float] [float 0x7FF8000000000000]
@numbers.nan2 = private constant [1 x float] [float 0x7FF8000000000000]
@box.a = private constant %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.a, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }
@box.b = private constant %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.b, ptr null }, { ptr, i32, ptr } { ptr @bytes.b, i32 3, ptr null } }
@box.bad = private constant %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.bad, ptr null }, { ptr, i32, ptr } { ptr @bytes.b, i32 3, ptr null } }
@box.nan = private constant %record0 { { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.nan, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }
@box.nan2 = private constant %record0 { { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.nan2, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }
@empty.a = private constant %record1 { i32 0, float 0x7FF8000000000000, { i32, i32, ptr, ptr } { i32 99, i32 99, ptr null, ptr null } }
@empty.b = private constant %record1 zeroinitializer
@value.nan = private constant %record1 { i32 1, float 0x7FF8000000000000, { i32, i32, ptr, ptr } zeroinitializer }
@union.a = private constant %record1 { i32 2, float 0x7FF8000000000000, { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @box.a, ptr null } }
@union.b = private constant %record1 { i32 2, float 7.0, { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @box.b, ptr null } }

@prefix.left = private constant [2 x %record0] [ %record0 { { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.a, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.a, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } } ]
@prefix.array.left = private constant { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @prefix.left, ptr null }
@prefix.bad = private constant [2 x %record0] [ %record0 { { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.bad, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.bad, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } } ]
@prefix.array.bad = private constant { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @prefix.bad, ptr null }
@prefix.equal = private constant [2 x %record0] [ %record0 { { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.b, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.b, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } } ]
@prefix.array.equal = private constant { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @prefix.equal, ptr null }

define internal ptr @allocate(ptr %ctx, i64 %bytes) {
entry:
  %size = add i64 %bytes, 8
  %raw = call ptr @malloc(i64 %size)
  %valid = icmp ne ptr %raw, null
  br i1 %valid, label %ready, label %oom
ready:
  store i64 %bytes, ptr %raw
  %live = load i64, ptr @live
  %next = add i64 %live, %bytes
  store i64 %next, ptr @live
  %peak = load i64, ptr @peak
  %higher = icmp ugt i64 %next, %peak
  %new.peak = select i1 %higher, i64 %next, i64 %peak
  store i64 %new.peak, ptr @peak
  %calls = load i64, ptr @allocations
  %called = add i64 %calls, 1
  store i64 %called, ptr @allocations
  %data = getelementptr i8, ptr %raw, i64 8
  ret ptr %data
oom:
  call void @llvm.trap()
  unreachable
}
define internal void @release(ptr %ctx, ptr %data) {
entry:
  %raw = getelementptr i8, ptr %data, i64 -8
  %bytes = load i64, ptr %raw
  %live = load i64, ptr @live
  %next = sub i64 %live, %bytes
  store i64 %next, ptr @live
  call void @llvm.memset.p0.i64(ptr %data, i8 -91, i64 %bytes, i1 false)
  call void @free(ptr %raw)
  ret void
}
@context = internal global { ptr, ptr, ptr, ptr, ptr } { ptr null, ptr null, ptr @allocate, ptr null, ptr @release }

define i32 @equality_probe(ptr %memory) {
entry:
  call void @free(ptr %memory)
  %session = alloca %chibi.value.equal
  store %chibi.value.equal zeroinitializer, ptr %session
  %separate = call i1 @chibi_value_equal.3(ptr @context, ptr %session, ptr @box.a, ptr @box.b)
  %different = call i1 @chibi_value_equal.3(ptr @context, ptr %session, ptr @box.a, ptr @box.bad)
  %nan = call i1 @chibi_value_equal.3(ptr @context, ptr %session, ptr @box.nan, ptr @box.nan)
  %nan.again = call i1 @chibi_value_equal.3(ptr @context, ptr %session, ptr @box.nan, ptr @box.nan)
  %distinct.nan = call i1 @chibi_value_equal.3(ptr @context, ptr %session, ptr @box.nan, ptr @box.nan2)
  %empty = call i1 @chibi_value_equal.5(ptr @context, ptr %session, ptr @empty.a, ptr @empty.b)
  %tagged.nan = call i1 @chibi_value_equal.5(ptr @context, ptr %session, ptr @value.nan, ptr @value.nan)
  %union = call i1 @chibi_value_equal.5(ptr @context, ptr %session, ptr @union.a, ptr @union.b)
  %prefix.bad = call i1 @chibi_value_equal.4(ptr @context, ptr %session, ptr @prefix.array.left, ptr @prefix.array.bad)
  %prefix.equal = call i1 @chibi_value_equal.4(ptr @context, ptr %session, ptr @prefix.array.left, ptr @prefix.array.equal)
  call void @chibi_value_equal.release(ptr @context, ptr %session)
  %live = load i64, ptr @live
  %balanced = icmp eq i64 %live, 0
  %calls = load i64, ptr @allocations
  %reused = icmp eq i64 %calls, 2
  %true.a = and i1 %separate, %empty
  %true.b = and i1 %union, %balanced
  %true.c = and i1 %true.a, %true.b
  %true.d = and i1 %true.c, %reused
  %good = and i1 %true.d, %prefix.equal
  %false.a = or i1 %different, %nan
  %false.b = or i1 %nan.again, %distinct.nan
  %false.c = or i1 %false.a, %false.b
  %false.d = or i1 %false.c, %tagged.nan
  %bad = or i1 %false.d, %prefix.bad
  %not.good = xor i1 %good, true
  %failure = or i1 %not.good, %bad
  %status = zext i1 %failure to i32
  ret i32 %status
}

; Recursive array-backed unions require bounded runtime stack use. The two
; chains are separate allocations, so descriptor identity cannot prove equality.
define i64 @equality_growth(i32 %count) {
entry:
  store i64 0, ptr @peak
  store i64 0, ptr @zeroed
  %end = getelementptr %record2, ptr null, i32 %count
  %bytes = ptrtoint ptr %end to i64
  %left = call ptr @malloc(i64 %bytes)
  %right = call ptr @malloc(i64 %bytes)
  %session = alloca %chibi.value.equal
  store %chibi.value.equal zeroinitializer, ptr %session
  br label %head
head:
  %index = phi i32 [ 0, %entry ], [ %next, %step ]
  %finished = icmp eq i32 %index, %count
  br i1 %finished, label %compare, label %body
body:
  %left.at = getelementptr %record2, ptr %left, i32 %index
  %right.at = getelementptr %record2, ptr %right, i32 %index
  store %record2 zeroinitializer, ptr %left.at
  store %record2 zeroinitializer, ptr %right.at
  %next = add i32 %index, 1
  %last = icmp eq i32 %next, %count
  br i1 %last, label %leaf, label %link
leaf:
  %left.value = getelementptr %record2, ptr %left.at, i32 0, i32 1
  %right.value = getelementptr %record2, ptr %right.at, i32 0, i32 1
  store i32 7, ptr %left.value
  store i32 7, ptr %right.value
  br label %step
link:
  store i32 1, ptr %left.at
  store i32 1, ptr %right.at
  %left.next = getelementptr %record2, ptr %left, i32 %next
  %right.next = getelementptr %record2, ptr %right, i32 %next
  %left.child = getelementptr %record2, ptr %left.at, i32 0, i32 2
  %right.child = getelementptr %record2, ptr %right.at, i32 0, i32 2
  %left.array = insertvalue { i32, i32, ptr, ptr } { i32 1, i32 1, ptr null, ptr null }, ptr %left.next, 2
  %right.array = insertvalue { i32, i32, ptr, ptr } { i32 1, i32 1, ptr null, ptr null }, ptr %right.next, 2
  store { i32, i32, ptr, ptr } %left.array, ptr %left.child
  store { i32, i32, ptr, ptr } %right.array, ptr %right.child
  br label %step
step:
  br label %head
compare:
  %same = call i1 @chibi_value_equal.9(ptr @context, ptr %session, ptr %left, ptr %right)
  %last.index = sub i32 %count, 1
  %changed = getelementptr %record2, ptr %right, i32 %last.index, i32 1
  store i32 8, ptr %changed
  %different = call i1 @chibi_value_equal.9(ptr @context, ptr %session, ptr %left, ptr %right)
  store i32 7, ptr %changed
  %repaired = call i1 @chibi_value_equal.9(ptr @context, ptr %session, ptr %left, ptr %right)
  call void @chibi_value_equal.release(ptr @context, ptr %session)
  call void @free(ptr %left)
  call void @free(ptr %right)
  %live = load i64, ptr @live
  %balanced = icmp eq i64 %live, 0
  %fixed = and i1 %same, %repaired
  %good = and i1 %fixed, %balanced
  %not.different = xor i1 %different, true
  %valid = and i1 %good, %not.different
  %peak = load i64, ptr @peak
  %limit = mul i64 %peak, 2
  %zeroed = load i64, ptr @zeroed
  %bounded.reset = icmp ule i64 %zeroed, %limit
  %accepted = and i1 %valid, %bounded.reset
  %result = select i1 %accepted, i64 %peak, i64 -1
  ret i64 %result
}
