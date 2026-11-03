; Desktop C ABI. Per-call temporary allocations and resource ownership never
; escape. Reentrant callbacks push a separate TLS scope; only published malloc
; output belongs to the caller. Quota/failure unwind lives in the child service.
target triple = "@TRIPLE@"

%context = type { ptr, ptr, ptr, ptr, ptr }
%options = type { ptr, ptr, i64, ptr, ptr }
; Zero-length i128 fields align opaque SDK storage and allocation payloads to 16 bytes.
%call = type { %context, [0 x i128], [0 x i8], ptr, ptr, i32, ptr, ptr, i64, i1, ptr, i32, ptr, i32, ptr, ptr, i64 }
%allocation = type { ptr, ptr, [0 x i128], [0 x i8] }
; Each resource node belongs to this call, independent of authored value roots.
%resource = type { ptr, i64, i32 }
@chibi_asset_native_active = internal thread_local global ptr null, align 8
@chibi_asset_native_live = internal thread_local global i32 0, align 8

@chibi_asset_native_message.budget = private constant [45 x i8] c"The package compile exceeded its step budget\00"
@chibi_asset_native_message.failed = private constant [35 x i8] c"The native package compiler failed\00"
@chibi_asset_native_message.cancelled = private constant [34 x i8] c"The package compile was cancelled\00"
@chibi_asset_native_message.output = private constant [51 x i8] c"The package compiler output exceeds its byte limit\00"
@chibi_asset_native_message.request = private constant [52 x i8] c"The package compiler request exceeds its byte limit\00"
@chibi_asset_native_message.missing = private constant [40 x i8] c"The package compiler request is missing\00"
@chibi_asset_native_message.busy = private constant [55 x i8] c"The package compiler is already running on this thread\00"
@chibi_asset_native_message.rejected = private constant [42 x i8] c"The package compiler rejected its request\00"

declare ptr @calloc(i64, i64)
declare ptr @malloc(i64)
declare void @free(ptr)
declare void @abort() noreturn
declare i64 @strnlen(ptr, i64)
declare i64 @strlen(ptr)
declare i32 @close(i32)
declare i32 @closedir(ptr)
declare i32 @unlink(ptr)
declare i32 @strcmp(ptr, ptr)
declare void @chibi_asset_publication_release()
declare void @llvm.trap() cold noreturn nounwind
declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1 immarg)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1 immarg)
declare i32 @chibi_scalar(ptr)
declare void @chibi_format_text(ptr, ptr, i32, ptr)

define internal void @chibi_asset_native_require(i1 %valid) {
entry:
  br i1 %valid, label %done, label %broken
broken:
  call void @llvm.trap()
  unreachable
done:
  ret void
}

define internal ptr @chibi_asset_native_call() {
  %call = load ptr, ptr @chibi_asset_native_active
  %present = icmp ne ptr %call, null
  call void @chibi_asset_native_require(i1 %present)
  ret ptr %call
}

define internal void @chibi_asset_native_fail(ptr %diagnostic, ptr %message) {
  %wide.count = call i64 @strnlen(ptr %message, i64 255)
  %count = trunc i64 %wide.count to i32
  %destination = getelementptr i8, ptr %diagnostic, i32 20
  call void @llvm.memcpy.p0.p0.i32(ptr %destination, ptr %message, i32 %count, i1 false)
  %end = getelementptr i8, ptr %destination, i32 %count
  store i8 0, ptr %end
  ret void
}


; Desktop quota and cancellation belong to the supervised compiler child.
; An unexpected invariant failure in this ABI implementation remains fatal.
define void @chibi_native_failure(i32 %kind) noreturn {
  call void @abort()
  unreachable
}
define void @chibi_native_step() { ret void }

