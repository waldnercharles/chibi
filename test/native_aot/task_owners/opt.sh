#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            sed '/^declare i64 @chibi_task_owner_allocations()$/d; s/define i32 @chibi_scalar(ptr %ctx)/define i32 @task_owner.entry(ptr %ctx)/' "$source" > "$source.audit"
            cat "$CHIBI_TASK_OWNER_AUDIT/Audit.ll" >> "$source.audit"
            mv "$source.audit" "$source"
        fi ;;
    esac
done
exec "$CHIBI_TASK_OWNER_LLVM/opt" "$@"
