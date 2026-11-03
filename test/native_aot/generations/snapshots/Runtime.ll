; Real emitted transaction callbacks; Audit.ll owns poison and final balance.
@dynamic.bytes = private constant [3 x i8] c"a\00b"
define internal void @require(i1 %condition) {
  br i1 %condition,label %done,label %bad
bad:
  call void @llvm.trap()
  unreachable
done:
  ret void
}
define internal {ptr,i32,ptr} @dynamic.text() {
  %owner = call ptr @chibi_value_test_allocate(ptr @chibi_context,i64 12)
  store i64 1,ptr %owner
  %data = getelementptr i8,ptr %owner,i64 8
  call void @chibi_scalar.copy(ptr %data,ptr @dynamic.bytes,i64 3)
  %end = getelementptr i8,ptr %data,i64 3
  store i8 0,ptr %end
  %a = insertvalue {ptr,i32,ptr} zeroinitializer,ptr %data,0
  %b = insertvalue {ptr,i32,ptr} %a,i32 3,1
  %c = insertvalue {ptr,i32,ptr} %b,ptr %owner,2
  ret {ptr,i32,ptr} %c
}
define i32 @chibi_value_test_entry(ptr %ctx) {
entry:
  %allocator = getelementptr {ptr,ptr,ptr,ptr,ptr},ptr @chibi_context,i32 0,i32 2
  %releaser = getelementptr {ptr,ptr,ptr,ptr,ptr},ptr @chibi_context,i32 0,i32 4
  store ptr @chibi_value_test_allocate,ptr %allocator
  store ptr @chibi_value_test_release,ptr %releaser
  %inline.offset = ptrtoint ptr getelementptr (%chibi.snapshot.stage,ptr null,i32 0,i32 5) to i64
  %inline.remainder = and i64 %inline.offset,7
  %inline.aligned = icmp eq i64 %inline.remainder,0
  call void @require(i1 %inline.aligned)
  %started = call i32 @chibi_host_call_Frame()
  %original = call {i32,i32,ptr,ptr} @chibi_array.1056.allocate(ptr @chibi_context,i32 2,i32 2)
  %original.data = extractvalue {i32,i32,ptr,ptr} %original,2
  %numbers = call {i32,i32,ptr,ptr} @chibi_array.1030.allocate(ptr @chibi_context,i32 2,i32 2)
  %numbers.data = extractvalue {i32,i32,ptr,ptr} %numbers,2
  store i32 7,ptr %numbers.data
  %numbers.second = getelementptr i32,ptr %numbers.data,i32 1
  store i32 9,ptr %numbers.second
  %text = call {ptr,i32,ptr} @dynamic.text()
  %r1 = insertvalue %record0 zeroinitializer,i32 42,0
  %r2 = insertvalue %record0 %r1,{ptr,i32,ptr} %text,1
  %record = insertvalue %record0 %r2,{i32,i32,ptr,ptr} %numbers,2
  store %record0 %record,ptr %original.data
  %second = getelementptr %record0,ptr %original.data,i32 1
  store %record0 %record,ptr %second
  call void @chibi_value.32.keep_at(ptr @chibi_context,ptr %second)

  %source = alloca {i32,i32,ptr,ptr}
  %number.source = alloca {i32,i32,ptr,ptr}
  %view = alloca %chibi.snapshot.view
  store {i32,i32,ptr,ptr} %original,ptr %source
  store {i32,i32,ptr,ptr} %numbers,ptr %number.source
  %with.value = insertvalue %chibi.snapshot.view zeroinitializer,ptr %source,0
  %with.shape = insertvalue %chibi.snapshot.view %with.value,ptr @chibi_snapshot.shape.0,1
  store %chibi.snapshot.view %with.shape,ptr %view
  %source.word = ptrtoint ptr %view to i64
  %number.word = ptrtoint ptr %number.source to i64
  %tree = call i64 @chibi_host_retained_get(i32 1)
  %texts = call i64 @chibi_host_retained_get(i32 2)
  %inline.source = alloca %record101
  store %record101 zeroinitializer,ptr %inline.source
  store i64 99,ptr %inline.source
  %inline.text.at = getelementptr %record101,ptr %inline.source,i32 0, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 0
  %text.owner = extractvalue {ptr,i32,ptr} %text,2
  call void @chibi_scalar.retain(ptr %text.owner)
  store {ptr,i32,ptr} %text,ptr %inline.text.at
  %inline.view = alloca %chibi.snapshot.view
  %inline.value = insertvalue %chibi.snapshot.view zeroinitializer,ptr %inline.source,0
  %inline.header = insertvalue %chibi.snapshot.view %inline.value,ptr @chibi_snapshot.shape.4,1
  store %chibi.snapshot.view %inline.header,ptr %inline.view
  %inline.word = ptrtoint ptr %inline.view to i64
  %session = call i64 @chibi_host_retained_begin()
  %a0 = call i32 @chibi_host_retained_accept(i64 %session,i32 0,i32 15,i64 %source.word)
  %a1 = call i32 @chibi_host_retained_accept(i64 %session,i32 1,i32 15,i64 %tree)
  %a2 = call i32 @chibi_host_retained_accept(i64 %session,i32 2,i32 15,i64 %texts)
  %a3 = call i32 @chibi_host_retained_accept(i64 %session,i32 3,i32 13,i64 %number.word)
  %a4 = call i32 @chibi_host_retained_accept(i64 %session,i32 4,i32 15,i64 %inline.word)
  %valid4 = icmp eq i32 %a4,1
  call void @require(i1 %valid4)
  %valid0 = icmp eq i32 %a0,1
  %valid1 = icmp eq i32 %a1,1
  %valid2 = icmp eq i32 %a2,1
  %valid3 = icmp eq i32 %a3,1
  call void @require(i1 %valid0)
  call void @require(i1 %valid1)
  call void @require(i1 %valid2)
  call void @require(i1 %valid3)
  %s0 = call i32 @chibi_host_retained_stage(i64 %session,i32 0,i32 15,i64 %source.word)
  %s1 = call i32 @chibi_host_retained_stage(i64 %session,i32 1,i32 15,i64 %tree)
  %s2 = call i32 @chibi_host_retained_stage(i64 %session,i32 2,i32 15,i64 %texts)
  %s3 = call i32 @chibi_host_retained_stage(i64 %session,i32 3,i32 13,i64 %number.word)
  %s4 = call i32 @chibi_host_retained_stage(i64 %session,i32 4,i32 15,i64 %inline.word)
  %staged4 = icmp eq i32 %s4,1
  call void @require(i1 %staged4)
  %staged0 = icmp eq i32 %s0,1
  %staged1 = icmp eq i32 %s1,1
  %staged2 = icmp eq i32 %s2,1
  %staged3 = icmp eq i32 %s3,1
  call void @require(i1 %staged0)
  call void @require(i1 %staged1)
  call void @require(i1 %staged2)
  call void @require(i1 %staged3)
  %before.commit = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %before.length = extractvalue {i32,i32,ptr,ptr} %before.commit,0
  %unpublished = icmp eq i32 %before.length,0
  call void @require(i1 %unpublished)
  %finished = call i32 @chibi_host_retained_finish(i64 %session,i32 1)
  %committed = icmp eq i32 %finished,1
  call void @require(i1 %committed)
  call void @chibi_array.1056.drop(ptr @chibi_context,{i32,i32,ptr,ptr} %original)
  call void @chibi_value.133.drop_at(ptr @chibi_context,ptr %inline.source)
  %snapshot = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %snapshot.data = extractvalue {i32,i32,ptr,ptr} %snapshot,2
  %independent.root = icmp ne ptr %snapshot.data,%original.data
  call void @require(i1 %independent.root)
  %held = load %record0,ptr %snapshot.data
  %held.text = extractvalue %record0 %held,1
  %held.bytes = extractvalue {ptr,i32,ptr} %held.text,0
  %source.bytes = extractvalue {ptr,i32,ptr} %text,0
  %independent.text = icmp ne ptr %held.bytes,%source.bytes
  call void @require(i1 %independent.text)
  %nul.at = getelementptr i8,ptr %held.bytes,i32 1
  %nul = load i8,ptr %nul.at
  %inline.tag = load i64,ptr @chibi_scalar.retained.4
  %inline.tag.valid = icmp eq i64 %inline.tag,99
  call void @require(i1 %inline.tag.valid)
  %inline.retained.text.at = getelementptr %record101,ptr @chibi_scalar.retained.4,i32 0, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 0
  %inline.retained.text = load {ptr,i32,ptr},ptr %inline.retained.text.at
  %inline.retained.bytes = extractvalue {ptr,i32,ptr} %inline.retained.text,0
  %inline.same.text = icmp eq ptr %inline.retained.bytes,%held.bytes
  call void @require(i1 %inline.same.text)
  %valid.nul = icmp eq i8 %nul,0
  call void @require(i1 %valid.nul)
  %held.numbers = extractvalue %record0 %held,2
  %held.data = extractvalue {i32,i32,ptr,ptr} %held.numbers,2
  %published.numbers = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.3
  %published.data = extractvalue {i32,i32,ptr,ptr} %published.numbers,2
  %independent.numbers = icmp ne ptr %published.data,%numbers.data
  call void @require(i1 %independent.numbers)
  %same.backing = icmp eq ptr %held.data,%published.data
  call void @require(i1 %same.backing)
  %seven = load i32,ptr %published.data
  %preserved = icmp eq i32 %seven,7
  call void @require(i1 %preserved)
  %legacy = call i32 @chibi_host_retained_set(i32 0,i64 %source.word)
  %closed.legacy = icmp eq i32 %legacy,0
  call void @require(i1 %closed.legacy)
  %bad.shape = alloca %chibi.snapshot.shape
  %accepted.shape = load %chibi.snapshot.shape,ptr @chibi_snapshot.shape.0
  %wrong.version = insertvalue %chibi.snapshot.shape %accepted.shape,i32 1,0
  store %chibi.snapshot.shape %wrong.version,ptr %bad.shape
  %published.word = call i64 @chibi_host_retained_get(i32 0)
  %published.view = inttoptr i64 %published.word to ptr
  %current.view = load %chibi.snapshot.view,ptr %published.view
  %bad.view = insertvalue %chibi.snapshot.view %current.view,ptr %bad.shape,1
  store %chibi.snapshot.view %bad.view,ptr %view
  %aborted.session = call i64 @chibi_host_retained_begin()
  %staged.prefix = call i32 @chibi_host_retained_stage(i64 %aborted.session,i32 1,i32 15,i64 %tree)
  %rejected = call i32 @chibi_host_retained_accept(i64 %aborted.session,i32 0,i32 15,i64 %source.word)
  %abort.result = call i32 @chibi_host_retained_finish(i64 %aborted.session,i32 1)
  %reject.ok = icmp eq i32 %rejected,0
  %abort.ok = icmp eq i32 %abort.result,0
  call void @require(i1 %reject.ok)
  call void @require(i1 %abort.ok)
  %after.abort = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %after.data = extractvalue {i32,i32,ptr,ptr} %after.abort,2
  %unchanged = icmp eq ptr %after.data,%snapshot.data
  call void @require(i1 %unchanged)
  %duplicate.session = call i64 @chibi_host_retained_begin()
  %duplicate.first = call i32 @chibi_host_retained_stage(i64 %duplicate.session,i32 0,i32 15,i64 %published.word)
  %duplicate.second = call i32 @chibi_host_retained_stage(i64 %duplicate.session,i32 0,i32 15,i64 %published.word)
  %duplicate.finished = call i32 @chibi_host_retained_finish(i64 %duplicate.session,i32 1)
  %first.ok = icmp eq i32 %duplicate.first,1
  %second.rejected = icmp eq i32 %duplicate.second,0
  %duplicate.aborted = icmp eq i32 %duplicate.finished,0
  call void @require(i1 %first.ok)
  call void @require(i1 %second.rejected)
  call void @require(i1 %duplicate.aborted)
  %after.duplicate = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %after.duplicate.data = extractvalue {i32,i32,ptr,ptr} %after.duplicate,2
  %duplicate.unchanged = icmp eq ptr %after.duplicate.data,%snapshot.data
  call void @require(i1 %duplicate.unchanged)
  %released = call i32 @chibi_host_retained_release()
  %released.twice = call i32 @chibi_host_retained_release()
  call void @snapshot.schema.identities()
  call void @snapshot.deep.values()
  ret i32 0
}