define ptr @chibi_allocate(ptr %call, i64 %size) {
entry:
  %fits = icmp ule i64 %size, 18446744073709551599
  br i1 %fits, label %allocate, label %oom
allocate:
  %total = add i64 %size, 16
  %item = call ptr @calloc(i64 1, i64 %total)
  %present = icmp ne ptr %item, null
  br i1 %present, label %link, label %oom
oom:
  call void @abort()
  unreachable
link:
  %head.at = getelementptr %call, ptr %call, i32 0, i32 3
  %head = load ptr, ptr %head.at
  %next.at = getelementptr %allocation, ptr %item, i32 0, i32 1
  store ptr %head, ptr %next.at
  %has.head = icmp ne ptr %head, null
  br i1 %has.head, label %previous, label %finish
previous:
  store ptr %item, ptr %head
  br label %finish
finish:
  store ptr %item, ptr %head.at
  ; @COUNT_ALLOCATE@
  %bytes = getelementptr %allocation, ptr %item, i32 0, i32 3
  ret ptr %bytes
}

define internal void @chibi_asset_native_release(ptr %call, ptr %bytes) {
entry:
  %empty = icmp eq ptr %bytes, null
  br i1 %empty, label %done, label %unlink
unlink:
  %item = getelementptr i8, ptr %bytes, i32 -16
  %previous = load ptr, ptr %item
  %next.at = getelementptr %allocation, ptr %item, i32 0, i32 1
  %next = load ptr, ptr %next.at
  %has.previous = icmp ne ptr %previous, null
  br i1 %has.previous, label %middle, label %first
middle:
  %previous.next = getelementptr %allocation, ptr %previous, i32 0, i32 1
  store ptr %next, ptr %previous.next
  br label %following
first:
  %head = getelementptr %call, ptr %call, i32 0, i32 3
  store ptr %next, ptr %head
  br label %following
following:
  %has.next = icmp ne ptr %next, null
  br i1 %has.next, label %back, label %finish
back:
  store ptr %previous, ptr %next
  br label %finish
finish:
  ; @COUNT_RELEASE@
  call void @free(ptr %item)
  br label %done
done:
  ret void
}

define internal void @chibi_asset_native_index_failed(ptr %call, i32 %index, i32 %length) noreturn {
  call void @chibi_native_failure(i32 1)
  unreachable
}

define i32 @chibi_asset_request_length() {
  %call = call ptr @chibi_asset_native_call()
  %length.at = getelementptr %call, ptr %call, i32 0, i32 5
  %length = load i32, ptr %length.at
  ret i32 %length
}
define i32 @chibi_asset_request_copy(ptr %buffer, i32 %count) {
  %call = call ptr @chibi_asset_native_call()
  %length.at = getelementptr %call, ptr %call, i32 0, i32 5
  %length = load i32, ptr %length.at
  %same = icmp eq i32 %length, %count
  call void @chibi_asset_native_require(i1 %same)
  %request.at = getelementptr %call, ptr %call, i32 0, i32 4
  %request = load ptr, ptr %request.at
  call void @llvm.memcpy.p0.p0.i32(ptr %buffer, ptr %request, i32 %count, i1 false)
  ret i32 0
}
define i64 @chibi_asset_budget() {
entry:
  %call = call ptr @chibi_asset_native_call()
  %options.at = getelementptr %call, ptr %call, i32 0, i32 6
  %options = load ptr, ptr %options.at
  %present = icmp ne ptr %options, null
  br i1 %present, label %read, label %default
read:
  %budget.at = getelementptr %options, ptr %options, i32 0, i32 2
  %budget = load i64, ptr %budget.at
  %set = icmp ne i64 %budget, 0
  %effective = select i1 %set, i64 %budget, i64 4000000000
  ret i64 %effective
default:
  ret i64 4000000000
}

