# Template for the generic and typed map helpers; $-placeholders replaced per map type.
GENERIC = r'''
define internal ptr @chibi_map.allocate(ptr %ctx, i64 %bytes) {
entry:
  %at = getelementptr inbounds { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 2
  %make = load ptr, ptr %at
  %fresh = call ptr %make(ptr %ctx, i64 %bytes)
  ret ptr %fresh
}
define internal void @chibi_map.give(ptr %ctx, ptr %memory) {
entry:
  %held = icmp ne ptr %memory, null
  br i1 %held, label %give, label %done
give:
  %at = getelementptr inbounds { ptr, ptr, ptr, ptr, ptr }, ptr %ctx, i32 0, i32 4
  %release = load ptr, ptr %at
  call void %release(ptr %ctx, ptr %memory)
  br label %done
done:
  ret void
}
define internal i32 @chibi_map.mix(i64 %wide) {
entry:
  %a = lshr i64 %wide, 30
  %b = xor i64 %wide, %a
  %c = mul i64 %b, -4658895280553007687
  %d = lshr i64 %c, 27
  %e = xor i64 %c, %d
  %f = mul i64 %e, -7723592293110705685
  %g = lshr i64 %f, 31
  %h = xor i64 %f, %g
  %hash = trunc i64 %h to i32
  ret i32 %hash
}
define internal i64 @chibi_map.text_hash({ ptr, i32, ptr } %text) {
entry:
  %bytes = extractvalue { ptr, i32, ptr } %text, 0
  %length = extractvalue { ptr, i32, ptr } %text, 1
  br label %head
head:
  %at = phi i32 [ 0, %entry ], [ %next, %body ]
  %hash = phi i64 [ -3750763034362895579, %entry ], [ %mixed, %body ]
  %end = icmp eq i32 %at, %length
  br i1 %end, label %done, label %body
body:
  %offset = zext i32 %at to i64
  %place = getelementptr inbounds i8, ptr %bytes, i64 %offset
  %byte = load i8, ptr %place
  %wide = zext i8 %byte to i64
  %folded = xor i64 %hash, %wide
  %mixed = mul i64 %folded, 1099511628211
  %next = add i32 %at, 1
  br label %head
done:
  ret i64 %hash
}
define internal void @chibi_map.place(ptr %index, i32 %hash, i32 %entry1) {
entry:
  %slots = load i32, ptr %index
  %mask = sub i32 %slots, 1
  %start = and i32 %hash, %mask
  %base = getelementptr inbounds i8, ptr %index, i64 8
  br label %walk
walk:
  %pos = phi i32 [ %start, %entry ], [ %pnext, %cont ]
  %dist = phi i32 [ 0, %entry ], [ %dnext, %cont ]
  %h = phi i32 [ %hash, %entry ], [ %hn, %cont ]
  %e = phi i32 [ %entry1, %entry ], [ %en, %cont ]
  %poff = zext i32 %pos to i64
  %sp = getelementptr inbounds { i32, i32 }, ptr %base, i64 %poff
  %sea = getelementptr inbounds { i32, i32 }, ptr %sp, i32 0, i32 1
  %sh = load i32, ptr %sp
  %se = load i32, ptr %sea
  %empty = icmp eq i32 %se, 0
  br i1 %empty, label %put, label %resident
put:
  store i32 %h, ptr %sp
  store i32 %e, ptr %sea
  ret void
resident:
  %home = and i32 %sh, %mask
  %gap = sub i32 %pos, %home
  %rdist = and i32 %gap, %mask
  %closer = icmp ult i32 %rdist, %dist
  br i1 %closer, label %swap, label %cont
swap:
  store i32 %h, ptr %sp
  store i32 %e, ptr %sea
  br label %cont
cont:
  %hn = phi i32 [ %h, %resident ], [ %sh, %swap ]
  %en = phi i32 [ %e, %resident ], [ %se, %swap ]
  %dc = phi i32 [ %dist, %resident ], [ %rdist, %swap ]
  %pn = add i32 %pos, 1
  %pnext = and i32 %pn, %mask
  %dnext = add i32 %dc, 1
  br label %walk
}
define internal ptr @chibi_map.reindex(ptr %ctx, ptr %old, i32 %slots, i32 %removed, i1 %owns) {
entry:
  %count = zext i32 %slots to i64
  %rows = add i64 %count, 1
  %bytes = shl i64 %rows, 3
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  store i32 %slots, ptr %fresh
  %has = icmp ne ptr %old, null
  br i1 %has, label %scan, label %done
scan:
  %oslots = load i32, ptr %old
  %obase = getelementptr inbounds i8, ptr %old, i64 8
  br label %head
head:
  %at = phi i32 [ 0, %scan ], [ %next, %step ]
  %end = icmp eq i32 %at, %oslots
  br i1 %end, label %free, label %look
look:
  %poff = zext i32 %at to i64
  %sp = getelementptr inbounds { i32, i32 }, ptr %obase, i64 %poff
  %sea = getelementptr inbounds { i32, i32 }, ptr %sp, i32 0, i32 1
  %sh = load i32, ptr %sp
  %se = load i32, ptr %sea
  %empty = icmp eq i32 %se, 0
  br i1 %empty, label %step, label %live
live:
  %idx = sub i32 %se, 1
  %gone = icmp eq i32 %idx, %removed
  br i1 %gone, label %step, label %keep
keep:
  %after = icmp ugt i32 %idx, %removed
  %shift = zext i1 %after to i32
  %moved = sub i32 %se, %shift
  call void @chibi_map.place(ptr %fresh, i32 %sh, i32 %moved)
  br label %step
step:
  %next = add i32 %at, 1
  br label %head
free:
  br i1 %owns, label %give, label %done
give:
  call void @chibi_map.give(ptr %ctx, ptr %old)
  br label %done
done:
  ret ptr %fresh
}
'''
TYPED = r'''
define internal i32 @chibi_map.$N.hash($K %key) {
entry:
$PREHASH
  %hash = call i32 @chibi_map.mix(i64 %wide)
  ret i32 %hash
}
define internal i1 @chibi_map.$N.same($K %a, $K %b) {
entry:
$SAME
  ret i1 %same
}
define internal i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %count, $K %wanted, i32 %hash) {
entry:
  %indexed = icmp ne ptr %index, null
  br i1 %indexed, label %probe, label %scan
scan:
  %at = phi i32 [ 0, %entry ], [ %next, %step ]
  %end = icmp eq i32 %at, %count
  br i1 %end, label %absent, label %look
look:
  %off = zext i32 %at to i64
  %ke = getelementptr inbounds $E, ptr %entries, i64 %off, i32 0
  %k = load $K, ptr %ke
  %eq = call i1 @chibi_map.$N.same($K %k, $K %wanted)
  br i1 %eq, label %found, label %step
step:
  %next = add i32 %at, 1
  br label %scan
found:
  ret i32 %at
absent:
  ret i32 -1
probe:
  %slots = load i32, ptr %index
  %mask = sub i32 %slots, 1
  %start = and i32 %hash, %mask
  %base = getelementptr inbounds i8, ptr %index, i64 8
  br label %walk
walk:
  %pos = phi i32 [ %start, %probe ], [ %pnext, %skip ]
  %dist = phi i32 [ 0, %probe ], [ %dnext, %skip ]
  %poff = zext i32 %pos to i64
  %sp = getelementptr inbounds { i32, i32 }, ptr %base, i64 %poff
  %sea = getelementptr inbounds { i32, i32 }, ptr %sp, i32 0, i32 1
  %sh = load i32, ptr %sp
  %se = load i32, ptr %sea
  %empty = icmp eq i32 %se, 0
  br i1 %empty, label %absent, label %resident
resident:
  %home = and i32 %sh, %mask
  %gap = sub i32 %pos, %home
  %rdist = and i32 %gap, %mask
  %closer = icmp ult i32 %rdist, %dist
  br i1 %closer, label %absent, label %compare
compare:
  %alike = icmp eq i32 %sh, %hash
  br i1 %alike, label %test, label %skip
test:
  %idx = sub i32 %se, 1
  %ioff = zext i32 %idx to i64
  %tke = getelementptr inbounds $E, ptr %entries, i64 %ioff, i32 0
  %tk = load $K, ptr %tke
  %teq = call i1 @chibi_map.$N.same($K %tk, $K %wanted)
  br i1 %teq, label %hit, label %skip
hit:
  ret i32 %idx
skip:
  %pn = add i32 %pos, 1
  %pnext = and i32 %pn, %mask
  %dnext = add i32 %dist, 1
  br label %walk
}
define internal $V @chibi_map.$N.get($M %m, $K %wanted) {
entry:
  %count = extractvalue $M %m, 0
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %count, $K %wanted, i32 %hash)
  %present = icmp sge i32 %at, 0
  br i1 %present, label %hit, label %miss
hit:
  %off = zext i32 %at to i64
  %ve = getelementptr inbounds $E, ptr %entries, i64 %off, i32 1
  %v = load $V, ptr %ve
  ret $V %v
miss:
  ret $V zeroinitializer
}
define internal i1 @chibi_map.$N.has($M %m, $K %wanted) {
entry:
  %count = extractvalue $M %m, 0
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %count, $K %wanted, i32 %hash)
  %present = icmp sge i32 %at, 0
  ret i1 %present
}
define internal $M @chibi_map.$N.set(ptr %ctx, $M %m, $K %key, $V %value, i1 %owns) {
entry:
  %count = extractvalue $M %m, 0
  %cap = extractvalue $M %m, 1
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
  %hash = call i32 @chibi_map.$N.hash($K %key)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %count, $K %key, i32 %hash)
  %present = icmp sge i32 %at, 0
  br i1 %present, label %replace, label %insert
replace:
  %roff = zext i32 %at to i64
  %rv = getelementptr inbounds $E, ptr %entries, i64 %roff, i32 1
$REPLACED
  store $V %value, ptr %rv
  ret $M %m
insert:
  %full = icmp eq i32 %count, %cap
  br i1 %full, label %grow, label %room
grow:
  %none = icmp eq i32 %cap, 0
  %double = shl i32 %cap, 1
  %newcap = select i1 %none, i32 4, i32 %double
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %cn = zext i32 %newcap to i64
  %bytes = mul i64 %cn, %width
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  %used = zext i32 %count to i64
  %moved = mul i64 %used, %width
  call void @chibi_scalar.copy(ptr %fresh, ptr %entries, i64 %moved)
  %had = icmp ne ptr %entries, null
  %freeold = and i1 %had, %owns
  br i1 %freeold, label %giveback, label %room
giveback:
  call void @chibi_map.give(ptr %ctx, ptr %entries)
  br label %room
room:
  %buffer = phi ptr [ %entries, %insert ], [ %fresh, %grow ], [ %fresh, %giveback ]
  %space = phi i32 [ %cap, %insert ], [ %newcap, %grow ], [ %newcap, %giveback ]
  %coff = zext i32 %count to i64
  %ek = getelementptr inbounds $E, ptr %buffer, i64 %coff, i32 0
  %ev = getelementptr inbounds $E, ptr %buffer, i64 %coff, i32 1
  store $K %key, ptr %ek
  store $V %value, ptr %ev
  %n = add i32 %count, 1
  %big = icmp ugt i32 %n, 8
  br i1 %big, label %indexed, label %out
indexed:
  %has = icmp ne ptr %index, null
  br i1 %has, label %refresh, label %build
refresh:
  %slots = load i32, ptr %index
  %load = mul i32 %n, 4
  %room3 = mul i32 %slots, 3
  %crowded = icmp ugt i32 %load, %room3
  br i1 %crowded, label %widen, label %add
widen:
  %wide = shl i32 %slots, 1
  %wider = call ptr @chibi_map.reindex(ptr %ctx, ptr %index, i32 %wide, i32 -1, i1 %owns)
  br label %add
add:
  %live = phi ptr [ %index, %refresh ], [ %wider, %widen ]
  call void @chibi_map.place(ptr %live, i32 %hash, i32 %n)
  br label %out
build:
  %made = call ptr @chibi_map.reindex(ptr %ctx, ptr null, i32 16, i32 -1, i1 false)
  br label %fill
fill:
  %fi = phi i32 [ 0, %build ], [ %fnext, %fbody ]
  %fend = icmp eq i32 %fi, %n
  br i1 %fend, label %built, label %fbody
fbody:
  %foff = zext i32 %fi to i64
  %fke = getelementptr inbounds $E, ptr %buffer, i64 %foff, i32 0
  %fk = load $K, ptr %fke
  %fh = call i32 @chibi_map.$N.hash($K %fk)
  %fe = add i32 %fi, 1
  call void @chibi_map.place(ptr %made, i32 %fh, i32 %fe)
  %fnext = add i32 %fi, 1
  br label %fill
built:
  br label %out
out:
  %final = phi ptr [ null, %room ], [ %live, %add ], [ %made, %built ]
  %m1 = insertvalue $M %m, i32 %n, 0
  %m2 = insertvalue $M %m1, i32 %space, 1
  %m3 = insertvalue $M %m2, ptr %buffer, 2
  %m4 = insertvalue $M %m3, ptr %final, 3
  ret $M %m4
}
define internal $M @chibi_map.$N.delete(ptr %ctx, $M %m, $K %wanted, i1 %owns) {
entry:
  %count = extractvalue $M %m, 0
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %count, $K %wanted, i32 %hash)
  %present = icmp sge i32 %at, 0
  br i1 %present, label %cut, label %kept
cut:
  %coff = zext i32 %at to i64
  %hole = getelementptr inbounds $E, ptr %entries, i64 %coff
$DELETED
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %after = add i32 %at, 1
  %tail = sub i32 %count, %after
  %noff = zext i32 %after to i64
  %next = getelementptr inbounds $E, ptr %entries, i64 %noff
  %tn = zext i32 %tail to i64
  %tbytes = mul i64 %tn, %width
  call void @chibi_scalar.copy(ptr %hole, ptr %next, i64 %tbytes)
  %n = sub i32 %count, 1
  %big = icmp ugt i32 %n, 8
  br i1 %big, label %reindex, label %unindex
reindex:
  %slots = load i32, ptr %index
  %fresh = call ptr @chibi_map.reindex(ptr %ctx, ptr %index, i32 %slots, i32 %at, i1 %owns)
  br label %done
unindex:
  br i1 %owns, label %giveback, label %done
giveback:
  call void @chibi_map.give(ptr %ctx, ptr %index)
  br label %done
done:
  %final = phi ptr [ %fresh, %reindex ], [ null, %unindex ], [ null, %giveback ]
  %m1 = insertvalue $M %m, i32 %n, 0
  %m2 = insertvalue $M %m1, ptr %final, 3
  ret $M %m2
kept:
  ret $M %m
}
define internal $M @chibi_map.$N.copy(ptr %ctx, $M %m) {
entry:
  %count = extractvalue $M %m, 0
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
  %none = icmp eq i32 %count, 0
  br i1 %none, label %empty, label %clone
empty:
  ret $M zeroinitializer
clone:
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %used = zext i32 %count to i64
  %bytes = mul i64 %used, %width
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  call void @chibi_scalar.copy(ptr %fresh, ptr %entries, i64 %bytes)
$RETAINED
  %indexed = icmp ne ptr %index, null
  br i1 %indexed, label %again, label %done
again:
  %slots = load i32, ptr %index
  %rows = zext i32 %slots to i64
  %extra = add i64 %rows, 1
  %ibytes = shl i64 %extra, 3
  %copy = call ptr @chibi_map.allocate(ptr %ctx, i64 %ibytes)
  call void @chibi_scalar.copy(ptr %copy, ptr %index, i64 %ibytes)
  br label %done
done:
  %final = phi ptr [ null, %rd ], [ %copy, %again ]
  %m1 = insertvalue $M %m, i32 %count, 1
  %m2 = insertvalue $M %m1, ptr %fresh, 2
  %m3 = insertvalue $M %m2, ptr %final, 3
  ret $M %m3
}
define internal void @chibi_map.$N.drop(ptr %ctx, $M %m) {
entry:
  %count = extractvalue $M %m, 0
  %entries = extractvalue $M %m, 2
  %index = extractvalue $M %m, 3
$RELEASED
  call void @chibi_map.give(ptr %ctx, ptr %entries)
  call void @chibi_map.give(ptr %ctx, ptr %index)
  ret void
}
'''
