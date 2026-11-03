#!/bin/sh
set -eu
# Start modules use ObjectResult directly, so they do not pass through opt.
for source do
    case "$source" in *.ll)
        if grep -q '^@chibi_context = hidden global ' "$source"; then
            sed '/^@chibi_context = hidden global /s/ptr @chibi_allocate/ptr @task_owner.shared.allocate/; /^@chibi_context = hidden global /s/ptr @chibi_release/ptr @task_owner.shared.release/' "$source" > "$source.audit"
            if ! grep -q '^declare void @llvm.trap()' "$source.audit"; then
                printf 'declare void @llvm.trap()\n' >> "$source.audit"
            fi
            cat "$CHIBI_TASK_OWNER_AUDIT/SharedAudit.ll" >> "$source.audit"
            mv "$source.audit" "$source"
        fi ;;
    esac
done
exec "$CHIBI_TASK_OWNER_LLVM/llc" "$@"
