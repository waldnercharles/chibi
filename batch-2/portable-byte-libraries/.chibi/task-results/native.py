from pathlib import Path
p=Path('tools/compiler/llvm_native/Emit.chibi');s=p.read_text()
s=s.replace('pub CallSpawned: int : 2;', 'pub CallSpawned: int : 2;\n// Source void has no success payload, despite the ramp\'s internal int result.\npub CallSpawnedVoid: int : 6;')
s=s.replace('if (item.literal == i64(CallSpawned)) {', 'if (item.literal == i64(CallSpawned) || item.literal == i64(CallSpawnedVoid)) {')
s=s.replace('waiting[function] && abi.ScalarType(type) == ""', 'waiting[function] && !TaskFrameValue(records, fields, type, true)')
s=s.replace('''    for (type in locals) {
        if (!Held(records, type))''','''    for (type in locals) {
        if (waiting[function] && !TaskFrameValue(records, fields, type, true)) {
            return Result.Rejected(-1, "native waiting frames cannot own managed values or pointers yet");
        }
        if (!Held(records, type))''',1)
s=s.replace('''    definitions: int[] = [];''','''    if (waiting[function] && (!TaskFrameValue(records, fields, result, false) || result == TaskType)) {
        return Result.Rejected(-1, "native waiting results require plain scalar values or records yet");
    }
    definitions: int[] = [];''',1)
s=s.replace('''(item.type >= RecordType && item.literal != 0)''','''((item.type >= RecordType || item.type == TaskType) && item.literal != 0)''')
s=s.replace('''(item.literal < i64(CallPlain) || item.literal > i64(CallSpawned) ||''','''((item.literal != i64(CallPlain) && item.literal != i64(CallAwaited) &&
                                 item.literal != i64(CallSpawned) && item.literal != i64(CallSpawnedVoid)) ||''')
s=s.replace('''if ((item.op == AwaitHandle || item.op == Cancel || item.op == CancelAfter) && !Typed(slots, item.left, abi.WordStorage))''','''if ((item.op == AwaitHandle || item.op == Cancel || item.op == CancelAfter || item.op == TaskCopy ||
             item.op == TaskDrop || item.op == TaskRunning) && !Typed(slots, item.left, TaskType))''')
s=s.replace('''        if ((item.op == CancelAfter &&''','''        if (((item.op == TaskCopy || item.op == TaskDrop) && item.type != TaskType) ||
            (item.op == TaskRunning && item.type != abi.BoolStorage) ||
            (item.op == AwaitResult && !TaskResultValid(records, fields, slots, item))) {
            return Result.Rejected(base + pc, "native task observer or result differs from its type");
        }
        if ((item.op == CancelAfter &&''',1)
# Save and Publish move owning observer slots.
s=s.replace('''        out = format_text("{}{}", out, Store(format_text("%a{}", pc), item.type, "l", item.target));''','''        out = format_text("{}{}", out, Store(format_text("%a{}", pc), item.type, "l", item.target));
        if (item.type == TaskType) { out = format_text("{}{}", out, Store("0", TaskType, "s", item.left)); }''',1)
s=s.replace('''        out = format_text("{}{}", out, Store(format_text("%a{}", pc), item.type, "result", 0));''','''        out = format_text("{}{}", out, Store(format_text("%a{}", pc), item.type, "result", 0));
        if (item.type == TaskType) { out = format_text("{}{}", out, Store("0", TaskType, "s", item.left)); }''',1)
s=s.replace('''    out = format_text("{}  %handle = call i64 @chibi_tasks.begin(ptr %ctx, i1 true)\\n''','''    out = format_text("{}  %handle = call i64 @chibi_tasks.begin(ptr %ctx, i32 {})\\n''',1)
# Fix exact changed format call's parameter at line end below.
s=s.replace('''ptr @chibi_tasks.current\\n", out);''','''ptr @chibi_tasks.current\\n", out, result);''',1)
s=s.replace('''CoroutineEpilogue :: () -> string {
    out := "coro.final:\\n  %coro.last''','''CoroutineEpilogue :: (final_cleanup: string, cancel_cleanup: string) -> string {
    out := format_text("coro.final:\\n{}", final_cleanup);
    out = format_text("{}  %coro.last''',1)
