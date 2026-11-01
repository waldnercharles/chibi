import sys, re
exec(open('/tmp/w22-map/gen.py').read())
M='{ i32, i32, ptr, ptr }'
TEXT='{ ptr, i32, ptr }'
def release_text(x, tag):
    return f"  %o.{tag} = extractvalue {TEXT} {x}, 2\n  call void @chibi_scalar.free(ptr %ctx, ptr %o.{tag})\n"
def retain_text(x, tag):
    return f"  %o.{tag} = extractvalue {TEXT} {x}, 2\n  call void @chibi_scalar.retain(ptr %o.{tag})\n"
def typed(N, K, V):
    E='{ '+K+', '+V+' }'
    kt = K==TEXT; vt = V==TEXT
    if kt:
        pre=f"  %wide = call i64 @chibi_map.text_hash({K} %key)"
        same=f"  %same = call i1 @chibi_scalar.text_equal({K} %a, {K} %b)"
    elif K=='i64':
        pre="  %wide = add i64 %key, 0"
        same="  %same = icmp eq i64 %a, %b"
    else:
        pre=f"  %wide = zext {K} %key to i64"
        same=f"  %same = icmp eq {K} %a, %b"
    replaced=''
    if vt:
        replaced+=f"  %old = load {V}, ptr %rv\n" + release_text('%old','rv')
    if kt:
        replaced+=release_text('%key','rk')
    deleted=''
    if kt:
        deleted+=f"  %dk = getelementptr inbounds {E}, ptr %hole, i32 0, i32 0\n  %dkv = load {K}, ptr %dk\n"
        deleted+=release_text('%dkv','dk')
    if vt:
        deleted+=f"  %dv = getelementptr inbounds {E}, ptr %hole, i32 0, i32 1\n  %dvv = load {V}, ptr %dv\n"
        deleted+=release_text('%dvv','dv')
    def loop(fresh, fn, p, pred):
        if not kt and not vt:
            return f"  br label %{p}d\n{p}d:\n"
        body=''
        if kt:
            body+=f"  %{p}k = getelementptr inbounds {E}, ptr {fresh}, i64 %{p}off, i32 0\n  %{p}kv = load {K}, ptr %{p}k\n"
            body+=fn(f"%{p}kv", p+'k')
        if vt:
            body+=f"  %{p}v = getelementptr inbounds {E}, ptr {fresh}, i64 %{p}off, i32 1\n  %{p}vv = load {V}, ptr %{p}v\n"
            body+=fn(f"%{p}vv", p+'v')
        return (f"  br label %{p}head\n{p}head:\n  %{p}i = phi i32 [ 0, %{pred} ], [ %{p}next, %{p}body ]\n"
                f"  %{p}end = icmp eq i32 %{p}i, %count\n  br i1 %{p}end, label %{p}d, label %{p}body\n{p}body:\n"
                f"  %{p}off = zext i32 %{p}i to i64\n{body}  %{p}next = add i32 %{p}i, 1\n  br label %{p}head\n{p}d:\n")
    retained=loop('%fresh', retain_text, 'r', 'clone')
    released=loop('%entries', release_text, 'x', 'entry')
    t=TYPED
    for k,v in {'$PREHASH':pre,'$SAME':same,'$REPLACED':replaced.rstrip('\n'),'$DELETED':deleted.rstrip('\n'),'$RETAINED':retained.rstrip('\n'),'$RELEASED':released.rstrip('\n')}.items():
        t=t.replace(k,v)
    t=t.replace('$M',M).replace('$E',E).replace('$K',K).replace('$V',V).replace('$N',str(N))
    return t
