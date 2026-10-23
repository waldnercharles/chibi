target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.trap()

define i32 @chibi_scalar(i32 %arg0, i1 %arg1) {
entry:
  %budget0 = alloca i32
  store i32 64, ptr %budget0
  %s0 = alloca i32
  %s1 = alloca i1
  %s2 = alloca i1
  %s3 = alloca i32
  %s4 = alloca i32
  %l0 = alloca i32
  store i32 0, ptr %l0
  %l1 = alloca i1
  store i1 0, ptr %l1
  %result0 = alloca i32
  store i32 0, ptr %result0
  br label %p0
p0:
  store i32 %arg0, ptr %s0
  br label %p1
p1:
  %a1 = load i32, ptr %s0
  store i32 %a1, ptr %l0
  br label %p2
p2:
  store i1 %arg1, ptr %s1
  br label %p3
p3:
  %a3 = load i1, ptr %s1
  store i1 %a3, ptr %l1
  br label %p4
p4:
  %remaining4 = load i32, ptr %budget0
  %available4 = icmp sgt i32 %remaining4, 0
  br i1 %available4, label %budget_ok4, label %trap
budget_ok4:
  %next_budget4 = sub i32 %remaining4, 1
  store i32 %next_budget4, ptr %budget0
  br label %p5
p5:
  %a5 = load i1, ptr %l1
  store i1 %a5, ptr %s2
  br label %p6
p6:
  %a6 = load i1, ptr %s2
  br i1 %a6, label %p8, label %p7
p7:
  br label %p12
p8:
  %remaining8 = load i32, ptr %budget0
  %available8 = icmp sgt i32 %remaining8, 0
  br i1 %available8, label %budget_ok8, label %trap
budget_ok8:
  %next_budget8 = sub i32 %remaining8, 1
  store i32 %next_budget8, ptr %budget0
  br label %p9
p9:
  %a9 = load i32, ptr %l0
  store i32 %a9, ptr %s3
  br label %p10
p10:
  %a10 = load i32, ptr %s3
  store i32 %a10, ptr %result0
  br label %p11
p11:
  %return11 = load i32, ptr %result0
  ret i32 %return11
p12:
  %remaining12 = load i32, ptr %budget0
  %available12 = icmp sgt i32 %remaining12, 0
  br i1 %available12, label %budget_ok12, label %trap
budget_ok12:
  %next_budget12 = sub i32 %remaining12, 1
  store i32 %next_budget12, ptr %budget0
  br label %p13
p13:
  store i32 0, ptr %s4
  br label %p14
p14:
  %a14 = load i32, ptr %s4
  store i32 %a14, ptr %result0
  br label %p15
p15:
  %return15 = load i32, ptr %result0
  ret i32 %return15
p16:
  %return16 = load i32, ptr %result0
  ret i32 %return16
trap:
  call void @llvm.trap()
  unreachable
}
