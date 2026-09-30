#!/bin/sh
# Claude Code on the web: install what `make build` and `make test` need.
# The container is cached after this hook, so later sessions find it done.
set -eu
[ "${CLAUDE_CODE_REMOTE:-}" = true ] || exit 0
[ "$(uname -s)" = Linux ] || exit 0

ready() {
	[ -e /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.asan-x86_64.a ] &&
		[ "$(readlink -f /usr/bin/cc)" = "$(readlink -f /usr/bin/clang-18)" ] &&
		command -v xvfb-run >/dev/null
}
ready && exit 0

log=/tmp/chibi-cloud-setup.log
if sh "$CLAUDE_PROJECT_DIR/tools/cloud_setup.sh" >"$log" 2>&1; then
	echo "Installed the build and test toolchain (log: $log)."
else
	echo "tools/cloud_setup.sh failed; see $log." >&2
	exit 1
fi
