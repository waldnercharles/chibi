; A real static Linux program with no libc and an unresolved weak errno
; accessor. The requested poll must use the null-accessor path and drain its
; request without reading through the empty scoped root registration.
@poll_test.heap = internal global %chibi.managed.heap zeroinitializer
@poll_test.context = internal global {ptr,ptr,ptr,ptr,ptr,ptr} {ptr null,ptr null,ptr null,ptr null,ptr null,ptr @poll_test.heap}

define void @chibi_native_failure(i32 %kind) noreturn {
  call void @llvm.trap()
  unreachable
}
; LLVM can lower a variable-size zero inside an unreachable helper to memset
; at O0. Supply the ordinary freestanding loop rather than importing libc.
define ptr @memset(ptr %to, i32 %value, i64 %bytes) noinline nounwind "no-builtins" {
entry:
  %byte = trunc i32 %value to i8
  br label %test
test:
  %index = phi i64 [0,%entry],[%next,%write]
  %done = icmp eq i64 %index,%bytes
  br i1 %done,label %exit,label %write
write:
  %at = getelementptr i8,ptr %to,i64 %index
  store i8 %byte,ptr %at
  %next = add i64 %index,1
  br label %test
exit:
  ret ptr %to
}
define void @_start() noreturn {
entry:
  %weak = icmp eq ptr @__errno_location,null
  %requested = getelementptr %chibi.managed.heap,ptr @poll_test.heap,i32 0,i32 9
  store i1 true,ptr %requested
  %result = call i32 @chibi_scalar(ptr @poll_test.context)
  %pending = load i1,ptr %requested
  %drained = xor i1 %pending,true
  %returned = icmp eq i32 %result,0
  %safe = and i1 %drained,%returned
  %valid = and i1 %safe,%weak
  %status = select i1 %valid,i64 0,i64 99
  call void asm sideeffect "syscall", "{rax},{rdi},~{rcx},~{r11},~{memory}"(i64 60,i64 %status)
  unreachable
}