RUNTIME = r'''
define internal void @chibi_scalar.copy(ptr %to, ptr %from, i64 %bytes) #1 {
entry:
  br label %head
head:
  %at = phi i64 [ 0, %entry ], [ %next, %body ]
  %done = icmp eq i64 %at, %bytes
  br i1 %done, label %exit, label %body
body:
  %source = getelementptr inbounds i8, ptr %from, i64 %at
  %byte = load i8, ptr %source
  %target = getelementptr inbounds i8, ptr %to, i64 %at
  store i8 %byte, ptr %target
  %next = add i64 %at, 1
  br label %head
exit:
  ret void
}
define internal i1 @chibi_scalar.text_equal({ ptr, i32, ptr } %a, { ptr, i32, ptr } %b) #0 {
entry:
  %a.bytes = extractvalue { ptr, i32, ptr } %a, 0
  %a.length = extractvalue { ptr, i32, ptr } %a, 1
  %b.bytes = extractvalue { ptr, i32, ptr } %b, 0
  %b.length = extractvalue { ptr, i32, ptr } %b, 1
  %same = icmp eq i32 %a.length, %b.length
  br i1 %same, label %head, label %differ
head:
  %index = phi i32 [ 0, %entry ], [ %next, %byte ]
  %done = icmp eq i32 %index, %a.length
  br i1 %done, label %equal, label %byte
byte:
  %offset = zext i32 %index to i64
  %a.at = getelementptr inbounds i8, ptr %a.bytes, i64 %offset
  %b.at = getelementptr inbounds i8, ptr %b.bytes, i64 %offset
  %a.byte = load i8, ptr %a.at
  %b.byte = load i8, ptr %b.at
  %next = add i32 %index, 1
  %matches = icmp eq i8 %a.byte, %b.byte
  br i1 %matches, label %head, label %differ
equal:
  ret i1 true
differ:
  ret i1 false
}
define internal void @chibi_scalar.retain(ptr %owner) {
entry:
  %counted = icmp ne ptr %owner, null
  br i1 %counted, label %count, label %done
count:
  %before = atomicrmw add ptr %owner, i64 1 monotonic
  br label %done
done:
  ret void
}
define internal void @chibi_scalar.free(ptr %ctx, ptr %owner) {
entry:
  %counted = icmp ne ptr %owner, null
  br i1 %counted, label %count, label %done
count:
  %before = atomicrmw sub ptr %owner, i64 1 acq_rel
  %last = icmp eq i64 %before, 1
  br i1 %last, label %release, label %done
release:
  %at = getelementptr inbounds { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 4
  %give = load ptr, ptr %at
  call void %give(ptr %ctx, ptr %owner)
  br label %done
done:
  ret void
}
attributes #0 = { nounwind }
attributes #1 = { noinline nounwind "no-builtins" }
'''
DRIVER_INTS = r'''
define i64 @run_ints(ptr %ctx, i32 %n) {
entry:
  br label %ihead
ihead:
  %i = phi i32 [ 0, %entry ], [ %inext, %ibody ]
  %m = phi { i32, i32, ptr, ptr } [ zeroinitializer, %entry ], [ %m1, %ibody ]
  %iend = icmp eq i32 %i, %n
  br i1 %iend, label %del, label %ibody
ibody:
  %k = mul i32 %i, 7
  %k1 = add i32 %k, 1
  %m1 = call { i32, i32, ptr, ptr } @chibi_map.1.set(ptr %ctx, { i32, i32, ptr, ptr } %m, i32 %k1, i32 %i, i1 true)
  %inext = add i32 %i, 1
  br label %ihead
del:
  br label %dhead
dhead:
  %d = phi i32 [ 0, %del ], [ %dnext, %dstep ]
  %dm = phi { i32, i32, ptr, ptr } [ %m, %del ], [ %dm2, %dstep ]
  %dend = icmp eq i32 %d, %n
  br i1 %dend, label %sum, label %dbody
dbody:
  %r = urem i32 %d, 3
  %gone = icmp eq i32 %r, 0
  br i1 %gone, label %dodel, label %dstep
dodel:
  %dk = mul i32 %d, 7
  %dk1 = add i32 %dk, 1
  %dm1 = call { i32, i32, ptr, ptr } @chibi_map.1.delete(ptr %ctx, { i32, i32, ptr, ptr } %dm, i32 %dk1, i1 true)
  br label %dstep
dstep:
  %dm2 = phi { i32, i32, ptr, ptr } [ %dm, %dbody ], [ %dm1, %dodel ]
  %dnext = add i32 %d, 1
  br label %dhead
sum:
  %copy = call { i32, i32, ptr, ptr } @chibi_map.1.copy(ptr %ctx, { i32, i32, ptr, ptr } %dm)
  br label %shead
shead:
  %s = phi i32 [ 0, %sum ], [ %snext, %sbody ]
  %acc = phi i64 [ 0, %sum ], [ %acc2, %sbody ]
  %send = icmp eq i32 %s, %n
  br i1 %send, label %fin, label %sbody
sbody:
  %sk = mul i32 %s, 7
  %sk1 = add i32 %sk, 1
  %v = call i32 @chibi_map.1.get({ i32, i32, ptr, ptr } %copy, i32 %sk1)
  %h = call i1 @chibi_map.1.has({ i32, i32, ptr, ptr } %copy, i32 %sk1)
  %hv = zext i1 %h to i64
  %vv = zext i32 %v to i64
  %t1 = mul i64 %acc, 31
  %t2 = add i64 %t1, %vv
  %t3 = add i64 %t2, %hv
  %acc2 = add i64 %t3, 0
  %snext = add i32 %s, 1
  br label %shead
fin:
  %cnt = extractvalue { i32, i32, ptr, ptr } %copy, 0
  %c64 = zext i32 %cnt to i64
  %mix = mul i64 %acc, 1000003
  %res = add i64 %mix, %c64
  call void @chibi_map.1.drop(ptr %ctx, { i32, i32, ptr, ptr } %copy)
  call void @chibi_map.1.drop(ptr %ctx, { i32, i32, ptr, ptr } %dm)
  ret i64 %res
}
'''
DRIVER_TEXT = r'''
define { ptr, i32, ptr } @key(ptr %ctx, i32 %i) {
entry:
  %buf = call ptr @chibi_map.allocate(ptr %ctx, i64 16)
  store i8 107, ptr %buf
  %a = udiv i32 %i, 100
  %b0 = urem i32 %i, 100
  %b = udiv i32 %b0, 10
  %c = urem i32 %i, 10
  %a8 = trunc i32 %a to i8
  %b8 = trunc i32 %b to i8
  %c8 = trunc i32 %c to i8
  %ad = add i8 %a8, 48
  %bd = add i8 %b8, 48
  %cd = add i8 %c8, 48
  %p1 = getelementptr inbounds i8, ptr %buf, i64 1
  store i8 %ad, ptr %p1
  %p2 = getelementptr inbounds i8, ptr %buf, i64 2
  store i8 %bd, ptr %p2
  %p3 = getelementptr inbounds i8, ptr %buf, i64 3
  store i8 %cd, ptr %p3
  %t0 = insertvalue { ptr, i32, ptr } zeroinitializer, ptr %buf, 0
  %t1 = insertvalue { ptr, i32, ptr } %t0, i32 4, 1
  ret { ptr, i32, ptr } %t1
}
define i64 @run_text(ptr %ctx, i32 %n) {
entry:
  br label %ihead
ihead:
  %i = phi i32 [ 0, %entry ], [ %inext, %ibody ]
  %m = phi { i32, i32, ptr, ptr } [ zeroinitializer, %entry ], [ %m1, %ibody ]
  %iend = icmp eq i32 %i, %n
  br i1 %iend, label %del, label %ibody
ibody:
  %k = call { ptr, i32, ptr } @key(ptr %ctx, i32 %i)
  %m1 = call { i32, i32, ptr, ptr } @chibi_map.2.set(ptr %ctx, { i32, i32, ptr, ptr } %m, { ptr, i32, ptr } %k, i32 %i, i1 true)
  %inext = add i32 %i, 1
  br label %ihead
del:
  br label %dhead
dhead:
  %d = phi i32 [ 0, %del ], [ %dnext, %dstep ]
  %dm = phi { i32, i32, ptr, ptr } [ %m, %del ], [ %dm2, %dstep ]
  %dend = icmp eq i32 %d, %n
  br i1 %dend, label %sum, label %dbody
dbody:
  %r = urem i32 %d, 3
  %gone = icmp eq i32 %r, 0
  br i1 %gone, label %dodel, label %dstep
dodel:
  %dk = call { ptr, i32, ptr } @key(ptr %ctx, i32 %d)
  %dm1 = call { i32, i32, ptr, ptr } @chibi_map.2.delete(ptr %ctx, { i32, i32, ptr, ptr } %dm, { ptr, i32, ptr } %dk, i1 true)
  br label %dstep
dstep:
  %dm2 = phi { i32, i32, ptr, ptr } [ %dm, %dbody ], [ %dm1, %dodel ]
  %dnext = add i32 %d, 1
  br label %dhead
sum:
  br label %shead
shead:
  %s = phi i32 [ 0, %sum ], [ %snext, %sbody ]
  %acc = phi i64 [ 0, %sum ], [ %acc2, %sbody ]
  %send = icmp eq i32 %s, %n
  br i1 %send, label %fin, label %sbody
sbody:
  %sk = call { ptr, i32, ptr } @key(ptr %ctx, i32 %s)
  %v = call i32 @chibi_map.2.get({ i32, i32, ptr, ptr } %dm, { ptr, i32, ptr } %sk)
  %h = call i1 @chibi_map.2.has({ i32, i32, ptr, ptr } %dm, { ptr, i32, ptr } %sk)
  %hv = zext i1 %h to i64
  %vv = zext i32 %v to i64
  %t1 = mul i64 %acc, 31
  %t2 = add i64 %t1, %vv
  %acc2 = add i64 %t2, %hv
  %snext = add i32 %s, 1
  br label %shead
fin:
  %cnt = extractvalue { i32, i32, ptr, ptr } %dm, 0
  %c64 = zext i32 %cnt to i64
  %mix = mul i64 %acc, 1000003
  %res = add i64 %mix, %c64
  ret i64 %res
}
'''
if __name__=='__main__':
    out="target triple = \"arm64-apple-macosx11.0.0\"\n"+RUNTIME+GENERIC+typed(1,'i32','i32')+typed(2,TEXT,'i32')+DRIVER_INTS+DRIVER_TEXT
    open('/tmp/w22-map/test.ll','w').write(out)
