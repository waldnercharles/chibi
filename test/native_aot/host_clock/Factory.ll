; No C runtime implementation participates in this native embedding contract.
@factory.mode = private constant i32 @MODE@
define private i32 @factory.provider(ptr %state, ptr %nanoseconds, ptr %error) {
  %calls = load i32, ptr %state
  %next = add i32 %calls, 1
  store i32 %next, ptr %state
  store i64 9007199254740993, ptr %nanoseconds
  store i32 5, ptr %error
  ret i32 0
}
define i32 @chibi_scalar() {
entry:
  %mode = load i32, ptr @factory.mode
  %missing = icmp eq i32 %mode, 1
  br i1 %missing, label %null, label %check.mode
null:
  %bad = call i32 @chibi_clock_grant_init(ptr null, ptr null, ptr null)
  ret i32 99
check.mode:
  %overflow = icmp eq i32 %mode, 2
  br i1 %overflow, label %exhaust, label %initialize
exhaust:
  store i64 -1, ptr @chibi_clock_next_domain
  br label %initialize
initialize:
  %first = alloca { i64, ptr, ptr }, align 8
  %second = alloca { i64, ptr, ptr }, align 8
  %calls = alloca i32, align 4
  %nanoseconds = alloca i64, align 8
  %error = alloca i32, align 4
  store i32 0, ptr %calls
  %a = call i32 @chibi_clock_grant_init(ptr %first, ptr @factory.provider, ptr %calls)
  %b = call i32 @chibi_clock_grant_init(ptr %second, ptr null, ptr null)
  %domain.a = load i64, ptr %first
  %domain.b = load i64, ptr %second
  %nonzero.a = icmp ne i64 %domain.a, 0
  %nonzero.b = icmp ne i64 %domain.b, 0
  %distinct = icmp ne i64 %domain.a, %domain.b
  %unread = load i32, ptr %calls
  %cold = icmp eq i32 %unread, 0
  %provider.at = getelementptr { i64, ptr, ptr }, ptr %first, i32 0, i32 1
  %provider = load ptr, ptr %provider.at
  %state.at = getelementptr { i64, ptr, ptr }, ptr %first, i32 0, i32 2
  %state = load ptr, ptr %state.at
  %status = call i32 %provider(ptr %state, ptr %nanoseconds, ptr %error)
  %time = load i64, ptr %nanoseconds
  %code = load i32, ptr %error
  %read = load i32, ptr %calls
  %ok = icmp eq i32 %status, 0
  %exact = icmp eq i64 %time, 9007199254740993
  %raw.error = icmp eq i32 %code, 5
  %once = icmp eq i32 %read, 1
  %platform.at = getelementptr { i64, ptr, ptr }, ptr %second, i32 0, i32 1
  %platform = load ptr, ptr %platform.at
  %sampled = call i32 %platform(ptr null, ptr %nanoseconds, ptr %error)
  %available = icmp eq i32 %sampled, @STATUS@
  %platform.time = load i64, ptr %nanoseconds
  %platform.error = load i32, ptr %error
  %nonnegative = icmp sge i64 %platform.time, 0
  %clear = icmp eq i32 %platform.error, 0
  %again = call i32 @chibi_clock_grant_init(ptr %first, ptr @factory.provider, ptr %calls)
  %domain.c = load i64, ptr %first
  %new.a = icmp ne i64 %domain.c, %domain.a
  %new.b = icmp ne i64 %domain.c, %domain.b
  %authority = call i@WORD@ @chibi_clock_host_init()
  %factory = ptrtoint ptr @chibi_clock_grant_init to i@WORD@
  %same = icmp eq i@WORD@ %authority, %factory
  %v0 = and i1 %nonzero.a, %nonzero.b
  %v1 = and i1 %v0, %distinct
  %v2 = and i1 %v1, %cold
  %v3 = and i1 %v2, %ok
  %v4 = and i1 %v3, %exact
  %v5 = and i1 %v4, %raw.error
  %v6 = and i1 %v5, %once
  %v7 = and i1 %v6, %available
  %v8 = and i1 %v7, %nonnegative
  %v9 = and i1 %v8, %clear
  %v10 = and i1 %v9, %new.a
  %v11 = and i1 %v10, %new.b
  %valid = and i1 %v11, %same
  %result = select i1 %valid, i32 0, i32 99
  ret i32 %result
}
