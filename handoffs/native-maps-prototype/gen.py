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
define internal void @chibi_map.place(ptr %index, i32 %slots, i32 %hash, i32 %entry1) {
entry:
  %mask = sub i32 %slots, 1
  %start = and i32 %hash, %mask
  br label %walk
walk:
  %pos = phi i32 [ %start, %entry ], [ %pnext, %cont ]
  %dist = phi i32 [ 0, %entry ], [ %dnext, %cont ]
  %h = phi i32 [ %hash, %entry ], [ %hn, %cont ]
  %e = phi i32 [ %entry1, %entry ], [ %en, %cont ]
  %poff = zext i32 %pos to i64
  %sp = getelementptr inbounds { i32, i32 }, ptr %index, i64 %poff
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
define internal i32 @chibi_map.slots(i32 %capacity) {
entry:
  %cap = zext i32 %capacity to i64
  %want = shl i64 %cap, 2
  br label %head
head:
  %s = phi i64 [ 1, %entry ], [ %double, %body ]
  %have = mul i64 %s, 3
  %enough = icmp uge i64 %have, %want
  br i1 %enough, label %done, label %body
body:
  %double = shl i64 %s, 1
  br label %head
done:
  %slots = trunc i64 %s to i32
  ret i32 %slots
}
TYPED = r'''
define internal i32 @chibi_map.$N.hash($K %key) {
entry:
$PREHASH
  %raw = call i32 @chibi_map.mix(i64 %wide)
  %zero = icmp eq i32 %raw, 0
  %hash = select i1 %zero, i32 1, i32 %raw
  ret i32 %hash
}
define internal i1 @chibi_map.$N.same($K %a, $K %b) {
entry:
$SAME
  ret i1 %same
}
define internal i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %used, i32 %slots, $K %wanted, i32 %hash) {
entry:
  %indexed = icmp ne ptr %index, null
  br i1 %indexed, label %probe, label %scan
scan:
  %at = phi i32 [ 0, %entry ], [ %next, %step ]
  %end = icmp eq i32 %at, %used
  br i1 %end, label %absent, label %look
look:
  %off = zext i32 %at to i64
  %he = getelementptr inbounds $E, ptr %entries, i64 %off, i32 2
  %eh = load i32, ptr %he
  %alike = icmp eq i32 %eh, %hash
  br i1 %alike, label %test, label %step
test:
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
  %mask = sub i32 %slots, 1
  %start = and i32 %hash, %mask
  br label %walk
walk:
  %pos = phi i32 [ %start, %probe ], [ %pnext, %skip ]
  %dist = phi i32 [ 0, %probe ], [ %dnext, %skip ]
  %poff = zext i32 %pos to i64
  %sp = getelementptr inbounds { i32, i32 }, ptr %index, i64 %poff
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
  %salike = icmp eq i32 %sh, %hash
  br i1 %salike, label %live, label %skip
live:
  %idx = sub i32 %se, 1
  %ioff = zext i32 %idx to i64
  %lhe = getelementptr inbounds $E, ptr %entries, i64 %ioff, i32 2
  %leh = load i32, ptr %lhe
  %lalive = icmp eq i32 %leh, %hash
  br i1 %lalive, label %test2, label %skip
test2:
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
  %used = extractvalue $M %m, 1
  %slots = extractvalue $M %m, 3
  %entries = extractvalue $M %m, 4
  %index = extractvalue $M %m, 5
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %used, i32 %slots, $K %wanted, i32 %hash)
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
  %used = extractvalue $M %m, 1
  %slots = extractvalue $M %m, 3
  %entries = extractvalue $M %m, 4
  %index = extractvalue $M %m, 5
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %used, i32 %slots, $K %wanted, i32 %hash)
  %present = icmp sge i32 %at, 0
  ret i1 %present
}
define internal ptr @chibi_map.$N.index(ptr %ctx, ptr %entries, i32 %used, i32 %slots) {
entry:
  %count = zext i32 %slots to i64
  %bytes = shl i64 %count, 3
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  br label %head
head:
  %i = phi i32 [ 0, %entry ], [ %next, %step ]
  %end = icmp eq i32 %i, %used
  br i1 %end, label %done, label %look
look:
  %off = zext i32 %i to i64
  %he = getelementptr inbounds $E, ptr %entries, i64 %off, i32 2
  %h = load i32, ptr %he
  %alive = icmp ne i32 %h, 0
  br i1 %alive, label %put, label %step
put:
  %e1 = add i32 %i, 1
  call void @chibi_map.place(ptr %fresh, i32 %slots, i32 %h, i32 %e1)
  br label %step
step:
  %next = add i32 %i, 1
  br label %head
done:
  ret ptr %fresh
}
define internal $M @chibi_map.$N.set(ptr %ctx, $M %m, $K %key, $V %value, i1 %owns) {
entry:
  %count = extractvalue $M %m, 0
  %used = extractvalue $M %m, 1
  %cap = extractvalue $M %m, 2
  %slots = extractvalue $M %m, 3
  %entries = extractvalue $M %m, 4
  %index = extractvalue $M %m, 5
  %hash = call i32 @chibi_map.$N.hash($K %key)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %used, i32 %slots, $K %key, i32 %hash)
  %present = icmp sge i32 %at, 0
  br i1 %present, label %replace, label %insert
replace:
  %roff = zext i32 %at to i64
  %rv = getelementptr inbounds $E, ptr %entries, i64 %roff, i32 1
$REPLACED
  store $V %value, ptr %rv
  ret $M %m
insert:
  %full = icmp eq i32 %used, %cap
  br i1 %full, label %grow, label %room
grow:
  %dbl = shl i32 %count, 1
  %tiny = icmp ult i32 %dbl, 4
  %newcap = select i1 %tiny, i32 4, i32 %dbl
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %cn = zext i32 %newcap to i64
  %bytes = mul i64 %cn, %width
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  br label %ghead
ghead:
  %r = phi i32 [ 0, %grow ], [ %rnext, %gstep ]
  %w = phi i32 [ 0, %grow ], [ %wnext, %gstep ]
  %rend = icmp eq i32 %r, %used
  br i1 %rend, label %grown, label %glook
glook:
  %roff2 = zext i32 %r to i64
  %rhe = getelementptr inbounds $E, ptr %entries, i64 %roff2, i32 2
  %reh = load i32, ptr %rhe
  %ralive = icmp ne i32 %reh, 0
  br i1 %ralive, label %gmove, label %gstep
gmove:
  %src = getelementptr inbounds $E, ptr %entries, i64 %roff2
  %pair = load $E, ptr %src
  %woff = zext i32 %w to i64
  %dst = getelementptr inbounds $E, ptr %fresh, i64 %woff
  store $E %pair, ptr %dst
  %wmoved = add i32 %w, 1
  br label %gstep
gstep:
  %wnext = phi i32 [ %w, %glook ], [ %wmoved, %gmove ]
  %rnext = add i32 %r, 1
  br label %ghead
grown:
  %had = icmp ne ptr %entries, null
  %freeold = and i1 %had, %owns
  br i1 %freeold, label %giveback, label %regrown
giveback:
  call void @chibi_map.give(ptr %ctx, ptr %entries)
  br label %regrown
regrown:
  %hasindex = icmp ne ptr %index, null
  %freeindex = and i1 %hasindex, %owns
  br i1 %freeindex, label %giveindex, label %room
giveindex:
  call void @chibi_map.give(ptr %ctx, ptr %index)
  br label %room
room:
  %buffer = phi ptr [ %entries, %insert ], [ %fresh, %regrown ], [ %fresh, %giveindex ]
  %space = phi i32 [ %cap, %insert ], [ %newcap, %regrown ], [ %newcap, %giveindex ]
  %used1 = phi i32 [ %used, %insert ], [ %w, %regrown ], [ %w, %giveindex ]
  %slots1 = phi i32 [ %slots, %insert ], [ 0, %regrown ], [ 0, %giveindex ]
  %index1 = phi ptr [ %index, %insert ], [ null, %regrown ], [ null, %giveindex ]
  %coff = zext i32 %used1 to i64
  %ek = getelementptr inbounds $E, ptr %buffer, i64 %coff, i32 0
  %ev = getelementptr inbounds $E, ptr %buffer, i64 %coff, i32 1
  %eh2 = getelementptr inbounds $E, ptr %buffer, i64 %coff, i32 2
  store $K %key, ptr %ek
  store $V %value, ptr %ev
  store i32 %hash, ptr %eh2
  %used2 = add i32 %used1, 1
  %count2 = add i32 %count, 1
  %big = icmp ugt i32 %used2, 8
  br i1 %big, label %indexed, label %out
indexed:
  %has = icmp ne ptr %index1, null
  br i1 %has, label %add, label %build
add:
  call void @chibi_map.place(ptr %index1, i32 %slots1, i32 %hash, i32 %used2)
  br label %out
build:
  %slots2 = call i32 @chibi_map.slots(i32 %space)
  %made = call ptr @chibi_map.$N.index(ptr %ctx, ptr %buffer, i32 %used2, i32 %slots2)
  br label %out
out:
  %final = phi ptr [ null, %room ], [ %index1, %add ], [ %made, %build ]
  %fslots = phi i32 [ 0, %room ], [ %slots1, %add ], [ %slots2, %build ]
  %m1 = insertvalue $M %m, i32 %count2, 0
  %m2 = insertvalue $M %m1, i32 %used2, 1
  %m3 = insertvalue $M %m2, i32 %space, 2
  %m4 = insertvalue $M %m3, i32 %fslots, 3
  %m5 = insertvalue $M %m4, ptr %buffer, 4
  %m6 = insertvalue $M %m5, ptr %final, 5
  ret $M %m6
}
define internal $M @chibi_map.$N.delete(ptr %ctx, $M %m, $K %wanted, i1 %owns) {
entry:
  %count = extractvalue $M %m, 0
  %used = extractvalue $M %m, 1
  %slots = extractvalue $M %m, 3
  %entries = extractvalue $M %m, 4
  %index = extractvalue $M %m, 5
  %hash = call i32 @chibi_map.$N.hash($K %wanted)
  %at = call i32 @chibi_map.$N.find(ptr %entries, ptr %index, i32 %used, i32 %slots, $K %wanted, i32 %hash)
  %present = icmp sge i32 %at, 0
  br i1 %present, label %cut, label %kept
cut:
  %coff = zext i32 %at to i64
  %hole = getelementptr inbounds $E, ptr %entries, i64 %coff
$DELETED
  %n = sub i32 %count, 1
  %scanned = icmp eq ptr %index, null
  br i1 %scanned, label %shift, label %mark
shift:
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %after = add i32 %at, 1
  %tail = sub i32 %used, %after
  %noff = zext i32 %after to i64
  %next = getelementptr inbounds $E, ptr %entries, i64 %noff
  %tn = zext i32 %tail to i64
  %tbytes = mul i64 %tn, %width
  call void @chibi_scalar.copy(ptr %hole, ptr %next, i64 %tbytes)
  %shorter = sub i32 %used, 1
  %s1 = insertvalue $M %m, i32 %n, 0
  %s2 = insertvalue $M %s1, i32 %shorter, 1
  ret $M %s2
mark:
  %hh = getelementptr inbounds $E, ptr %hole, i32 0, i32 2
  store i32 0, ptr %hh
  %dead = sub i32 %used, %n
  %dead2 = shl i32 %dead, 1
  %crowded = icmp ugt i32 %dead2, %n
  br i1 %crowded, label %compact, label %marked
marked:
  %k1 = insertvalue $M %m, i32 %n, 0
  ret $M %k1
compact:
  br label %chead
chead:
  %r = phi i32 [ 0, %compact ], [ %rnext, %cstep ]
  %w = phi i32 [ 0, %compact ], [ %wnext, %cstep ]
  %rend = icmp eq i32 %r, %used
  br i1 %rend, label %packed, label %clook
clook:
  %roff = zext i32 %r to i64
  %rhe = getelementptr inbounds $E, ptr %entries, i64 %roff, i32 2
  %reh = load i32, ptr %rhe
  %ralive = icmp ne i32 %reh, 0
  br i1 %ralive, label %cmove, label %cstep
cmove:
  %src = getelementptr inbounds $E, ptr %entries, i64 %roff
  %pair = load $E, ptr %src
  %woff = zext i32 %w to i64
  %dst = getelementptr inbounds $E, ptr %entries, i64 %woff
  store $E %pair, ptr %dst
  %wmoved = add i32 %w, 1
  br label %cstep
cstep:
  %wnext = phi i32 [ %w, %clook ], [ %wmoved, %cmove ]
  %rnext = add i32 %r, 1
  br label %chead
packed:
  %still = icmp ugt i32 %w, 8
  br i1 %still, label %refill, label %unindex
refill:
  %sbytes0 = zext i32 %slots to i64
  %sbytes = shl i64 %sbytes0, 3
  call void @chibi_scalar.zero(ptr %index, i64 %sbytes)
  br label %fhead
fhead:
  %fi = phi i32 [ 0, %refill ], [ %fnext, %fstep ]
  %fend = icmp eq i32 %fi, %w
  br i1 %fend, label %refilled, label %fput
fput:
  %foff = zext i32 %fi to i64
  %fhe = getelementptr inbounds $E, ptr %entries, i64 %foff, i32 2
  %fh = load i32, ptr %fhe
  %fe = add i32 %fi, 1
  call void @chibi_map.place(ptr %index, i32 %slots, i32 %fh, i32 %fe)
  br label %fstep
fstep:
  %fnext = add i32 %fi, 1
  br label %fhead
refilled:
  %c1 = insertvalue $M %m, i32 %n, 0
  %c2 = insertvalue $M %c1, i32 %w, 1
  ret $M %c2
unindex:
  br i1 %owns, label %giveback, label %dropped
giveback:
  call void @chibi_map.give(ptr %ctx, ptr %index)
  br label %dropped
dropped:
  %u1 = insertvalue $M %m, i32 %n, 0
  %u2 = insertvalue $M %u1, i32 %w, 1
  %u3 = insertvalue $M %u2, i32 0, 3
  %u4 = insertvalue $M %u3, ptr null, 5
  ret $M %u4
kept:
  ret $M %m
}
define internal $M @chibi_map.$N.copy(ptr %ctx, $M %m) {
entry:
  %count = extractvalue $M %m, 0
  %used = extractvalue $M %m, 1
  %entries = extractvalue $M %m, 4
  %none = icmp eq i32 %count, 0
  br i1 %none, label %empty, label %clone
empty:
  ret $M zeroinitializer
clone:
  %wat = getelementptr $E, ptr null, i32 1
  %width = ptrtoint ptr %wat to i64
  %used64 = zext i32 %count to i64
  %bytes = mul i64 %used64, %width
  %fresh = call ptr @chibi_map.allocate(ptr %ctx, i64 %bytes)
  br label %yhead
yhead:
  %r = phi i32 [ 0, %clone ], [ %rnext, %ystep ]
  %w = phi i32 [ 0, %clone ], [ %wnext, %ystep ]
  %rend = icmp eq i32 %r, %used
  br i1 %rend, label %copied, label %ylook
ylook:
  %roff = zext i32 %r to i64
  %rhe = getelementptr inbounds $E, ptr %entries, i64 %roff, i32 2
  %reh = load i32, ptr %rhe
  %ralive = icmp ne i32 %reh, 0
  br i1 %ralive, label %ymove, label %ystep
ymove:
  %src = getelementptr inbounds $E, ptr %entries, i64 %roff
  %pair = load $E, ptr %src
  %woff = zext i32 %w to i64
  %dst = getelementptr inbounds $E, ptr %fresh, i64 %woff
  store $E %pair, ptr %dst
$RETAINED
  %wmoved = add i32 %w, 1
  br label %ystep
ystep:
  %wnext = phi i32 [ %w, %ylook ], [ %wmoved, %yretained ]
  %rnext = add i32 %r, 1
  br label %yhead
copied:
  %big = icmp ugt i32 %count, 8
  br i1 %big, label %indexed, label %done
indexed:
  %slots = call i32 @chibi_map.slots(i32 %count)
  %made = call ptr @chibi_map.$N.index(ptr %ctx, ptr %fresh, i32 %count, i32 %slots)
  br label %done
done:
  %final = phi ptr [ null, %copied ], [ %made, %indexed ]
  %fslots = phi i32 [ 0, %copied ], [ %slots, %indexed ]
  %m1 = insertvalue $M zeroinitializer, i32 %count, 0
  %m2 = insertvalue $M %m1, i32 %count, 1
  %m3 = insertvalue $M %m2, i32 %count, 2
  %m4 = insertvalue $M %m3, i32 %fslots, 3
  %m5 = insertvalue $M %m4, ptr %fresh, 4
  %m6 = insertvalue $M %m5, ptr %final, 5
  ret $M %m6
}
define internal void @chibi_map.$N.drop(ptr %ctx, $M %m) {
entry:
  %used = extractvalue $M %m, 1
  %entries = extractvalue $M %m, 4
  %index = extractvalue $M %m, 5
  br label %xhead
xhead:
  %xi = phi i32 [ 0, %entry ], [ %xnext, %xstep ]
  %xend = icmp eq i32 %xi, %used
  br i1 %xend, label %xd, label %xlook
xlook:
  %xoff = zext i32 %xi to i64
  %xhe = getelementptr inbounds $E, ptr %entries, i64 %xoff, i32 2
  %xeh = load i32, ptr %xhe
  %xalive = icmp ne i32 %xeh, 0
  br i1 %xalive, label %xbody, label %xstep
xbody:
$RELEASED
  br label %xstep
xstep:
  %xnext = add i32 %xi, 1
  br label %xhead
xd:
  call void @chibi_map.give(ptr %ctx, ptr %entries)
  call void @chibi_map.give(ptr %ctx, ptr %index)
  ret void
}
'''
