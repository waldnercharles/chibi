; Independent C-ABI caller: signed byte and bool arguments, a true void
; return, and an observable effect passed back through another void call.
@callback.observed = internal global i32 -999

define void @callback_record(i32 %value, i1 zeroext %flag) {
  %extra = select i1 %flag, i32 100, i32 0
  %result = add i32 %value, %extra
  store i32 %result, ptr @callback.observed
  ret void
}

define i32 @callback_visit(ptr %visit, i8 signext %value, i1 zeroext %flag) {
  store i32 -999, ptr @callback.observed
  call void %visit(i8 signext %value, i1 zeroext %flag)
  %result = load i32, ptr @callback.observed
  ret i32 %result
}