define i32 @chibi_asset_publish(ptr %bytes, i32 %count) {
entry:
  %call = call ptr @chibi_asset_native_call()
  %output.at = getelementptr %call, ptr %call, i32 0, i32 10
  %existing = load ptr, ptr %output.at
  %empty = icmp eq ptr %existing, null
  %positive = icmp sge i32 %count, 0
  %valid = and i1 %empty, %positive
  call void @chibi_asset_native_require(i1 %valid)
  %bounded = icmp ule i32 %count, 16777216
  br i1 %bounded, label %allocate, label %large
large:
  %diagnostic.at = getelementptr %call, ptr %call, i32 0, i32 7
  %diagnostic = load ptr, ptr %diagnostic.at
  call void @chibi_asset_native_fail(ptr %diagnostic, ptr @chibi_asset_native_message.output)
  ret i32 2
allocate:
  %nonempty = icmp ne i32 %count, 0
  %size = select i1 %nonempty, i32 %count, i32 1
  %wide.size = zext i32 %size to i64
  %output = call ptr @malloc(i64 %wide.size)
  %present = icmp ne ptr %output, null
  br i1 %present, label %publish, label %oom
oom:
  call void @abort()
  unreachable
publish:
  call void @llvm.memcpy.p0.p0.i32(ptr %output, ptr %bytes, i32 %count, i1 false)
  store ptr %output, ptr %output.at
  %length.at = getelementptr %call, ptr %call, i32 0, i32 11
  store i32 %count, ptr %length.at
  ret i32 0
}
define i32 @chibi_asset_rejected(i32 %document, i32 %line, i32 %column, ptr %message) {
  %call = call ptr @chibi_asset_native_call()
  %diagnostic.at = getelementptr %call, ptr %call, i32 0, i32 7
  %diagnostic = load ptr, ptr %diagnostic.at
  store i32 %document, ptr %diagnostic
  %line.at = getelementptr i8, ptr %diagnostic, i32 12
  store i32 %line, ptr %line.at
  %column.at = getelementptr i8, ptr %diagnostic, i32 16
  store i32 %column, ptr %column.at
  call void @chibi_asset_native_fail(ptr %diagnostic, ptr %message)
  ret i32 0
}