; Array recursion uses queue jobs, while sixty-four roots share one clone.
define internal void @snapshot.deep.values() {
entry:
  %memo = alloca %chibi.snapshot.memo
  %queue = alloca %chibi.snapshot.queue
  %session = alloca %chibi.snapshot.values
  %source = alloca {i32,i32,ptr,ptr}
  store %chibi.snapshot.memo zeroinitializer,ptr %memo
  store %chibi.snapshot.queue zeroinitializer,ptr %queue
  %with.memo = insertvalue %chibi.snapshot.values zeroinitializer,ptr %memo,0
  %with.queue = insertvalue %chibi.snapshot.values %with.memo,ptr %queue,1
  store %chibi.snapshot.values %with.queue,ptr %session
  %deep.text = call {ptr,i32,ptr} @dynamic.text()
  br label %build
build:
  %level = phi i32 [0,%entry],[%next.level,%build]
  %children = phi {i32,i32,ptr,ptr} [zeroinitializer,%entry],[%tree,%build]
  %name = phi {ptr,i32,ptr} [%deep.text,%entry],[zeroinitializer,%build]
  %tree = call {i32,i32,ptr,ptr} @chibi_array.1057.allocate(ptr @chibi_context,i32 1,i32 1)
  %tree.data = extractvalue {i32,i32,ptr,ptr} %tree,2
  %named = insertvalue %record1 zeroinitializer,{ptr,i32,ptr} %name,0
  %parent = insertvalue %record1 %named,{i32,i32,ptr,ptr} %children,1
  store %record1 %parent,ptr %tree.data
  %next.level = add i32 %level,1
  %built = icmp eq i32 %next.level,10000
  br i1 %built,label %deep.clone,label %build
deep.clone:
  %roots = alloca [64 x {i32,i32,ptr,ptr}]
  store {i32,i32,ptr,ptr} %tree,ptr %source
  call void @chibi_snapshot.value.1057(ptr @chibi_context,ptr %session,ptr %source,ptr %roots)
  %deep.drained = call i1 @chibi_snapshot.queue.drain(ptr @chibi_context,ptr %queue,ptr %session)
  call void @require(i1 %deep.drained)
  %deep.allocations = load i64,ptr @chibi_value_test_total
  br label %copies
copies:
  %copy = phi i32 [1,%deep.clone],[%next.copy,%copies]
  %target = getelementptr [64 x {i32,i32,ptr,ptr}],ptr %roots,i32 0,i32 %copy
  call void @chibi_snapshot.value.1057(ptr @chibi_context,ptr %session,ptr %source,ptr %target)
  %next.copy = add i32 %copy,1
  %copied.all = icmp eq i32 %next.copy,64
  br i1 %copied.all,label %discard.source,label %copies
discard.source:
  %final.allocations = load i64,ptr @chibi_value_test_total
  %deep.deduplicated = icmp eq i64 %final.allocations,%deep.allocations
  call void @require(i1 %deep.deduplicated)
  call void @chibi_array.1057.drop(ptr @chibi_context,{i32,i32,ptr,ptr} %tree)
  %root = load {i32,i32,ptr,ptr},ptr %roots
  br label %walk
walk:
  %depth = phi i32 [0,%discard.source],[%next.depth,%descend]
  %current = phi {i32,i32,ptr,ptr} [%root,%discard.source],[%child,%descend]
  %current.length = extractvalue {i32,i32,ptr,ptr} %current,0
  %one.child = icmp eq i32 %current.length,1
  call void @require(i1 %one.child)
  %next.depth = add i32 %depth,1
  %bottom = icmp eq i32 %next.depth,10000
  br i1 %bottom,label %drop,label %descend
descend:
  %current.data = extractvalue {i32,i32,ptr,ptr} %current,2
  %child.at = getelementptr %record1,ptr %current.data,i32 0,i32 1
  %child = load {i32,i32,ptr,ptr},ptr %child.at
  br label %walk
drop:
  %drop.index = phi i32 [0,%walk],[%next.drop,%drop]
  %drop.at = getelementptr [64 x {i32,i32,ptr,ptr}],ptr %roots,i32 0,i32 %drop.index
  %drop.value = load {i32,i32,ptr,ptr},ptr %drop.at
  call void @chibi_array.1057.drop(ptr @chibi_context,{i32,i32,ptr,ptr} %drop.value)
  %next.drop = add i32 %drop.index,1
  %dropped = icmp eq i32 %next.drop,64
  br i1 %dropped,label %done,label %drop
done:
  call void @chibi_snapshot.memo.release(ptr @chibi_context,ptr %memo)
  call void @chibi_snapshot.queue.release(ptr @chibi_context,ptr %queue)
  ret void
}

