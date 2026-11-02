; The source fixture advances host simulation time through ordinary foreign
; calls. The generated scheduler still owns every task and deadline.
define i32 @TestTime(double %now) {
entry:
  %status = call i32 @chibi_host_tasks_set_time(double %now)
  ret i32 %status
}

define double @TestNaN() {
entry:
  ret double 0x7FF8000000000000
}
