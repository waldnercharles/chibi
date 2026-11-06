import json,re,subprocess
from pathlib import Path
p=Path('tools/compiler/llvm_native/Tasks.chibi')
src=subprocess.check_output(['git','show','6a191af4e:tools/compiler/llvm_native/Tasks.chibi'],text=True)
body=src.split('return strings.Join([',1)[1].rsplit('], "");',1)[0]
parts=[]
for m in re.finditer(r'"(?:[^"\\]|\\.)*"|\b(fields|sample|started|tick|attached|runnable|reset_wait|updated_wait)\b',body):
    parts.append(json.loads(m.group()) if m.group().startswith('"') else '$'+m.group())
old=''.join(parts)
old=re.sub(r'(getelementptr inbounds %chibi.task, ptr [^\n]+, i32 0, i32 )(\d+)',lambda m:m[1]+str(int(m[2])+1 if int(m[2])>=7 else int(m[2])),old)
functions={}
for m in re.finditer(r'(?m)^define [^\n]*@chibi_tasks\.(\w+)\(',old):
    end=old.index('\n}\n',m.start())+3
    functions[m[1]]=old[m.start():end]+'\n'
row=functions['row'].replace('  br i1 %same, label %found, label %none','  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7\n  %state = load i32, ptr %state.at\n  %live = icmp ne i32 %state, 0\n  %matches = and i1 %same, %live\n  br i1 %matches, label %found, label %none')
begin=functions['begin'].replace('i1 %keep','i32 %kind')
begin=begin.replace('  %keep.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 6\n  store i32 %kind, ptr %keep.at','  %kind.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 4\n  store i32 %kind, ptr %kind.at\n  %owners.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 6\n  store i32 1, ptr %owners.at\n  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7\n  store i32 1, ptr %state.at')
unlink=functions['dispose'].replace('@chibi_tasks.dispose(', '@chibi_tasks.unlink(')
unlink=unlink[:unlink.index('release:\n')]+ 'release:\n  br label %exit\nexit:\n  ret void\n}\n'
queue=functions['attach'][functions['attach'].index('queue:\n'):]
attach='''define internal void @chibi_tasks.attach(i64 %handle, ptr %frame) #0 {
entry:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  store ptr %frame, ptr %row
  %wake.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 1
  %pending = load i64, ptr @chibi_tasks.pending
  store i64 %pending, ptr %wake.at
  store i64 0, ptr @chibi_tasks.pending
$attached  %done = call i1 @llvm.coro.done(ptr %frame)
  br i1 %done, label %complete, label %queue
complete:
  call void @chibi_tasks.complete(i64 %handle, i1 false)
  ret void
'''+queue
compact=functions['update'][functions['update'].index('compact.start:\n'):]
compact=compact.replace('  %candidate.frame = load ptr, ptr %candidate.row\n  %live = icmp ne ptr %candidate.frame, null','  %candidate.state.at = getelementptr inbounds %chibi.task, ptr %candidate.row, i32 0, i32 7\n  %candidate.state = load i32, ptr %candidate.state.at\n  %live = icmp eq i32 %candidate.state, 1')
update='''define internal void @chibi_tasks.update() #0 {
entry:
$tick  %count = load i64, ptr @chibi_tasks.ready_count
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %next ]
  %more = icmp ult i64 %i, %count
  br i1 %more, label %task, label %compact.start
task:
  %ready = load ptr, ptr @chibi_tasks.ready
  %at = getelementptr inbounds i64, ptr %ready, i64 %i
  %handle = load i64, ptr %at
  %done = call i1 @chibi_tasks.finished(i64 %handle)
  br i1 %done, label %next, label %waiting
waiting:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %wake.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 1
  %wake = load i64, ptr %wake.at
  %over = call i1 @chibi_tasks.finished(i64 %wake)
$runnable run:
  %frame = load ptr, ptr %row
  %previous = load i64, ptr @chibi_tasks.current
  store i64 %handle, ptr @chibi_tasks.current
  store i64 0, ptr @chibi_tasks.pending
$reset_wait  call void @llvm.coro.resume(ptr %frame)
  store i64 %previous, ptr @chibi_tasks.current
  %updated = call ptr @chibi_tasks.row(i64 %handle)
  %updated.wake = getelementptr inbounds %chibi.task, ptr %updated, i32 0, i32 1
  store i64 0, ptr %updated.wake
  call void @chibi_tasks.release(i64 %wake)
  %pending = load i64, ptr @chibi_tasks.pending
  store i64 %pending, ptr %updated.wake
  store i64 0, ptr @chibi_tasks.pending
$updated_wait  %finished = call i1 @llvm.coro.done(ptr %frame)
  br i1 %finished, label %complete, label %next
complete:
  call void @chibi_tasks.complete(i64 %handle, i1 false)
  br label %next
next:
  %i.next = add i64 %i, 1
  br label %loop
'''+compact
core='''define internal ptr @chibi_tasks.result_frame(i64 %handle, i32 %kind) #0 {
entry:
  %empty = icmp eq i64 %handle, 0
  br i1 %empty, label %cancelled, label %lookup
lookup:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %valid = icmp ne ptr %row, null
  br i1 %valid, label %check, label %trap
check:
  %kind.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 4
  %actual = load i32, ptr %kind.at
  %same = icmp eq i32 %actual, %kind
  br i1 %same, label %state, label %trap
state:
  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7
  %phase = load i32, ptr %state.at
  switch i32 %phase, label %trap [ i32 2, label %success
                                  i32 3, label %cancelled ]
success:
  %frame = load ptr, ptr %row
  ret ptr %frame
cancelled:
  ret ptr null
trap:
  call void @llvm.trap()
  unreachable
}
define internal void @chibi_tasks.reclaim(i64 %handle) #0 {
entry:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %frame = load ptr, ptr %row
  %has = icmp ne ptr %frame, null
  br i1 %has, label %destroy, label %free
destroy:
  call void @llvm.coro.destroy(ptr %frame)
  br label %free
free:
  store ptr null, ptr %row
  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7
  store i32 0, ptr %state.at
  %head = load i32, ptr @chibi_tasks.free
  %next.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 4
  store i32 %head, ptr %next.at
  %index = trunc i64 %handle to i32
  store i32 %index, ptr @chibi_tasks.free
  ret void
}
define internal void @chibi_tasks.retain(i64 %handle) #0 {
entry:
  %empty = icmp eq i64 %handle, 0
  br i1 %empty, label %exit, label %lookup
lookup:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %valid = icmp ne ptr %row, null
  br i1 %valid, label %count, label %trap
count:
  %owners.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 6
  %owners = load i32, ptr %owners.at
  %room = icmp ult i32 %owners, -1
  br i1 %room, label %keep, label %trap
keep:
  %next = add i32 %owners, 1
  store i32 %next, ptr %owners.at
  br label %exit
trap:
  call void @llvm.trap()
  unreachable
exit:
  ret void
}
define internal void @chibi_tasks.release(i64 %handle) #0 {
entry:
  %empty = icmp eq i64 %handle, 0
  %stopping = load i1, ptr @chibi_tasks.stopping
  %skip = or i1 %empty, %stopping
  br i1 %skip, label %exit, label %lookup
lookup:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %valid = icmp ne ptr %row, null
  br i1 %valid, label %count, label %trap
count:
  %owners.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 6
  %owners = load i32, ptr %owners.at
  %has = icmp ne i32 %owners, 0
  br i1 %has, label %drop, label %trap
drop:
  %next = sub i32 %owners, 1
  store i32 %next, ptr %owners.at
  %none = icmp eq i32 %next, 0
  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7
  %state = load i32, ptr %state.at
  %done = icmp ne i32 %state, 1
  %free = and i1 %none, %done
  br i1 %free, label %reclaim, label %exit
reclaim:
  call void @chibi_tasks.reclaim(i64 %handle)
  br label %exit
trap:
  call void @llvm.trap()
  unreachable
exit:
  ret void
}
define internal i1 @chibi_tasks.finished(i64 %handle) #0 {
entry:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  %valid = icmp ne ptr %row, null
  br i1 %valid, label %check, label %yes
check:
  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7
  %state = load i32, ptr %state.at
  %done = icmp ne i32 %state, 1
  ret i1 %done
yes:
  ret i1 true
}
define internal void @chibi_tasks.complete(i64 %handle, i1 %cancelled) #0 {
entry:
  %row = call ptr @chibi_tasks.row(i64 %handle)
  call void @chibi_tasks.unlink(i64 %handle)
  %state.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 7
  %state = select i1 %cancelled, i32 3, i32 2
  %wake.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 1
  %wake = load i64, ptr %wake.at
  store i64 0, ptr %wake.at
  call void @chibi_tasks.release(i64 %wake)
  br i1 %cancelled, label %destroy, label %check
destroy:
  %frame = load ptr, ptr %row
  store ptr null, ptr %row
  call void @llvm.coro.destroy(ptr %frame)
  br label %check
check:
  store i32 %state, ptr %state.at
  %owners.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 6
  %owners = load i32, ptr %owners.at
  %none = icmp eq i32 %owners, 0
  br i1 %none, label %reclaim, label %exit
reclaim:
  call void @chibi_tasks.reclaim(i64 %handle)
  br label %exit
exit:
  ret void
}
define internal void @chibi_tasks.wait(i64 %awaited) #0 {
entry:
  %current = load i64, ptr @chibi_tasks.current
  %self = icmp eq i64 %current, %awaited
  br i1 %self, label %trap, label %keep
keep:
  call void @chibi_tasks.retain(i64 %awaited)
  store i64 %awaited, ptr @chibi_tasks.pending
  ret void
trap:
  call void @llvm.trap()
  unreachable
}
'''
cancel=functions['cancel'].replace('call void @chibi_tasks.dispose(i64 %current)','call void @chibi_tasks.complete(i64 %current, i1 true)')
public=old[old.index('define i64 @chibi_host_tasks_pending()'):old.index('define i32 @chibi_host_tasks_cancel_all()')]
shutdown='''define i32 @chibi_host_tasks_cancel_all() #0 {
entry:
  store i1 true, ptr @chibi_tasks.stopping
  %count = load i32, ptr @chibi_tasks.capacity
  %rows = load ptr, ptr @chibi_tasks.table
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %advance ]
  %more = icmp ult i32 %i, %count
  br i1 %more, label %read, label %table
read:
  %wide = zext i32 %i to i64
  %row = getelementptr inbounds %chibi.task, ptr %rows, i64 %wide
  %frame = load ptr, ptr %row
  %has = icmp ne ptr %frame, null
  br i1 %has, label %destroy, label %advance
destroy:
  store ptr null, ptr %row
  call void @llvm.coro.destroy(ptr %frame)
  br label %advance
advance:
  %next = add i32 %i, 1
  br label %loop
'''
oldshutdown=old[old.index('define i32 @chibi_host_tasks_cancel_all()'):]
shutdown+=oldshutdown[oldshutdown.index('table:\n'):]
shutdown=shutdown.replace('  %rows = load ptr, ptr @chibi_tasks.table\n  %has.rows','  %has.rows')
shutdown=shutdown.replace('ready.check:\n  %has.ready','ready.check:\n  %ready = load ptr, ptr @chibi_tasks.ready\n  %has.ready')
shutdown=shutdown.replace('  ret i32 0','  store i1 false, ptr @chibi_tasks.stopping\n  ret i32 0')
globals=old[:old.index('define internal ptr')]
globals=globals.replace('%chibi.task = type { ptr, i64, i64, i32, i32, ptr, i1, i64, i64, i64$fields','%chibi.task = type { ptr, i64, i64, i32, i32, ptr, i32, i32, i64, i64, i64$fields')
globals+='@chibi_tasks.stopping = internal global i1 false\n'
text=globals+row+functions['grow']+begin+unlink+core+attach+cancel+update+public+shutdown
Path('.chibi/task-results/scheduler.ll.template').write_text(text)
variables={
'fields':', double, double, double',
'sample':'  call void @chibi_tasks.sample_time()\n',
'started':'  %started.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 11\n  %now = load double, ptr @chibi_tasks.now\n  store double %now, ptr %started.at\n  %deadline.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 12\n  store double 0x7FF0000000000000, ptr %deadline.at\n',
'tick':'  call void @chibi_tasks.sample_time()\n  call void @chibi_tasks.expire()\n',
'attached':'  %time.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 13\n  %time = load double, ptr @chibi_tasks.pending_time\n  store double %time, ptr %time.at\n  store double 0xFFF0000000000000, ptr @chibi_tasks.pending_time\n',
'runnable':'  %time.at = getelementptr inbounds %chibi.task, ptr %row, i32 0, i32 13\n  %time = load double, ptr %time.at\n  %now = load double, ptr @chibi_tasks.now\n  %awake = fcmp oge double %now, %time\n  %runnable = and i1 %over, %awake\n  br i1 %runnable, label %run, label %next\n',
'reset_wait':'  store double 0xFFF0000000000000, ptr @chibi_tasks.pending_time\n',
'updated_wait':'  %updated.time = getelementptr inbounds %chibi.task, ptr %updated, i32 0, i32 13\n  %time.pending = load double, ptr @chibi_tasks.pending_time\n  store double %time.pending, ptr %updated.time\n  store double 0xFFF0000000000000, ptr @chibi_tasks.pending_time\n',
}
prefix='''package llvm_native

import strings "core:strings"

// Execution belongs to the program while active. Source values own observers;
// dropping the last one never cancels running work. Completed results and
// cancelled rows remain only while observed, then return to the generation
// free list. Owners and phase fill the old entry-flag padding: rows remain
// 72 bytes, or 96 with timing. The kind/free field is a success type while live.
TaskScheduler :: (timed: bool) -> string {
'''
for k in variables:prefix+='    '+k+' := "";\n'
prefix+='    runnable = "  br i1 %over, label %run, label %next\\n";\n    if (timed) {\n'
for k,v in variables.items():prefix+='        '+k+' = '+json.dumps(v)+';\n'
prefix+='    }\n    text := strings.Join([\n'
lines=text.splitlines(True)
# Compact a handful of IR lines per authored string, keeping state blocks visible.
for at in range(0,len(lines),5):prefix+='        '+json.dumps(''.join(lines[at:at+5]))+',\n'
prefix+='    ], "");\n'
for k in variables:prefix+='    text = strings.ReplaceAll(text, "$'+k+'", '+k+');\n'
prefix+='    return text;\n}\n'
p.write_text(prefix)
