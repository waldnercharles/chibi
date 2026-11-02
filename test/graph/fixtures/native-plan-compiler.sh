#!/bin/sh
# Planner tests need executable native child programs, but never run them.
# The real native compiler/runtime is covered by test/bootstrap/native-runner.sh.
set -eu
output=
while [ "$#" -gt 0 ]; do
    if [ "$1" = --output ]; then
        output=$2
        shift
    fi
    shift
done
test -n "$output"
cat > "$output" <<'RUNNER'
#!/bin/sh
echo 'planner child must not run' >&2
exit 93
RUNNER
chmod +x "$output"