define ptr @asset_compiler_compile_closure(ptr %request, i64 %request_length, ptr %length, ptr %options, ptr %diagnostic) {
  %output = call ptr @chibi_asset_native_compile(ptr %request, i64 %request_length, ptr null, i32 0, ptr %length, ptr %options, ptr %diagnostic)
  ret ptr %output
}
define ptr @asset_compiler_compile_closure_cached(ptr %request, i64 %request_length, ptr %salt, ptr %length, ptr %options, ptr %diagnostic) {
  %output = call ptr @chibi_asset_native_compile(ptr %request, i64 %request_length, ptr %salt, i32 1, ptr %length, ptr %options, ptr %diagnostic)
  ret ptr %output
}
define internal ptr @chibi_asset_native_compile(ptr %request, i64 %request_length, ptr %salt, i32 %cached, ptr %length, ptr %options, ptr %diagnostic) {
entry:
  store i64 0, ptr %length
  call void @llvm.memset.p0.i32(ptr %diagnostic, i8 0, i32 792, i1 false)
  store i32 -1, ptr %diagnostic
  %missing = icmp eq ptr %request, null
  %large = icmp ugt i64 %request_length, 67108864
  %invalid = or i1 %missing, %large
  br i1 %invalid, label %bad, label %available
bad:
  %bad.message = select i1 %missing, ptr @chibi_asset_native_message.missing, ptr @chibi_asset_native_message.request
  call void @chibi_asset_native_fail(ptr %diagnostic, ptr %bad.message)
  ret ptr null
available:
  %previous = load ptr, ptr @chibi_asset_native_active
  br label %allocate
allocate:
  %end = getelementptr %call, ptr null, i32 1
  %size = ptrtoint ptr %end to i64
  %call = call ptr @calloc(i64 1, i64 %size)
  %allocated = icmp ne ptr %call, null
  br i1 %allocated, label %initialize, label %oom
oom:
  call void @abort()
  unreachable
initialize:
  store ptr @chibi_format_text, ptr %call
  %allocator = getelementptr %context, ptr %call, i32 0, i32 2
  store ptr @chibi_allocate, ptr %allocator
  %index = getelementptr %context, ptr %call, i32 0, i32 3
  store ptr @chibi_asset_native_index_failed, ptr %index
  %release = getelementptr %context, ptr %call, i32 0, i32 4
  store ptr @chibi_asset_native_release, ptr %release
  %request.at = getelementptr %call, ptr %call, i32 0, i32 4
  store ptr %request, ptr %request.at
  %request_length.at = getelementptr %call, ptr %call, i32 0, i32 5
  %narrow.length = trunc i64 %request_length to i32
  store i32 %narrow.length, ptr %request_length.at
  %options.at = getelementptr %call, ptr %call, i32 0, i32 6
  store ptr %options, ptr %options.at
  %diagnostic.at = getelementptr %call, ptr %call, i32 0, i32 7
  store ptr %diagnostic, ptr %diagnostic.at
  %salt.at = getelementptr %call, ptr %call, i32 0, i32 12
  store ptr %salt, ptr %salt.at
  %salt.length.at = getelementptr %call, ptr %call, i32 0, i32 16
  store i64 -1, ptr %salt.length.at
  %cached.at = getelementptr %call, ptr %call, i32 0, i32 13
  store i32 %cached, ptr %cached.at
  %previous.at = getelementptr %call, ptr %call, i32 0, i32 15
  store ptr %previous, ptr %previous.at
  store ptr %call, ptr @chibi_asset_native_active
  br label %compile
compile:
  %result = call i32 @chibi_scalar(ptr %call)
  %failure.at = getelementptr %context, ptr %call, i32 0, i32 1
  %reported = load ptr, ptr %failure.at
  %failed = icmp ne ptr %reported, null
  %compiled = select i1 %failed, i32 134, i32 %result
  br label %drain
drain:
  %status = phi i32 [ %compiled, %compile ]
  ; Resources close before their list
  ; nodes and every other managed temporary are released.
  call void @chibi_asset_native_close_resources(ptr %call)
  br label %next
next:
  %head.at = getelementptr %call, ptr %call, i32 0, i32 3
  %head = load ptr, ptr %head.at
  %has.head = icmp ne ptr %head, null
  br i1 %has.head, label %drop, label %output
drop:
  %bytes = getelementptr %allocation, ptr %head, i32 0, i32 3
  call void @chibi_asset_native_release(ptr %call, ptr %bytes)
  br label %next
output:
  %output.at = getelementptr %call, ptr %call, i32 0, i32 10
  %published = load ptr, ptr %output.at
  %succeeded = icmp eq i32 %status, 0
  br i1 %succeeded, label %success, label %failure
success:
  %output_length.at = getelementptr %call, ptr %call, i32 0, i32 11
  %output_length = load i32, ptr %output_length.at
  %wide.output_length = zext i32 %output_length to i64
  store i64 %wide.output_length, ptr %length
  br label %finish
failure:
  call void @free(ptr %published)
  %message = getelementptr i8, ptr %diagnostic, i32 20
  %first = load i8, ptr %message
  %has.message = icmp ne i8 %first, 0
  br i1 %has.message, label %finish, label %explain
explain:
  call void @chibi_asset_native_fail(ptr %diagnostic, ptr @chibi_asset_native_message.rejected)
  br label %finish
finish:
  %escaped = phi ptr [ %published, %success ], [ null, %failure ], [ null, %explain ]
  %restore.at = getelementptr %call, ptr %call, i32 0, i32 15
  %restore = load ptr, ptr %restore.at
  store ptr %restore, ptr @chibi_asset_native_active
  call void @free(ptr %call)
  ret ptr %escaped
}

