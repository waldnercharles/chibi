; Test-only allocator accounting around the emitter's real map operations.
declare ptr @malloc(i64)
declare void @free(ptr)
declare i32 @printf(ptr, ...)
declare void @abort() noreturn
declare void @exit(i32) noreturn
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

@map_test.active = internal global i64 0
@map_test.failure = private constant [25 x i8] c"map assertion %d failed\0A\00"
@map_test.leak = private constant [28 x i8] c"map allocations left: %lld\0A\00"
@map_test.context = internal global { ptr, ptr, ptr, ptr, ptr } {
  ptr null, ptr null, ptr @map_test.allocate, ptr @map_test.index, ptr @map_test.release
}

define internal ptr @map_test.allocate(ptr %ctx, i64 %bytes) {
entry:
  %reserved = add i64 %bytes, 16
  %allocation = call ptr @malloc(i64 %reserved)
  %good = icmp ne ptr %allocation, null
  br i1 %good, label %count, label %bad
bad:
  call void @abort()
  unreachable
count:
  store i64 %bytes, ptr %allocation
  %memory = getelementptr inbounds i8, ptr %allocation, i64 16
  %before = load i64, ptr @map_test.active
  %after = add i64 %before, 1
  store i64 %after, ptr @map_test.active
  ret ptr %memory
}

define internal void @map_test.release(ptr %ctx, ptr %memory) {
entry:
  %before = load i64, ptr @map_test.active
  %after = sub i64 %before, 1
  store i64 %after, ptr @map_test.active
  ; Volatile poison makes a read after a drop fail even when malloc has not
  ; reused the freed allocation yet. The prefix keeps its byte length.
  %allocation = getelementptr inbounds i8, ptr %memory, i64 -16
  %bytes = load i64, ptr %allocation
  call void @llvm.memset.p0.i64(ptr %memory, i8 -91, i64 %bytes, i1 true)
  call void @free(ptr %allocation)
  ret void
}

define internal void @map_test.index(ptr %ctx, i32 %index, i32 %count) {
entry:
  %expected = load i1, ptr @map_test.expected_index
  %correct_index = icmp eq i32 %index, 3
  %correct_count = icmp eq i32 %count, 12
  %correct = and i1 %correct_index, %correct_count
  %pass = and i1 %expected, %correct
  br i1 %pass, label %done, label %bad
done:
  call void @exit(i32 0)
  unreachable
bad:
  call void @abort()
  unreachable
}

; Check logical space bounds after a map shrinks. The actual map helpers
; build and delete 4096 entries; the final64 must not keep clearing an index
; sized for the historical peak during later churn.
define internal i1 @map_test.bounds(ptr %ctx) {
entry:
  br label %insert
insert:
  %i = phi i32 [ 0, %entry ], [ %next, %inserted ]
  %m = phi { i32, i32, i32, i32, ptr, ptr } [ zeroinitializer, %entry ], [ %added, %inserted ]
  %full = icmp eq i32 %i, 4096
  br i1 %full, label %deleting, label %inserted
inserted:
  %added = call { i32, i32, i32, i32, ptr, ptr } @chibi_map.2150.set(ptr %ctx, { i32, i32, i32, i32, ptr, ptr } %m, i32 %i, i32 %i, i1 true)
  %next = add i32 %i, 1
  br label %insert
deleting:
  br label %delete
delete:
  %d = phi i32 [ 0, %deleting ], [ %dnext, %deleted ]
  %dm = phi { i32, i32, i32, i32, ptr, ptr } [ %m, %deleting ], [ %cut, %deleted ]
  %done = icmp eq i32 %d, 4032
  br i1 %done, label %check, label %deleted
deleted:
  %cut = call { i32, i32, i32, i32, ptr, ptr } @chibi_map.2150.delete(ptr %ctx, { i32, i32, i32, i32, ptr, ptr } %dm, i32 %d, i1 true)
  %dnext = add i32 %d, 1
  br label %delete
check:
  %capacity = extractvalue { i32, i32, i32, i32, ptr, ptr } %dm, 2
  %slots = extractvalue { i32, i32, i32, i32, ptr, ptr } %dm, 3
  %bounded_capacity = icmp ule i32 %capacity, 256
  %bounded_slots = icmp ule i32 %slots, 512
  %bounded = and i1 %bounded_capacity, %bounded_slots
  call void @chibi_map.2150.drop(ptr %ctx, { i32, i32, i32, i32, ptr, ptr } %dm)
  %small = call i32 @chibi_map.capacity(i32 0)
  %firstlarge = call i32 @chibi_map.capacity(i32 805306369)
  %lastlarge = call i32 @chibi_map.capacity(i32 1073741823)
  %maximumslots = call i32 @chibi_map.slots(i32 1073741824)
  %small_valid = icmp eq i32 %small, 4
  %first_valid = icmp eq i32 %firstlarge, 1073741824
  %last_valid = icmp eq i32 %lastlarge, 1073741824
  %slots_valid = icmp eq i32 %maximumslots, -2147483648
  %large_valid = and i1 %first_valid, %last_valid
  %planning_valid = and i1 %small_valid, %slots_valid
  %capacity_valid = and i1 %large_valid, %planning_valid
  %valid = and i1 %bounded, %capacity_valid
  ret i1 %valid
}

define i32 @main() {
entry:
  %bounded = call i1 @map_test.bounds(ptr @map_test.context)
  br i1 %bounded, label %run, label %bounds_failed
bounds_failed:
  ret i32 4
run:
  %result = call i32 @chibi_scalar(ptr @map_test.context)
  %expected = load i1, ptr @map_test.expected_index
  br i1 %expected, label %missing_index, label %normal
missing_index:
  ret i32 3
normal:
  %failure_at = getelementptr inbounds { ptr, ptr, ptr, ptr, ptr }, ptr @map_test.context, i32 0, i32 1
  %failure = load ptr, ptr %failure_at
  %failed = icmp ne ptr %failure, null
  br i1 %failed, label %assertion, label %balance
assertion:
  %left_at = getelementptr inbounds { i64, i32, i32 }, ptr %failure, i32 0, i32 1
  %left = load i32, ptr %left_at
  %printed = call i32 (ptr, ...) @printf(ptr @map_test.failure, i32 %left)
  ret i32 1
balance:
  %active = load i64, ptr @map_test.active
  %balanced = icmp eq i64 %active, 0
  br i1 %balanced, label %done, label %leak
leak:
  %reported = call i32 (ptr, ...) @printf(ptr @map_test.leak, i64 %active)
  ret i32 2
done:
  ret i32 %result
}
