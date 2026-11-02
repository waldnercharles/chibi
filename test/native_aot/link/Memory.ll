; 1,056 cases per level: unaligned boundaries through 4,097 bytes, low-byte
; fill conversion, unchanged guards, both overlap directions and returned ptrs.
@lengths = private constant [22 x i64] [i64 0, i64 1, i64 7, i64 8, i64 15, i64 16, i64 17, i64 31, i64 32, i64 33, i64 63, i64 64, i64 65, i64 127, i64 128, i64 129, i64 255, i64 256, i64 257, i64 4095, i64 4096, i64 4097]
@offsets = private constant [4 x i64] [i64 0, i64 1, i64 7, i64 15]
@fills = private constant [4 x i32] [i32 0, i32 -1, i32 257, i32 2147483647]
@fill_bytes = private constant [4 x i8] [i8 0, i8 255, i8 1, i8 255]
declare ptr @memset(ptr, i32, i64)
declare ptr @memcpy(ptr, ptr, i64)
declare ptr @memmove(ptr, ptr, i64)

define internal i8 @pattern(i64 %index) {
  %scaled = mul i64 %index, 17
  %shifted = add i64 %scaled, 13
  %byte = trunc i64 %shifted to i8
  ret i8 %byte
}

define internal i1 @check(i32 %mode, i64 %length, i64 %source, i64 %destination, i32 %fill, i8 %expected_fill) noinline "no-builtins" {
entry:
  %a = alloca [4352 x i8], align 16
  %b = alloca [4352 x i8], align 16
  br label %initialize
initialize:
  %i = phi i64 [0, %entry], [%next_i, %initialize]
  %pa = getelementptr inbounds i8, ptr %a, i64 %i
  %pb = getelementptr inbounds i8, ptr %b, i64 %i
  %original = call i8 @pattern(i64 %i)
  %other_index = add i64 %i, 91
  %other = call i8 @pattern(i64 %other_index)
  store i8 %original, ptr %pa
  store i8 %other, ptr %pb
  %next_i = add i64 %i, 1
  %initialized = icmp eq i64 %next_i, 4352
  br i1 %initialized, label %dispatch, label %initialize
dispatch:
  %from = getelementptr inbounds i8, ptr %a, i64 %source
  %to_a = getelementptr inbounds i8, ptr %a, i64 %destination
  %to_b = getelementptr inbounds i8, ptr %b, i64 %destination
  switch i32 %mode, label %unreachable [i32 0, label %set i32 1, label %copy i32 2, label %move]
set:
  %set_result = call ptr @memset(ptr %to_a, i32 %fill, i64 %length) nobuiltin
  %set_ptr = icmp eq ptr %set_result, %to_a
  br label %verify_entry
copy:
  %copy_result = call ptr @memcpy(ptr %to_b, ptr %from, i64 %length) nobuiltin
  %copy_ptr = icmp eq ptr %copy_result, %to_b
  br label %verify_entry
move:
  %move_result = call ptr @memmove(ptr %to_a, ptr %from, i64 %length) nobuiltin
  %move_ptr = icmp eq ptr %move_result, %to_a
  br label %verify_entry
verify_entry:
  %buffer = phi ptr [%a, %set], [%b, %copy], [%a, %move]
  %pointer = phi i1 [%set_ptr, %set], [%copy_ptr, %copy], [%move_ptr, %move]
  %is_set = icmp eq i32 %mode, 0
  %is_copy = icmp eq i32 %mode, 1
  %bias = select i1 %is_copy, i64 91, i64 0
  %end = add i64 %destination, %length
  br i1 %pointer, label %verify, label %wrong
verify:
  %j = phi i64 [0, %verify_entry], [%next_j, %continue]
  %after_start = icmp uge i64 %j, %destination
  %before_end = icmp ult i64 %j, %end
  %inside = and i1 %after_start, %before_end
  %displacement = sub i64 %j, %destination
  %copied_index = add i64 %source, %displacement
  %copied = call i8 @pattern(i64 %copied_index)
  %changed = select i1 %is_set, i8 %expected_fill, i8 %copied
  %untouched_index = add i64 %j, %bias
  %untouched = call i8 @pattern(i64 %untouched_index)
  %expected = select i1 %inside, i8 %changed, i8 %untouched
  %slot = getelementptr inbounds i8, ptr %buffer, i64 %j
  %actual = load i8, ptr %slot
  %equal = icmp eq i8 %actual, %expected
  br i1 %equal, label %continue, label %wrong
continue:
  %next_j = add i64 %j, 1
  %complete = icmp eq i64 %next_j, 4352
  br i1 %complete, label %right, label %verify
right:
  ret i1 true
wrong:
  ret i1 false
unreachable:
  unreachable
}

define i32 @chibi_scalar() "no-builtins" {
entry:
  %zero_set = call ptr @memset(ptr null, i32 -1, i64 0) nobuiltin
  %zero_copy = call ptr @memcpy(ptr null, ptr null, i64 0) nobuiltin
  %zero_move = call ptr @memmove(ptr null, ptr null, i64 0) nobuiltin
  %set_null = icmp eq ptr %zero_set, null
  %copy_null = icmp eq ptr %zero_copy, null
  %move_null = icmp eq ptr %zero_move, null
  %both = and i1 %set_null, %copy_null
  %all_null = and i1 %both, %move_null
  br i1 %all_null, label %mode, label %failed_null
mode:
  %m = phi i32 [0, %entry], [%next_m, %next_mode]
  br label %source
source:
  %s = phi i64 [0, %mode], [%next_s, %next_source]
  %source_at = getelementptr inbounds [4 x i64], ptr @offsets, i64 0, i64 %s
  %source_value = load i64, ptr %source_at
  %fill_at = getelementptr inbounds [4 x i32], ptr @fills, i64 0, i64 %s
  %fill = load i32, ptr %fill_at
  %byte_at = getelementptr inbounds [4 x i8], ptr @fill_bytes, i64 0, i64 %s
  %byte = load i8, ptr %byte_at
  br label %destination
destination:
  %d = phi i64 [0, %source], [%next_d, %next_destination]
  %destination_at = getelementptr inbounds [4 x i64], ptr @offsets, i64 0, i64 %d
  %destination_value = load i64, ptr %destination_at
  br label %length
length:
  %n = phi i64 [0, %destination], [%next_n, %next_length]
  %length_at = getelementptr inbounds [22 x i64], ptr @lengths, i64 0, i64 %n
  %length_value = load i64, ptr %length_at
  %ok = call i1 @check(i32 %m, i64 %length_value, i64 %source_value, i64 %destination_value, i32 %fill, i8 %byte)
  br i1 %ok, label %next_length, label %failed
next_length:
  %next_n = add i64 %n, 1
  %last_length = icmp eq i64 %next_n, 22
  br i1 %last_length, label %next_destination, label %length
next_destination:
  %next_d = add i64 %d, 1
  %last_destination = icmp eq i64 %next_d, 4
  br i1 %last_destination, label %next_source, label %destination
next_source:
  %next_s = add i64 %s, 1
  %last_source = icmp eq i64 %next_s, 4
  br i1 %last_source, label %next_mode, label %source
next_mode:
  %next_m = add i32 %m, 1
  %last_mode = icmp eq i32 %next_m, 3
  br i1 %last_mode, label %success, label %mode
failed:
  %code = add i32 %m, 1
  ret i32 %code
failed_null:
  ret i32 4
success:
  ret i32 42
}
