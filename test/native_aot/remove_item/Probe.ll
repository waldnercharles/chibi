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
  %maps = call i1 @equality_maps(ptr @context, ptr %session)
  call void @chibi_value_equal.release(ptr @context, ptr %session)
  %live = load i64, ptr @live
  %balanced = icmp eq i64 %live, 0
  %calls = load i64, ptr @allocations
  %reused = icmp eq i64 %calls, 2
  %true.a = and i1 %separate, %empty
  %true.b = and i1 %union, %balanced
  %true.c = and i1 %true.a, %true.b
  %true.d = and i1 %true.c, %reused
  %old.good = and i1 %true.d, %prefix.equal
  %good = and i1 %old.good, %maps
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

; Map comparisons borrow entry payloads, preserve IEEE rules and use the same
; indexed lookup as ordinary reads. None of these descriptors owns the data.
@map.a.entries = private constant [2 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 1.0, i32 269157861 }, { i32, float, i32 } { i32 2, float 2.0, i32 975901834 } ]
@map.a = private constant { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.a.entries, ptr null }
@map.b.entries = private constant [2 x { i32, float, i32 }] [ { i32, float, i32 } { i32 2, float 2.0, i32 975901834 }, { i32, float, i32 } { i32 1, float 1.0, i32 269157861 } ]
@map.b = private constant { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.b.entries, ptr null }
@map.bad.entries = private constant [2 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 1.0, i32 269157861 }, { i32, float, i32 } { i32 2, float 3.0, i32 975901834 } ]
@map.bad = private constant { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.bad.entries, ptr null }
@map.absent.entries = private constant [2 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 1.0, i32 269157861 }, { i32, float, i32 } { i32 3, float 2.0, i32 3809749232 } ]
@map.absent = private constant { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.absent.entries, ptr null }
@map.dead.entries = private constant [3 x { i32, float, i32 }] [ { i32, float, i32 } { i32 99, float 99.0, i32 0 }, { i32, float, i32 } { i32 2, float 2.0, i32 975901834 }, { i32, float, i32 } { i32 1, float 1.0, i32 269157861 } ]
@map.dead = private constant { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 3, i32 3, i32 0, ptr @map.dead.entries, ptr null }
@map.nan.entries = private constant [1 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 0x7FF8000000000000, i32 269157861 } ]
@map.nan = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.nan.entries, ptr null }
@map.nan2.entries = private constant [1 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 0x7FF8000000000000, i32 269157861 } ]
@map.nan2 = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.nan2.entries, ptr null }
@map.negative.entries = private constant [1 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float -0.0, i32 269157861 } ]
@map.negative = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.negative.entries, ptr null }
@map.positive.entries = private constant [1 x { i32, float, i32 }] [ { i32, float, i32 } { i32 1, float 0.0, i32 269157861 } ]
@map.positive = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.positive.entries, ptr null }
@map.large.entries = private constant [64 x { i32, float, i32 }] [ { i32, float, i32 } { i32 0, float 0.0, i32 1 }, { i32, float, i32 } { i32 1, float 1.0, i32 269157861 }, { i32, float, i32 } { i32 2, float 2.0, i32 975901834 }, { i32, float, i32 } { i32 3, float 3.0, i32 3809749232 }, { i32, float, i32 } { i32 4, float 4.0, i32 1951803668 }, { i32, float, i32 } { i32 5, float 5.0, i32 3199944156 }, { i32, float, i32 } { i32 6, float 6.0, i32 1886925676 }, { i32, float, i32 } { i32 7, float 7.0, i32 2065162004 }, { i32, float, i32 } { i32 8, float 8.0, i32 2632690152 }, { i32, float, i32 } { i32 9, float 9.0, i32 2145213143 }, { i32, float, i32 } { i32 10, float 10.0, i32 2838627705 }, { i32, float, i32 } { i32 11, float 11.0, i32 4114266989 }, { i32, float, i32 } { i32 12, float 12.0, i32 3609279420 }, { i32, float, i32 } { i32 13, float 13.0, i32 3052962257 }, { i32, float, i32 } { i32 14, float 14.0, i32 4130324009 }, { i32, float, i32 } { i32 15, float 15.0, i32 887756089 }, { i32, float, i32 } { i32 16, float 16.0, i32 3433343165 }, { i32, float, i32 } { i32 17, float 17.0, i32 1914458558 }, { i32, float, i32 } { i32 18, float 18.0, i32 4138371185 }, { i32, float, i32 } { i32 19, float 19.0, i32 2554460247 }, { i32, float, i32 } { i32 20, float 20.0, i32 1382288114 }, { i32, float, i32 } { i32 21, float 21.0, i32 1873750857 }, { i32, float, i32 } { i32 22, float 22.0, i32 3020217723 }, { i32, float, i32 } { i32 23, float 23.0, i32 3193519269 }, { i32, float, i32 } { i32 24, float 24.0, i32 3683044024 }, { i32, float, i32 } { i32 25, float 25.0, i32 114556472 }, { i32, float, i32 } { i32 26, float 26.0, i32 4194961115 }, { i32, float, i32 } { i32 27, float 27.0, i32 858394364 }, { i32, float, i32 } { i32 28, float 28.0, i32 863116194 }, { i32, float, i32 } { i32 29, float 29.0, i32 666324818 }, { i32, float, i32 } { i32 30, float 30.0, i32 1775512178 }, { i32, float, i32 } { i32 31, float 31.0, i32 74379621 }, { i32, float, i32 } { i32 32, float 32.0, i32 1233104197 }, { i32, float, i32 } { i32 33, float 33.0, i32 2181589191 }, { i32, float, i32 } { i32 34, float 34.0, i32 2825136188 }, { i32, float, i32 } { i32 35, float 35.0, i32 113255102 }, { i32, float, i32 } { i32 36, float 36.0, i32 3981775074 }, { i32, float, i32 } { i32 37, float 37.0, i32 4017491480 }, { i32, float, i32 } { i32 38, float 38.0, i32 1712690030 }, { i32, float, i32 } { i32 39, float 39.0, i32 1204080003 }, { i32, float, i32 } { i32 40, float 40.0, i32 4038300453 }, { i32, float, i32 } { i32 41, float 41.0, i32 3149438245 }, { i32, float, i32 } { i32 42, float 42.0, i32 3564271138 }, { i32, float, i32 } { i32 43, float 43.0, i32 3348682954 }, { i32, float, i32 } { i32 44, float 44.0, i32 1745468151 }, { i32, float, i32 } { i32 45, float 45.0, i32 849014403 }, { i32, float, i32 } { i32 46, float 46.0, i32 2092071243 }, { i32, float, i32 } { i32 47, float 47.0, i32 2738543644 }, { i32, float, i32 } { i32 48, float 48.0, i32 3100111204 }, { i32, float, i32 } { i32 49, float 49.0, i32 3261253855 }, { i32, float, i32 } { i32 50, float 50.0, i32 3244320560 }, { i32, float, i32 } { i32 51, float 51.0, i32 4265669625 }, { i32, float, i32 } { i32 52, float 52.0, i32 2783218727 }, { i32, float, i32 } { i32 53, float 53.0, i32 2813008852 }, { i32, float, i32 } { i32 54, float 54.0, i32 818264924 }, { i32, float, i32 } { i32 55, float 55.0, i32 1938557595 }, { i32, float, i32 } { i32 56, float 56.0, i32 2837426180 }, { i32, float, i32 } { i32 57, float 57.0, i32 1381736465 }, { i32, float, i32 } { i32 58, float 58.0, i32 3061479800 }, { i32, float, i32 } { i32 59, float 59.0, i32 1823867422 }, { i32, float, i32 } { i32 60, float 60.0, i32 3551024357 }, { i32, float, i32 } { i32 61, float 61.0, i32 1741705023 }, { i32, float, i32 } { i32 62, float 62.0, i32 365830251 }, { i32, float, i32 } { i32 63, float 63.0, i32 21004309 } ]
@map.large.index = private constant [128 x { i32, i32 }] [ { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1, i32 1 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1204080003, i32 40 }, { i32, i32 } { i32 849014403, i32 46 }, { i32, i32 } { i32 2837426180, i32 57 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 975901834, i32 3 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1381736465, i32 58 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1951803668, i32 5 }, { i32, i32 } { i32 2065162004, i32 8 }, { i32, i32 } { i32 21004309, i32 64 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 4017491480, i32 38 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1938557595, i32 56 }, { i32, i32 } { i32 2738543644, i32 48 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1823867422, i32 60 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 863116194, i32 29 }, { i32, i32 } { i32 3564271138, i32 43 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3193519269, i32 24 }, { i32, i32 } { i32 4038300453, i32 41 }, { i32, i32 } { i32 3149438245, i32 42 }, { i32, i32 } { i32 2783218727, i32 53 }, { i32, i32 } { i32 4130324009, i32 15 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3244320560, i32 51 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3683044024, i32 25 }, { i32, i32 } { i32 114556472, i32 26 }, { i32, i32 } { i32 887756089, i32 16 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3609279420, i32 13 }, { i32, i32 } { i32 2825136188, i32 35 }, { i32, i32 } { i32 3433343165, i32 17 }, { i32, i32 } { i32 1914458558, i32 18 }, { i32, i32 } { i32 113255102, i32 36 }, { i32, i32 } { i32 1741705023, i32 62 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1233104197, i32 33 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2181589191, i32 34 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1873750857, i32 22 }, { i32, i32 } { i32 3348682954, i32 44 }, { i32, i32 } { i32 2092071243, i32 47 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3052962257, i32 14 }, { i32, i32 } { i32 666324818, i32 30 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2813008852, i32 54 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2145213143, i32 10 }, { i32, i32 } { i32 2554460247, i32 20 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 4194961115, i32 27 }, { i32, i32 } { i32 3199944156, i32 6 }, { i32, i32 } { i32 818264924, i32 55 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3261253855, i32 50 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3981775074, i32 37 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3100111204, i32 49 }, { i32, i32 } { i32 269157861, i32 2 }, { i32, i32 } { i32 74379621, i32 32 }, { i32, i32 } { i32 3551024357, i32 61 }, { i32, i32 } { i32 2632690152, i32 9 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 365830251, i32 63 }, { i32, i32 } { i32 1886925676, i32 7 }, { i32, i32 } { i32 4114266989, i32 12 }, { i32, i32 } { i32 1712690030, i32 39 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3809749232, i32 4 }, { i32, i32 } { i32 4138371185, i32 19 }, { i32, i32 } { i32 1382288114, i32 21 }, { i32, i32 } { i32 1775512178, i32 31 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1745468151, i32 45 }, { i32, i32 } { i32 3061479800, i32 59 }, { i32, i32 } { i32 2838627705, i32 11 }, { i32, i32 } { i32 4265669625, i32 52 }, { i32, i32 } { i32 3020217723, i32 23 }, { i32, i32 } { i32 858394364, i32 28 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 } ]
@map.large = private constant { i32, i32, i32, i32, ptr, ptr } { i32 64, i32 64, i32 64, i32 128, ptr @map.large.entries, ptr @map.large.index }
@map.large.reverse.entries = private constant [64 x { i32, float, i32 }] [ { i32, float, i32 } { i32 63, float 63.0, i32 21004309 }, { i32, float, i32 } { i32 62, float 62.0, i32 365830251 }, { i32, float, i32 } { i32 61, float 61.0, i32 1741705023 }, { i32, float, i32 } { i32 60, float 60.0, i32 3551024357 }, { i32, float, i32 } { i32 59, float 59.0, i32 1823867422 }, { i32, float, i32 } { i32 58, float 58.0, i32 3061479800 }, { i32, float, i32 } { i32 57, float 57.0, i32 1381736465 }, { i32, float, i32 } { i32 56, float 56.0, i32 2837426180 }, { i32, float, i32 } { i32 55, float 55.0, i32 1938557595 }, { i32, float, i32 } { i32 54, float 54.0, i32 818264924 }, { i32, float, i32 } { i32 53, float 53.0, i32 2813008852 }, { i32, float, i32 } { i32 52, float 52.0, i32 2783218727 }, { i32, float, i32 } { i32 51, float 51.0, i32 4265669625 }, { i32, float, i32 } { i32 50, float 50.0, i32 3244320560 }, { i32, float, i32 } { i32 49, float 49.0, i32 3261253855 }, { i32, float, i32 } { i32 48, float 48.0, i32 3100111204 }, { i32, float, i32 } { i32 47, float 47.0, i32 2738543644 }, { i32, float, i32 } { i32 46, float 46.0, i32 2092071243 }, { i32, float, i32 } { i32 45, float 45.0, i32 849014403 }, { i32, float, i32 } { i32 44, float 44.0, i32 1745468151 }, { i32, float, i32 } { i32 43, float 43.0, i32 3348682954 }, { i32, float, i32 } { i32 42, float 42.0, i32 3564271138 }, { i32, float, i32 } { i32 41, float 41.0, i32 3149438245 }, { i32, float, i32 } { i32 40, float 40.0, i32 4038300453 }, { i32, float, i32 } { i32 39, float 39.0, i32 1204080003 }, { i32, float, i32 } { i32 38, float 38.0, i32 1712690030 }, { i32, float, i32 } { i32 37, float 37.0, i32 4017491480 }, { i32, float, i32 } { i32 36, float 36.0, i32 3981775074 }, { i32, float, i32 } { i32 35, float 35.0, i32 113255102 }, { i32, float, i32 } { i32 34, float 34.0, i32 2825136188 }, { i32, float, i32 } { i32 33, float 33.0, i32 2181589191 }, { i32, float, i32 } { i32 32, float 32.0, i32 1233104197 }, { i32, float, i32 } { i32 31, float 31.0, i32 74379621 }, { i32, float, i32 } { i32 30, float 30.0, i32 1775512178 }, { i32, float, i32 } { i32 29, float 29.0, i32 666324818 }, { i32, float, i32 } { i32 28, float 28.0, i32 863116194 }, { i32, float, i32 } { i32 27, float 27.0, i32 858394364 }, { i32, float, i32 } { i32 26, float 26.0, i32 4194961115 }, { i32, float, i32 } { i32 25, float 25.0, i32 114556472 }, { i32, float, i32 } { i32 24, float 24.0, i32 3683044024 }, { i32, float, i32 } { i32 23, float 23.0, i32 3193519269 }, { i32, float, i32 } { i32 22, float 22.0, i32 3020217723 }, { i32, float, i32 } { i32 21, float 21.0, i32 1873750857 }, { i32, float, i32 } { i32 20, float 20.0, i32 1382288114 }, { i32, float, i32 } { i32 19, float 19.0, i32 2554460247 }, { i32, float, i32 } { i32 18, float 18.0, i32 4138371185 }, { i32, float, i32 } { i32 17, float 17.0, i32 1914458558 }, { i32, float, i32 } { i32 16, float 16.0, i32 3433343165 }, { i32, float, i32 } { i32 15, float 15.0, i32 887756089 }, { i32, float, i32 } { i32 14, float 14.0, i32 4130324009 }, { i32, float, i32 } { i32 13, float 13.0, i32 3052962257 }, { i32, float, i32 } { i32 12, float 12.0, i32 3609279420 }, { i32, float, i32 } { i32 11, float 11.0, i32 4114266989 }, { i32, float, i32 } { i32 10, float 10.0, i32 2838627705 }, { i32, float, i32 } { i32 9, float 9.0, i32 2145213143 }, { i32, float, i32 } { i32 8, float 8.0, i32 2632690152 }, { i32, float, i32 } { i32 7, float 7.0, i32 2065162004 }, { i32, float, i32 } { i32 6, float 6.0, i32 1886925676 }, { i32, float, i32 } { i32 5, float 5.0, i32 3199944156 }, { i32, float, i32 } { i32 4, float 4.0, i32 1951803668 }, { i32, float, i32 } { i32 3, float 3.0, i32 3809749232 }, { i32, float, i32 } { i32 2, float 2.0, i32 975901834 }, { i32, float, i32 } { i32 1, float 1.0, i32 269157861 }, { i32, float, i32 } { i32 0, float 0.0, i32 1 } ]
@map.large.reverse.index = private constant [128 x { i32, i32 }] [ { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1, i32 64 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 849014403, i32 19 }, { i32, i32 } { i32 1204080003, i32 25 }, { i32, i32 } { i32 2837426180, i32 8 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 975901834, i32 62 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1381736465, i32 7 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2065162004, i32 57 }, { i32, i32 } { i32 1951803668, i32 60 }, { i32, i32 } { i32 21004309, i32 1 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 4017491480, i32 27 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1938557595, i32 9 }, { i32, i32 } { i32 2738543644, i32 17 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1823867422, i32 5 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3564271138, i32 22 }, { i32, i32 } { i32 863116194, i32 36 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3149438245, i32 23 }, { i32, i32 } { i32 4038300453, i32 24 }, { i32, i32 } { i32 3193519269, i32 41 }, { i32, i32 } { i32 2783218727, i32 12 }, { i32, i32 } { i32 4130324009, i32 50 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3244320560, i32 14 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 114556472, i32 39 }, { i32, i32 } { i32 3683044024, i32 40 }, { i32, i32 } { i32 887756089, i32 49 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2825136188, i32 30 }, { i32, i32 } { i32 3609279420, i32 52 }, { i32, i32 } { i32 3433343165, i32 48 }, { i32, i32 } { i32 1914458558, i32 47 }, { i32, i32 } { i32 113255102, i32 29 }, { i32, i32 } { i32 1741705023, i32 3 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1233104197, i32 32 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2181589191, i32 31 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1873750857, i32 43 }, { i32, i32 } { i32 3348682954, i32 21 }, { i32, i32 } { i32 2092071243, i32 18 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3052962257, i32 51 }, { i32, i32 } { i32 666324818, i32 35 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2813008852, i32 11 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 2554460247, i32 45 }, { i32, i32 } { i32 2145213143, i32 55 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 4194961115, i32 38 }, { i32, i32 } { i32 818264924, i32 10 }, { i32, i32 } { i32 3199944156, i32 59 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3261253855, i32 15 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3981775074, i32 28 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3100111204, i32 16 }, { i32, i32 } { i32 3551024357, i32 4 }, { i32, i32 } { i32 74379621, i32 33 }, { i32, i32 } { i32 269157861, i32 63 }, { i32, i32 } { i32 2632690152, i32 56 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 365830251, i32 2 }, { i32, i32 } { i32 1886925676, i32 58 }, { i32, i32 } { i32 4114266989, i32 53 }, { i32, i32 } { i32 1712690030, i32 26 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 3809749232, i32 61 }, { i32, i32 } { i32 4138371185, i32 46 }, { i32, i32 } { i32 1775512178, i32 34 }, { i32, i32 } { i32 1382288114, i32 44 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 1745468151, i32 20 }, { i32, i32 } { i32 3061479800, i32 6 }, { i32, i32 } { i32 4265669625, i32 13 }, { i32, i32 } { i32 2838627705, i32 54 }, { i32, i32 } { i32 3020217723, i32 42 }, { i32, i32 } { i32 858394364, i32 37 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 }, { i32, i32 } { i32 0, i32 0 } ]
@map.large.reverse = private constant { i32, i32, i32, i32, ptr, ptr } { i32 64, i32 64, i32 64, i32 128, ptr @map.large.reverse.entries, ptr @map.large.reverse.index }
@map.empty = private constant { i32, i32, i32, i32, ptr, ptr } zeroinitializer
@map.empty.backed = private constant { i32, i32, i32, i32, ptr, ptr } { i32 0, i32 0, i32 2, i32 0, ptr @map.a.entries, ptr null }
@map.text.a.entries = private constant { { ptr, i32, ptr }, float, i32 } { { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null }, float 2.0, i32 3814865824 }
@map.text.a = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.text.a.entries, ptr null }
@map.text.b.entries = private constant { { ptr, i32, ptr }, float, i32 } { { ptr, i32, ptr } { ptr @bytes.b, i32 3, ptr null }, float 2.0, i32 3814865824 }
@map.text.b = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.text.b.entries, ptr null }
@map.record.a.entries = private constant { i32, %record0, i32 } { i32 1, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.a, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, i32 269157861 }
@map.record.a = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.record.a.entries, ptr null }
@map.record.b.entries = private constant { i32, %record0, i32 } { i32 1, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.b, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, i32 269157861 }
@map.record.b = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.record.b.entries, ptr null }
@map.record.bad.entries = private constant { i32, %record0, i32 } { i32 1, %record0 { { i32, i32, ptr, ptr } { i32 2, i32 2, ptr @numbers.bad, ptr null }, { ptr, i32, ptr } { ptr @bytes.a, i32 3, ptr null } }, i32 269157861 }
@map.record.bad = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.record.bad.entries, ptr null }
@map.array.nan.entries = private constant { i32, { i32, i32, ptr, ptr }, i32 } { i32 1, { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.nan, ptr null }, i32 269157861 }
@map.array.nan = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.array.nan.entries, ptr null }
@map.array.nan2.entries = private constant { i32, { i32, i32, ptr, ptr }, i32 } { i32 1, { i32, i32, ptr, ptr } { i32 1, i32 1, ptr @numbers.nan2, ptr null }, i32 269157861 }
@map.array.nan2 = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.array.nan2.entries, ptr null }
@map.nested.a.entries = private constant { i32, %record3, i32 } { i32 1, %record3 { { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.a.entries, ptr null } }, i32 269157861 }
@map.nested.a = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.nested.a.entries, ptr null }
@map.nested.b.entries = private constant { i32, %record3, i32 } { i32 1, %record3 { { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.b.entries, ptr null } }, i32 269157861 }
@map.nested.b = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.nested.b.entries, ptr null }
@map.nested.bad.entries = private constant { i32, %record3, i32 } { i32 1, %record3 { { i32, i32, i32, i32, ptr, ptr } { i32 2, i32 2, i32 2, i32 0, ptr @map.bad.entries, ptr null } }, i32 269157861 }
@map.nested.bad = private constant { i32, i32, i32, i32, ptr, ptr } { i32 1, i32 1, i32 1, i32 0, ptr @map.nested.bad.entries, ptr null }
define internal i1 @equality_maps(ptr %ctx, ptr %session) {
entry:
  %map.check0 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.a, ptr @map.b)
  %map.good0 = and i1 true, %map.check0
  %map.check1 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.a, ptr @map.bad)
  %map.not1 = xor i1 %map.check1, true
  %map.good1 = and i1 %map.good0, %map.not1
  %map.check2 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.a, ptr @map.absent)
  %map.not2 = xor i1 %map.check2, true
  %map.good2 = and i1 %map.good1, %map.not2
  %map.check3 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.a, ptr @map.dead)
  %map.good3 = and i1 %map.good2, %map.check3
  %map.check4 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.empty, ptr @map.empty.backed)
  %map.good4 = and i1 %map.good3, %map.check4
  %map.check5 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.nan, ptr @map.nan)
  %map.not5 = xor i1 %map.check5, true
  %map.good5 = and i1 %map.good4, %map.not5
  %map.check6 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.nan, ptr @map.nan2)
  %map.not6 = xor i1 %map.check6, true
  %map.good6 = and i1 %map.good5, %map.not6
  %map.check7 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.negative, ptr @map.positive)
  %map.good7 = and i1 %map.good6, %map.check7
  %map.check8 = call i1 @chibi_value_equal.14(ptr %ctx, ptr %session, ptr @map.large, ptr @map.large.reverse)
  %map.good8 = and i1 %map.good7, %map.check8
  %map.check9 = call i1 @chibi_value_equal.15(ptr %ctx, ptr %session, ptr @map.text.a, ptr @map.text.b)
  %map.good9 = and i1 %map.good8, %map.check9
  %map.check10 = call i1 @chibi_value_equal.16(ptr %ctx, ptr %session, ptr @map.record.a, ptr @map.record.b)
  %map.good10 = and i1 %map.good9, %map.check10
  %map.check11 = call i1 @chibi_value_equal.16(ptr %ctx, ptr %session, ptr @map.record.a, ptr @map.record.bad)
  %map.not11 = xor i1 %map.check11, true
  %map.good11 = and i1 %map.good10, %map.not11
  %map.check12 = call i1 @chibi_value_equal.17(ptr %ctx, ptr %session, ptr @map.array.nan, ptr @map.array.nan)
  %map.not12 = xor i1 %map.check12, true
  %map.good12 = and i1 %map.good11, %map.not12
  %map.check13 = call i1 @chibi_value_equal.17(ptr %ctx, ptr %session, ptr @map.array.nan, ptr @map.array.nan2)
  %map.not13 = xor i1 %map.check13, true
  %map.good13 = and i1 %map.good12, %map.not13
  %map.check14 = call i1 @chibi_value_equal.18(ptr %ctx, ptr %session, ptr @map.nested.a, ptr @map.nested.b)
  %map.good14 = and i1 %map.good13, %map.check14
  %map.check15 = call i1 @chibi_value_equal.18(ptr %ctx, ptr %session, ptr @map.nested.a, ptr @map.nested.bad)
  %map.not15 = xor i1 %map.check15, true
  %map.good15 = and i1 %map.good14, %map.not15
  ret i1 %map.good15
}