; Structurally identical types must retain their identity across all roots:
; splitting one old type or merging two old types rejects the transaction.
@schema.label = private constant [5 x i8] c"value"
@schema.old.nodes = private constant [3 x %chibi.snapshot.node] [
  %chibi.snapshot.node {i32 2,i32 0,i32 0,i32 1},
  %chibi.snapshot.node {i32 2,i32 0,i32 1,i32 1},
  %chibi.snapshot.node {i32 0,i32 6,i32 0,i32 0}]
@schema.new.nodes = private constant [3 x %chibi.snapshot.node] [
  %chibi.snapshot.node {i32 2,i32 0,i32 0,i32 1},
  %chibi.snapshot.node {i32 2,i32 0,i32 1,i32 1},
  %chibi.snapshot.node {i32 0,i32 6,i32 0,i32 0}]
@schema.edges = private constant [2 x %chibi.snapshot.edge] [
  %chibi.snapshot.edge {ptr @schema.label,i32 5,i32 2},
  %chibi.snapshot.edge {ptr @schema.label,i32 5,i32 2}]
@schema.old.first = private constant %chibi.snapshot.shape {i32 2,i32 0,i32 3,i32 2,ptr @schema.old.nodes,ptr @schema.edges}
@schema.old.second = private constant %chibi.snapshot.shape {i32 2,i32 1,i32 3,i32 2,ptr @schema.old.nodes,ptr @schema.edges}
@schema.new.first = private constant %chibi.snapshot.shape {i32 2,i32 0,i32 3,i32 2,ptr @schema.new.nodes,ptr @schema.edges}
@schema.new.second = private constant %chibi.snapshot.shape {i32 2,i32 1,i32 3,i32 2,ptr @schema.new.nodes,ptr @schema.edges}
define internal void @snapshot.schema.identities() {
  %memo = alloca %chibi.snapshot.memo
  %queue = alloca %chibi.snapshot.queue
  store %chibi.snapshot.memo zeroinitializer,ptr %memo
  store %chibi.snapshot.queue zeroinitializer,ptr %queue
  %first = call i1 @chibi_snapshot.schema.accept(ptr @chibi_context,ptr %memo,ptr %queue,ptr @schema.old.first,ptr @schema.new.first)
  call void @require(i1 %first)
  %split = call i1 @chibi_snapshot.schema.accept(ptr @chibi_context,ptr %memo,ptr %queue,ptr @schema.old.first,ptr @schema.new.second)
  %split.rejected = xor i1 %split,true
  call void @require(i1 %split.rejected)
  call void @chibi_snapshot.memo.release(ptr @chibi_context,ptr %memo)
  call void @chibi_snapshot.queue.release(ptr @chibi_context,ptr %queue)
  %again = call i1 @chibi_snapshot.schema.accept(ptr @chibi_context,ptr %memo,ptr %queue,ptr @schema.old.first,ptr @schema.new.first)
  call void @require(i1 %again)
  %merge = call i1 @chibi_snapshot.schema.accept(ptr @chibi_context,ptr %memo,ptr %queue,ptr @schema.old.second,ptr @schema.new.first)
  %merge.rejected = xor i1 %merge,true
  call void @require(i1 %merge.rejected)
  call void @chibi_snapshot.memo.release(ptr @chibi_context,ptr %memo)
  call void @chibi_snapshot.queue.release(ptr @chibi_context,ptr %queue)
  ret void
}