; Option selectors are shared with the desktop ABI: program, cache, salt.
; -1 is NULL/default, 0 is explicit empty, -2 is malformed or over the bound.
define internal ptr @chibi_asset_native_option(i32 %which) {
entry:
  %call = call ptr @chibi_asset_native_call()
  switch i32 %which, label %invalid [ i32 0, label %program
                                    i32 1, label %cache
                                    i32 2, label %salt ]
program:
  %program.options.at = getelementptr %call, ptr %call, i32 0, i32 6
  %program.options = load ptr, ptr %program.options.at
  %has.program.options = icmp ne ptr %program.options, null
  br i1 %has.program.options, label %load.program, label %none
load.program:
  %program.value = load ptr, ptr %program.options
  ret ptr %program.value
cache:
  %options.at = getelementptr %call, ptr %call, i32 0, i32 6
  %options = load ptr, ptr %options.at
  %has.options = icmp ne ptr %options, null
  br i1 %has.options, label %load.cache, label %none
load.cache:
  %cache.at = getelementptr %options, ptr %options, i32 0, i32 1
  %cache.value = load ptr, ptr %cache.at
  ret ptr %cache.value
salt:
  %salt.at = getelementptr %call, ptr %call, i32 0, i32 12
  %salt.value = load ptr, ptr %salt.at
  ret ptr %salt.value
none:
  ret ptr null
invalid:
  call void @llvm.trap()
  unreachable
}
define i32 @chibi_asset_cached() {
  %call = call ptr @chibi_asset_native_call()
  %at = getelementptr %call, ptr %call, i32 0, i32 13
  %cached = load i32, ptr %at
  ret i32 %cached
}
define i32 @chibi_asset_option_length(i32 %which) {
entry:
  %is.salt = icmp eq i32 %which, 2
  br i1 %is.salt, label %salt, label %ordinary
salt:
  %salt.size = call i64 @chibi_asset_salt_length()
  %salt.fits = icmp ule i64 %salt.size, 2147483647
  %salt.narrow = trunc i64 %salt.size to i32
  %salt.result = select i1 %salt.fits, i32 %salt.narrow, i32 -2
  %absent = icmp eq i64 %salt.size, -1
  %effective = select i1 %absent, i32 -1, i32 %salt.result
  ret i32 %effective
ordinary:
  %value = call ptr @chibi_asset_native_option(i32 %which)
  %present = icmp ne ptr %value, null
  br i1 %present, label %length, label %none
length:
  %wide.size = call i64 @strnlen(ptr %value, i64 4097)
  %fits = icmp ule i64 %wide.size, 4096
  %size = trunc i64 %wide.size to i32
  %result = select i1 %fits, i32 %size, i32 -2
  ret i32 %result
none:
  ret i32 -1
}
define i32 @chibi_asset_option_copy(i32 %which, ptr %buffer, i32 %count) {
entry:
  %size = call i32 @chibi_asset_option_length(i32 %which)
  %nonnegative = icmp sge i32 %size, 0
  %same = icmp eq i32 %size, %count
  %valid = and i1 %nonnegative, %same
  br i1 %valid, label %copy, label %invalid
copy:
  %value = call ptr @chibi_asset_native_option(i32 %which)
  call void @llvm.memcpy.p0.p0.i32(ptr %buffer, ptr %value, i32 %count, i1 false)
  ret i32 0
invalid:
  ret i32 -1
}
define i32 @chibi_asset_cancelled() {
entry:
  %call = call ptr @chibi_asset_native_call()
  %options.at = getelementptr %call, ptr %call, i32 0, i32 6
  %options = load ptr, ptr %options.at
  %has.options = icmp ne ptr %options, null
  br i1 %has.options, label %callback, label %no
callback:
  %cancel.at = getelementptr %options, ptr %options, i32 0, i32 3
  %cancel = load ptr, ptr %cancel.at
  %has.cancel = icmp ne ptr %cancel, null
  br i1 %has.cancel, label %check, label %no
check:
  %user.at = getelementptr %options, ptr %options, i32 0, i32 4
  %user = load ptr, ptr %user.at
  %cancelled = call zeroext i1 %cancel(ptr %user)
  %result = zext i1 %cancelled to i32
  ret i32 %result
no:
  ret i32 0
}
; A node is published before the next authored checkpoint can unwind. The
; (value,kind) pair disambiguates an fd from a same-valued directory address.
define ptr @chibi_asset_call_context() {
  %call = call ptr @chibi_asset_native_call()
  ret ptr %call
}
; Kinds: 0 FD, 1 DIR pointer, 2 publication-lock FD, 3 owned temp-path pointer.
define i32 @chibi_asset_resource_add(i64 %value, i32 %kind) {
entry:
  %call = call ptr @chibi_asset_native_call()
  %ordinary.fd = icmp eq i32 %kind, 0
  %lock.fd = icmp eq i32 %kind, 2
  %fd = or i1 %ordinary.fd, %lock.fd
  %fd.fits = icmp ule i64 %value, 2147483647
  %directory = icmp eq i32 %kind, 1
  %temporary = icmp eq i32 %kind, 3
  %pointer.kind = or i1 %directory, %temporary
  %nonnull = icmp ne i64 %value, 0
  %pointer.fits = icmp ne i64 %value, 0
  %pointer.valid = and i1 %nonnull, %pointer.fits
  %valid.fd = and i1 %fd, %fd.fits
  %valid.directory = and i1 %pointer.kind, %pointer.valid
  %valid = or i1 %valid.fd, %valid.directory
  br i1 %valid, label %allocate, label %invalid
allocate:
  %node = call ptr @chibi_allocate(ptr %call, i64 24)
  %head.at = getelementptr %call, ptr %call, i32 0, i32 14
  %head = load ptr, ptr %head.at
  store ptr %head, ptr %node
  %value.at = getelementptr %resource, ptr %node, i32 0, i32 1
  store i64 %value, ptr %value.at
  %kind.at = getelementptr %resource, ptr %node, i32 0, i32 2
  store i32 %kind, ptr %kind.at
  store ptr %node, ptr %head.at
  ret i32 0
invalid:
  ret i32 -1
}
; The caller closes first, then removes the ownership record synchronously.
define i32 @chibi_asset_resource_remove(i64 %value, i32 %kind) {
entry:
  %call = call ptr @chibi_asset_native_call()
  %head.at = getelementptr %call, ptr %call, i32 0, i32 14
  br label %next
next:
  %link = phi ptr [ %head.at, %entry ], [ %node, %following ]
  %node = load ptr, ptr %link
  %present = icmp ne ptr %node, null
  br i1 %present, label %check, label %missing
check:
  %value.at = getelementptr %resource, ptr %node, i32 0, i32 1
  %owned.value = load i64, ptr %value.at
  %kind.at = getelementptr %resource, ptr %node, i32 0, i32 2
  %owned.kind = load i32, ptr %kind.at
  %same.value = icmp eq i64 %value, %owned.value
  %same.kind = icmp eq i32 %kind, %owned.kind
  %same = and i1 %same.value, %same.kind
  br i1 %same, label %remove, label %following
following:
  br label %next
remove:
  %following.node = load ptr, ptr %node
  store ptr %following.node, ptr %link
  call void @chibi_asset_native_release(ptr %call, ptr %node)
  ret i32 0
missing:
  ret i32 -1
}
define internal void @chibi_asset_native_close_resources(ptr %call) {
entry:
  %head.at = getelementptr %call, ptr %call, i32 0, i32 14
  br label %next
next:
  %node = load ptr, ptr %head.at
  %present = icmp ne ptr %node, null
  br i1 %present, label %read, label %done
read:
  %following = load ptr, ptr %node
  %value.at = getelementptr %resource, ptr %node, i32 0, i32 1
  %value = load i64, ptr %value.at
  %kind.at = getelementptr %resource, ptr %node, i32 0, i32 2
  %kind = load i32, ptr %kind.at
  store ptr %following, ptr %head.at
  switch i32 %kind, label %broken [ i32 0, label %close.fd
                                        i32 1, label %close.pointer
                                        i32 2, label %close.fd
                                        i32 3, label %close.pointer ]
close.fd:
  %descriptor = trunc i64 %value to i32
  %closed.fd = call i32 @close(i32 %descriptor)
  %publication = icmp eq i32 %kind, 2
  br i1 %publication, label %unlock, label %release
unlock:
  call void @chibi_asset_publication_release()
  br label %release
close.pointer:
  %pointer = inttoptr i64 %value to ptr
  %directory = icmp eq i32 %kind, 1
  br i1 %directory, label %close.directory, label %remove.temporary
close.directory:
  %closed.directory = call i32 @closedir(ptr %pointer)
  br label %release
remove.temporary:
  %unlinked = call i32 @unlink(ptr %pointer)
  call void @chibi_asset_native_release(ptr %call, ptr %pointer)
  br label %release
broken:
  call void @llvm.trap()
  unreachable
release:
  call void @chibi_asset_native_release(ptr %call, ptr %node)
  br label %next
done:
  ret void
}


