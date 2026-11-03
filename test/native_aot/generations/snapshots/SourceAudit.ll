; The image keeps its production context/formatter and family heap. Only
; allocation callbacks are replaced by the common poison/balance oracle.
declare ptr @calloc(i64,i64)
define i32 @chibi_snapshot_source_audit() {
  %bytes = call i64 @chibi_host_family_size()
  %heap = call ptr @calloc(i64 1,i64 %bytes)
  %adopted = call i32 @chibi_host_set_family(ptr %heap)
  %result = call i32 @chibi_scalar(ptr @chibi_context)
  %collected = call i32 @chibi_host_family_collect()
  call void @free(ptr %heap)
  %good = icmp eq i32 %adopted,0
  %checked = select i1 %good,i32 %result,i32 2
  ret i32 %checked
}
define i32 @chibi_value_test_entry(ptr %unused) {
  %allocate.at = getelementptr {ptr,ptr,ptr,ptr,ptr},ptr @chibi_context,i32 0,i32 2
  %release.at = getelementptr {ptr,ptr,ptr,ptr,ptr},ptr @chibi_context,i32 0,i32 4
  store ptr @chibi_value_test_allocate,ptr %allocate.at
  store ptr @chibi_value_test_release,ptr %release.at
  %first = call i32 @chibi_host_call_Frame()
  %second = call i32 @chibi_host_call_Frame()
  %third = call i32 @chibi_host_call_Frame()
  %failed = call i32 @chibi_host_failed()
  %released = call i32 @chibi_host_retained_release()
  %released.twice = call i32 @chibi_host_retained_release()
  %frames = or i32 %first,%second
  %all = or i32 %frames,%third
  %source.good = icmp eq i32 %failed,0
  %result = select i1 %source.good,i32 %all,i32 134
  ret i32 %result
}
