#!/bin/sh
# Native output preserves the existing print_string action's byte contract.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native output: unsupported test host' >&2; exit 77 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-output.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
compile() {
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$1" --entry Main --output "$2" --llvm "$llvm" --target "$target" --level "$3" --jobs 1
}
for level in 0 2; do
    compile "$root/test/native_aot/output/Main.chibi" "$work/output" "$level"
    "$work/output" > "$work/actual"
    python3 - "$work/actual" <<'PY'
import pathlib, sys
assert pathlib.Path(sys.argv[1]).read_bytes() == '\nplain\nλ 日本語\nA\0B\nowned 42\n'.encode()
PY
    printf 'PASS native print empty, named, UTF-8, embedded NUL and owned text O%s\n' "$level"
done
mkdir "$work/shadow" "$work/rejected" "$work/ready"
cat > "$work/shadow/Main.chibi" <<'SOURCE'
package shadow_print
print_string :: (text: string) -> int { return text_byte_length(text) + 7; }
Main :: () -> int { assert(print_string("shadow") == 13); return 0; }
SOURCE
compile "$work/shadow/Main.chibi" "$work/shadow/program" 2
"$work/shadow/program" > "$work/actual"
test ! -s "$work/actual"
for body in 'print_string();' 'print_string(1);' 'print_string("one", "two");' 'value := print_string("void");'; do
    printf 'package rejected\nMain :: () { %s }\n' "$body" > "$work/rejected/Main.chibi"
    printf 'preserved' > "$work/rejected/program"
    if compile "$work/rejected/Main.chibi" "$work/rejected/program" 0 > "$work/rejected/log" 2>&1; then
        echo 'native output accepted an invalid print call' >&2; exit 1
    fi
    test "$(cat "$work/rejected/program")" = preserved
done
cat > "$work/ready/Main.chibi" <<'SOURCE'
package output_readiness
Main :: () { print_string("ready"); while true {} }
SOURCE
compile "$work/ready/Main.chibi" "$work/ready/program" 2
python3 - "$work/ready/program" <<'PY'
import select, subprocess, sys
process = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE)
try:
    ready, _, _ = select.select([process.stdout], [], [], 5)
    assert ready and process.stdout.readline() == b'ready\n', 'print must be visible before the program ends'
    assert process.poll() is None
finally:
    process.kill()
    process.wait()
PY
printf 'PASS native print shadowing, rejected calls, preserved output and synchronous readiness\n'