; Finish a published/removed temporary inside the same uninstrumented boundary.
define i32 @chibi_asset_temp_remove(ptr %path) {
entry:
  %call = call ptr @chibi_asset_native_call()
  %head.at = getelementptr %call, ptr %call, i32 0, i32 14
  br label %next
next:
  %node = phi ptr [ null, %entry ], [ %following, %advance ]
  %first = icmp eq ptr %node, null
  %head = load ptr, ptr %head.at
  %current = select i1 %first, ptr %head, ptr %node
  %present = icmp ne ptr %current, null
  br i1 %present, label %check, label %missing
check:
  %kind.at = getelementptr %resource, ptr %current, i32 0, i32 2
  %kind = load i32, ptr %kind.at
  %temporary = icmp eq i32 %kind, 3
  br i1 %temporary, label %compare, label %advance
compare:
  %value.at = getelementptr %resource, ptr %current, i32 0, i32 1
  %value = load i64, ptr %value.at
  %saved = inttoptr i64 %value to ptr
  %compared = call i32 @strcmp(ptr %saved, ptr %path)
  %same = icmp eq i32 %compared, 0
  br i1 %same, label %remove, label %advance
advance:
  %following = load ptr, ptr %current
  %more = icmp ne ptr %following, null
  br i1 %more, label %next, label %missing
remove:
  %removed = call i32 @chibi_asset_resource_remove(i64 %value, i32 3)
  call void @chibi_asset_native_release(ptr %call, ptr %saved)
  ret i32 %removed
missing:
  ret i32 0
}

