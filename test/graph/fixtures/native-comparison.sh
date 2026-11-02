#!/bin/sh
# Controlled stage/runner programs: exercise comparison selection, not LLVM.
set -eu
case "$0" in
    */stage2/chibi|*/stage3/chibi|*/runner/driver|*/runner/graph) work=$(dirname "$(dirname "$0")") ;;
    *) work=$(dirname "$0") ;;
esac
case "$0" in
    */runner/driver)
        exec "$(cat "$work/real-host")" --compiler tools/compiler --compiler-core packages/core --root "$PWD" \
            --source tools/toolchain/Driver.chibi --core packages/core --entry Main --timeout-ms 600000 -- "$@"
        ;;
    */runner/graph) echo 'fixture reached graph comparison' >&2; exit 23 ;;
    */host)
        for argument do
            if [ "$argument" = tools/toolchain/Graph.chibi ]; then exit 0; fi
        done
        exec "$(cat "$work/real-host")" "$@"
        ;;
esac
output=
while [ "$#" -gt 0 ]; do
    if [ "$1" = --output ]; then output=$2; shift; fi
    shift
done
case "$output" in
    */Build-Build|*/Graph-Libraries)
        # The comparison plans these children but must never execute them.
        cat > "$output" <<'RUNNER'
#!/bin/sh
echo 'planner child must not run' >&2
exit 93
RUNNER
        chmod +x "$output"
        exit 0
        ;;
    */stage2/chibi|*/stage3/chibi|*/runner/driver|*/runner/graph) ;;
    *) exit 24 ;;
esac
cp "$work/seed" "$output"
chmod +x "$output"
