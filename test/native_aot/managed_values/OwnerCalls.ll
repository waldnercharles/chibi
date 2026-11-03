@chibi_value_test_owner_calls_total = internal global i64 0
@chibi_value_test_owner_calls_first = internal global i64 0
@chibi_value_test_owner_calls_seen = internal global i1 false
@chibi_value_test_owner_calls_format = private constant [24 x i8] c"deep owner calls: %lld\0A\00"
declare i32 @printf(ptr, ...)

define i64 @chibi_value_test_owner_calls() {
entry:
  %total = load i64, ptr @chibi_value_test_owner_calls_total
  %seen = load i1, ptr @chibi_value_test_owner_calls_seen
  br i1 %seen, label %report, label %first
first:
  store i64 %total, ptr @chibi_value_test_owner_calls_first
  store i1 true, ptr @chibi_value_test_owner_calls_seen
  ret i64 %total
report:
  %before = load i64, ptr @chibi_value_test_owner_calls_first
  %delta = sub i64 %total, %before
  %printed = call i32 (ptr, ...) @printf(ptr @chibi_value_test_owner_calls_format, i64 %delta)
  ret i64 %total
}