; The borrowed salt is scanned once and streamed without an owned whole-string
; copy. -1 means NULL; a valid C object cannot have UINT64_MAX bytes.
define i64 @chibi_asset_salt_length() {
entry:
  %call = call ptr @chibi_asset_native_call()
  %salt.at = getelementptr %call, ptr %call, i32 0, i32 12
  %salt = load ptr, ptr %salt.at
  %present = icmp ne ptr %salt, null
  br i1 %present, label %cached, label %absent
cached:
  %at = getelementptr %call, ptr %call, i32 0, i32 16
  %previous = load i64, ptr %at
  %known = icmp ne i64 %previous, -1
  br i1 %known, label %reuse, label %measure
reuse:
  ret i64 %previous
measure:
  %native.length = call i64 @strlen(ptr %salt)
  %length = add i64 %native.length, 0
  store i64 %length, ptr %at
  ret i64 %length
absent:
  ret i64 -1
}
define i32 @chibi_asset_salt_read(i64 %first, ptr %buffer, i32 %count) {
entry:
  %size = call i64 @chibi_asset_salt_length()
  %present = icmp ne i64 %size, -1
  %within = icmp ule i64 %first, %size
  %remaining = sub i64 %size, %first
  %positive = icmp sge i32 %count, 0
  %wide.count = zext i32 %count to i64
  %bounded = icmp ule i64 %wide.count, %remaining
  %range = and i1 %within, %bounded
  %admitted = and i1 %present, %positive
  %valid = and i1 %range, %admitted
  br i1 %valid, label %copy, label %invalid
copy:
  %salt = call ptr @chibi_asset_native_option(i32 2)
  %source = getelementptr i8, ptr %salt, i64 %first
  call void @llvm.memcpy.p0.p0.i32(ptr %buffer, ptr %source, i32 %count, i1 false)
  ret i32 0
invalid:
  ret i32 -1
}