s=s.replace('''(token none, i1 true)\\n";
    out = format_text''','''(token none, i1 true)\\n", out);
    out = format_text''',1)
s=s.replace('''{}coro.cleanup:\\n  %coro.free =''','''{}coro.cleanup:\\n{}  %coro.free =''',1).replace('''(token %coro.id, ptr %coro.hdl)\\n", out);''','''(token %coro.id, ptr %coro.hdl)\\n", out, cancel_cleanup);''',1)
s=s.replace('''        out = format_text("{}  %parent{} = load i64, ptr @chibi_tasks.current\\n  %task{} = call i64 @chibi_tasks.begin(ptr %ctx, i1 false)\\n", out, pc, pc);''','''        kind := callee.result;
        if (item.literal == i64(CallSpawnedVoid)) { kind = 0; }
        out = format_text("{}  %parent{} = load i64, ptr @chibi_tasks.current\\n  %task{} = call i64 @chibi_tasks.begin(ptr %ctx, i32 {})\\n", out, pc, pc, kind);''',1)
s=s.replace('abi.WordStorage, "s", item.left', 'TaskType, "s", item.left')
s=s.replace('''        out.append(format_text("  %s{} = alloca {}\\n", index, TypeName(slots[shape.first_slot + index])));''','''        out.append(format_text("  %s{} = alloca {}\\n", index, TypeName(slots[shape.first_slot + index])));
        if (slots[shape.first_slot + index] == TaskType) { out.append(Store("0", TaskType, "s", index)); }''',1)
s=s.replace('''} else if (item.op == Call && item.literal == i64(CallSpawned)) {''','''} else if (item.op == Call && (item.literal == i64(CallSpawned) || item.literal == i64(CallSpawnedVoid))) {''')
s=s.replace('''        } else if (item.op == AwaitHandle) {''','''        } else if (item.op == AwaitResult) {
            out.append(TaskResultInstruction(item, pc));
        } else if (item.op == TaskCopy || item.op == TaskDrop || item.op == TaskRunning) {
            out.append(format_text("p{}:\\n", pc));
            out.append(Load(format_text("h{}", pc), TaskType, "s", item.left));
            if (item.op == TaskCopy) {
                out.append(format_text("  call void @chibi_tasks.retain(i64 %h{})\\n", pc));
                out.append(Store(format_text("%h{}", pc), TaskType, "s", item.output));
            } else if (item.op == TaskDrop) {
                out.append(Store("0", TaskType, "s", item.left));
                out.append(format_text("  call void @chibi_tasks.release(i64 %h{})\\n", pc));
            } else {
                out.append(format_text("  %done{} = call i1 @chibi_tasks.finished(i64 %h{})\\n  %running{} = xor i1 %done{}, true\\n", pc, pc, pc, pc));
                out.append(Store(format_text("%running{}", pc), abi.BoolStorage, "s", item.output));
            }
            out.append(format_text("  br label %p{}\\n", pc + 1));
        } else if (item.op == AwaitHandle) {''',1)
s=s.replace('out.append(CoroutineEpilogue());','out.append(CoroutineEpilogue(TaskFrameCleanup(shape, slots, locals, code, "final"),\n                                     TaskFrameCleanup(shape, slots, locals, code, "cancel")));',1)
p.write_text(s)
p=Path('tools/compiler/scalar_llvm/Emit.chibi');s=p.read_text().replace('VariantRow(rows, union_at,', 'VariantOf(rows, union_at,').replace('return type < facts.MapType && Valued(records, type);','return type != facts.TaskType && type < facts.MapType && Valued(records, type);');p.write_text(s)
