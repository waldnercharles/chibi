; Real host ABI fixture. Variants use different native record ordinals.
; The old image's destructor proves migrated owners survive actual dlclose.
@fixture.variant = private constant i32 $VARIANT
@fixture.message = private constant [4 x i8] c"a\00b\00"
@fixture.ready = private constant [6 x i8] c"ready\00"
@fixture.file_mode = private constant [3 x i8] c"wb\00"
@fixture.unloaded = private constant [9 x i8] c"unloaded\00"
@fixture.read = private constant [3 x i8] c"rb\00"
@fixture.marked = internal global i1 false
declare ptr @fopen(ptr,ptr)
declare i32 @fclose(ptr)
define i32 @chibi_host_call_Main() {
  %old = icmp eq i32 $VARIANT,0
  %status = select i1 %old,i32 0,i32 99
  ret i32 %status
}
define i32 @chibi_host_call_Frame() {
entry:
  %initialized = call i32 @chibi_snapshot.original_Frame()
  %root = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %length = extractvalue {i32,i32,ptr,ptr} %root,0
  %empty = icmp eq i32 %length,0
  br i1 %empty,label %seed.check,label %read
seed.check:
  %first.image = icmp eq i32 $VARIANT,0
  br i1 %first.image,label %seed,label %missing.state
missing.state:
  ret i32 89
seed:
  %seed.numbers = call {i32,i32,ptr,ptr} @chibi_array.1030.allocate(ptr @chibi_context,i32 1,i32 1)
  %seed.numbers.data = extractvalue {i32,i32,ptr,ptr} %seed.numbers,2
  store i32 7,ptr %seed.numbers.data
  %seed.numbers.copy = call {i32,i32,ptr,ptr} @chibi_array.1030.copy(ptr @chibi_context,{i32,i32,ptr,ptr} %seed.numbers)
  store {i32,i32,ptr,ptr} %seed.numbers.copy,ptr @chibi_scalar.retained.3
  %seed.records = call {i32,i32,ptr,ptr} @chibi_array.$ARRAY.allocate(ptr @chibi_context,i32 1,i32 1)
  %seed.records.data = extractvalue {i32,i32,ptr,ptr} %seed.records,2
  %seed.numbered = insertvalue $RECORD zeroinitializer,i32 100,0
  %seed.message = insertvalue $RECORD %seed.numbered,{ptr,i32,ptr} {ptr @fixture.message,i32 3,ptr null},1
  %seed.value = insertvalue $RECORD %seed.message,{i32,i32,ptr,ptr} %seed.numbers,2
  store $RECORD %seed.value,ptr %seed.records.data
  store {i32,i32,ptr,ptr} %seed.records,ptr @chibi_scalar.retained.0
  %seed.strings = call {i32,i32,ptr,ptr} @chibi_array.1037.allocate(ptr @chibi_context,i32 1,i32 1)
  %seed.strings.data = extractvalue {i32,i32,ptr,ptr} %seed.strings,2
  store {ptr,i32,ptr} {ptr @fixture.message,i32 3,ptr null},ptr %seed.strings.data
  store {i32,i32,ptr,ptr} %seed.strings,ptr @chibi_scalar.retained.2
  br label %read
read:
  %current = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.0
  %data = extractvalue {i32,i32,ptr,ptr} %current,2
  %number.at = getelementptr $RECORD,ptr %data,i32 0,i32 0
  %number = load i32,ptr %number.at
  switch i32 $VARIANT,label %edited [i32 0,label %old i32 1,label %incompatible]
incompatible:
  ret i32 88
old:
  %next = add i32 %number,1
  store i32 %next,ptr %number.at
  %ready = icmp sge i32 %next,103
  %marked = load i1,ptr @fixture.marked
  %unmarked = xor i1 %marked,true
  %mark = and i1 %ready,%unmarked
  br i1 %mark,label %signal,label %wait
signal:
  %file = call ptr @fopen(ptr @fixture.ready,ptr @fixture.file_mode)
  %opened = icmp ne ptr %file,null
  br i1 %opened,label %close,label %bad
close:
  %closed = call i32 @fclose(ptr %file)
  %ok = icmp eq i32 %closed,0
  store i1 true,ptr @fixture.marked
  br i1 %ok,label %wait,label %bad
wait:
  ret i32 0
bad:
  ret i32 90
edited:
  %unload.file = call ptr @fopen(ptr @fixture.unloaded,ptr @fixture.read)
  %unloaded = icmp ne ptr %unload.file,null
  br i1 %unloaded,label %unloaded.image,label %not.unloaded
not.unloaded:
  ret i32 92
unloaded.image:
  %unload.closed = call i32 @fclose(ptr %unload.file)
  %persisted = icmp sge i32 %number,103
  %message.at = getelementptr $RECORD,ptr %data,i32 0,i32 1
  %message = load {ptr,i32,ptr},ptr %message.at
  %bytes = extractvalue {ptr,i32,ptr} %message,0
  %owner = extractvalue {ptr,i32,ptr} %message,2
  %owned = icmp ne ptr %owner,null
  %first = load i8,ptr %bytes
  %second.at = getelementptr i8,ptr %bytes,i32 1
  %third.at = getelementptr i8,ptr %bytes,i32 2
  %second = load i8,ptr %second.at
  %third = load i8,ptr %third.at
  %a = icmp eq i8 %first,97
  %nul = icmp eq i8 %second,0
  %b = icmp eq i8 %third,98
  %text.first = and i1 %a,%nul
  %text.all = and i1 %text.first,%b
  %text.owned = and i1 %text.all,%owned
  %texts = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.2
  %texts.data = extractvalue {i32,i32,ptr,ptr} %texts,2
  %array.message = load {ptr,i32,ptr},ptr %texts.data
  %array.bytes = extractvalue {ptr,i32,ptr} %array.message,0
  %one.text.copy = icmp eq ptr %bytes,%array.bytes
  %inner.at = getelementptr $RECORD,ptr %data,i32 0,i32 2
  %inner = load {i32,i32,ptr,ptr},ptr %inner.at
  %inner.data = extractvalue {i32,i32,ptr,ptr} %inner,2
  %numbers = load {i32,i32,ptr,ptr},ptr @chibi_scalar.retained.3
  %numbers.data = extractvalue {i32,i32,ptr,ptr} %numbers,2
  %one.array.copy = icmp eq ptr %inner.data,%numbers.data
  %seven = load i32,ptr %numbers.data
  %number.ok = icmp eq i32 %seven,7
  %arrays.ok = and i1 %one.array.copy,%number.ok
  %strings.ok = and i1 %text.owned,%one.text.copy
  %values.ok = and i1 %arrays.ok,%strings.ok
  %valid = and i1 %persisted,%values.ok
  %status = select i1 %valid,i32 42,i32 91
  ret i32 %status
}
define internal void @fixture.unload() {
  %file = call ptr @fopen(ptr @fixture.unloaded,ptr @fixture.file_mode)
  %opened = icmp ne ptr %file,null
  br i1 %opened,label %close,label %done
close:
  %status = call i32 @fclose(ptr %file)
  br label %done
done:
  ret void
}
