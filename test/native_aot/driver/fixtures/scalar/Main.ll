target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.trap()

define i32 @chibi_scalar() #0 {
entry:
  %budget0 = alloca i32
  store i32 1, ptr %budget0
  %s0 = alloca i32
  %result0 = alloca i32
  store i32 0, ptr %result0
  br label %p0
p0:
  %remaining0 = load i32, ptr %budget0
  %available0 = icmp sgt i32 %remaining0, 0
  br i1 %available0, label %budget_ok0, label %trap
budget_ok0:
  %next_budget0 = sub i32 %remaining0, 1
  store i32 %next_budget0, ptr %budget0
  br label %p1
p1:
  store i32 42, ptr %s0
  br label %p2
p2:
  %a2 = load i32, ptr %s0
  store i32 %a2, ptr %result0
  br label %p3
p3:
  %return3 = load i32, ptr %result0
  ret i32 %return3
trap:
  call void @llvm.trap()
  unreachable
}

attributes #0 = { "probe-stack"="inline-asm" }
