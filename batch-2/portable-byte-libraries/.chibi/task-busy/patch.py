from pathlib import Path
p=Path('tools/compiler/llvm_native/Emit.chibi');s=p.read_text()
old='''// Unarmed programs retain their fast loops. Only a source closure that can
// begin a scope needs shared quota calls; the start shim alone cannot arm one.
ScopedProgram :: (code: Instruction[], texts: string[]) -> bool {
    for (item in code) {
        if (item.op != ForeignCall || item.left != 0) { continue; }
        name := texts[int(item.literal)];
        if (name == "chibi_native_scope_begin" || name == "chibi_native_test_begin") { return true; }
    }
    return false;
}'''
new='''RuntimeScopeNone: int : 0;
RuntimeScopeBudget: int : 1;
RuntimeScopeTest: int : 2;
// Unarmed programs retain their fast loops. Test scopes additionally refuse
// task effects; ordinary quota scopes still allow them. One foreign-call scan
// determines both needs from the validated source closure.
ProgramScope :: (code: Instruction[], texts: string[]) -> int {
    found := RuntimeScopeNone;
    for (item in code) {
        if (item.op != ForeignCall || item.left != 0) { continue; }
        name := texts[int(item.literal)];
        if (name == "chibi_native_test_begin") { return RuntimeScopeTest; }
        if (name == "chibi_native_scope_begin") { found = RuntimeScopeBudget; }
    }
    return found;
}'''
assert old in s;s=s.replace(old,new,1)
s=s.replace('''    scoped := ProgramMode(mode) && ScopedProgram(code, texts);''','''    scope := RuntimeScopeNone;
    if (ProgramMode(mode)) { scope = ProgramScope(code, texts); }
    scoped := scope != RuntimeScopeNone;
    testing := scope == RuntimeScopeTest;''',1)
s=s.replace('''SuspendInstruction :: (pc: int) -> string {
    out := format_text("p{}:\\n  %suspend{} = call i8 @llvm.coro.suspend(token none, i1 false)\\n", pc, pc);''','''SuspendInstruction :: (pc: int, testing: bool) -> string {
    out := format_text("p{}:\\n{}  %suspend{} = call i8 @llvm.coro.suspend(token none, i1 false)\\n", pc, TaskTestGuard(testing, 0), pc);''',1)
s=s.replace('''             spawned: bool) -> string {''','''             spawned: bool, testing: bool) -> string {''',1)
s=s.replace('''    if (spawned) {
        kind := callee.result;''','''    if (spawned) {
        out = format_text("{}{}", out, TaskTestGuard(testing, 1));
        kind := callee.result;''',1)
s=s.replace('''                           mode: int) -> string {
    out := RampCall(functions, parameters, arguments, item, pc, mode, false);''','''                           mode: int, testing: bool) -> string {
    out := RampCall(functions, parameters, arguments, item, pc, mode, false, testing);''',1)
# Pending inline wait is distinct from the direct callee's initial execution.
needle='''    out = format_text("{}  %suspend{} = call i8 @llvm.coro.suspend(token none, i1 false)\\n", out, pc);'''
start=s.index('AwaitedCallInstruction ::');pos=s.index(needle,start);s=s[:pos]+'''    out = format_text("{}{}", out, TaskTestGuard(testing, 0));
'''+s[pos:]
s=s.replace('''                           mode: int) -> string {
    out := RampCall(functions, parameters, arguments, item, pc, mode, true);''','''                           mode: int, testing: bool) -> string {
    out := RampCall(functions, parameters, arguments, item, pc, mode, true, testing);''',1)
s=s.replace('''AwaitHandleInstruction :: (item: Instruction, pc: int) -> string {''','''AwaitHandleInstruction :: (item: Instruction, pc: int, testing: bool) -> string {''',1)
needle='''    out = format_text("{}  call void @chibi_tasks.wait(i64 %h{})\\n", out, pc);''';start=s.index('AwaitHandleInstruction ::');pos=s.index(needle,start);s=s[:pos]+'''    out = format_text("{}{}", out, TaskTestGuard(testing, 0));
'''+s[pos:]
s=s.replace('''waiting: bool[], scoped: bool, record_owners: bool[]) -> string {''','''waiting: bool[], scoped: bool, testing: bool, record_owners: bool[]) -> string {''',1)
s=s.replace('AwaitedCallInstruction(functions, parameters, own, item, pc, mode)', 'AwaitedCallInstruction(functions, parameters, own, item, pc, mode, testing)').replace('SpawnedCallInstruction(functions, parameters, own, item, pc, mode)', 'SpawnedCallInstruction(functions, parameters, own, item, pc, mode, testing)').replace('SuspendInstruction(pc)', 'SuspendInstruction(pc, testing)').replace('TaskResultInstruction(item, pc)', 'TaskResultInstruction(item, pc, testing)').replace('AwaitHandleInstruction(item, pc)', 'AwaitHandleInstruction(item, pc, testing)').replace('DelayInstruction(item, pc)', 'DelayInstruction(item, pc, testing)')
s=s.replace('''texts, mode, step_budget, runtime, fallible, shared[function], waiting, scoped, record_owners);''','''texts, mode, step_budget, runtime, fallible, shared[function], waiting, scoped, testing, record_owners);''',1)
s=s.replace('''        if (Tasks(held)) {
            out.append(CoroutineDeclarations());
        }''','''        if (Tasks(held)) {
            out.append(CoroutineDeclarations());
            if (testing) { out.append("declare void @chibi_native_test_busy(i32)\\n"); }
        }''',1)
p.write_text(s)
p=Path('tools/compiler/llvm_native/TaskResults.chibi');s=p.read_text().replace('''TaskResultInstruction :: (item: Instruction, pc: int) -> string {''','''TaskResultInstruction :: (item: Instruction, pc: int, testing: bool) -> string {''',1);needle='''    out = format_text("{}  call void @chibi_tasks.wait(i64 %h{})\\n", out, pc);''';assert needle in s;s=s.replace(needle,'''    out = format_text("{}{}", out, TaskTestGuard(testing, 0));
'''+needle,1)
s+='''
// Only closures that can arm a generated test scope carry these calls. The
// runtime returns while unarmed and stops the test before an actual wait or
// spawn. It uses an unclassified failure, independent of ExpectFailure.
TaskTestGuard :: (testing: bool, kind: int) -> string {
    if (!testing) { return ""; }
    return format_text("  call void @chibi_native_test_busy(i32 {})\\n", kind);
}
''';p.write_text(s)
p=Path('tools/compiler/llvm_native/TaskTiming.chibi');s=p.read_text().replace('''DelayInstruction :: (item: Instruction, pc: int) -> string {''','''DelayInstruction :: (item: Instruction, pc: int, testing: bool) -> string {''',1);needle='''    out = format_text("{}  store double %deadline{}, ptr @chibi_tasks.pending_time\\n", out, pc);''';assert needle in s;s=s.replace(needle,'''    out = format_text("{}{}", out, TaskTestGuard(testing, 0));
'''+needle,1);p.write_text(s)
