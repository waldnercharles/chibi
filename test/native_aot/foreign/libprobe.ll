; The vendored library the foreign-import test links, built by
;   llc -mtriple=x86_64-unknown-linux-gnu -filetype=obj -relocation-model=pic -O2 libprobe.ll -o libprobe.o
;   llvm-ar rcs libprobe.a libprobe.o
; A prebuilt archive, as a vendored library is: the build links it and never compiles it.
target triple = "x86_64-unknown-linux-gnu"

define i32 @chibi_probe_add(i32 %a, i32 %b) {
  %c = add i32 %a, %b
  ret i32 %c
}

define zeroext i1 @chibi_probe_pick(i32 %n, i1 zeroext %flag) {
  %big = icmp sgt i32 %n, 3
  %both = and i1 %big, %flag
  ret i1 %both
}

define double @chibi_probe_scale(double %value, double %by) {
  %scaled = fmul double %value, %by
  ret double %scaled
}
