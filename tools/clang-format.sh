#!/bin/sh
# Formats project C sources with the clang-format major version CI uses.
#   tools/clang-format.sh           format every checked source
#   tools/clang-format.sh --check   fail when any checked source differs
#   tools/clang-format.sh --staged  format staged sources whole and re-stage
#                                   them (the pre-commit hook)
#   tools/clang-format.sh --resolve  print the selected formatter without formatting
# Other major versions format differently, so they are never substituted.
set -eu

CLANG_FORMAT_VERSION=22

cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

formatter()
{
	for candidate in "${CLANG_FORMAT:-}" "clang-format-$CLANG_FORMAT_VERSION" \
		"/opt/homebrew/opt/llvm@$CLANG_FORMAT_VERSION/bin/clang-format" \
		"/usr/local/opt/llvm@$CLANG_FORMAT_VERSION/bin/clang-format" \
		"/usr/lib/llvm-$CLANG_FORMAT_VERSION/bin/clang-format" clang-format; do
		[ -n "$candidate" ] || continue
		if command -v "$candidate" >/dev/null 2>&1 &&
			version_output=$("$candidate" --version); then
			major=$(printf '%s\n' "$version_output" | sed -n 's/.*version \([0-9]*\)\..*/\1/p')
			if [ "$major" = "$CLANG_FORMAT_VERSION" ]; then
				command -v "$candidate"
				return 0
			fi
		fi
		if [ "$candidate" = "${CLANG_FORMAT:-}" ]; then
			echo "Explicit CLANG_FORMAT=$CLANG_FORMAT must be an executable clang-format $CLANG_FORMAT_VERSION; unset it to use automatic selection." >&2
			return 1
		fi
	done
	echo "clang-format $CLANG_FORMAT_VERSION is required (CI uses it)." >&2
	echo "Install it with: brew install llvm@$CLANG_FORMAT_VERSION, or set CLANG_FORMAT." >&2
	return 1
}

# The checked sources: C files under src, tools/reflect, tools/format and test, and public headers.
sources()
{
	find src tools/reflect tools/format test -type f \( -name '*.c' -o -name '*.h' \)
	find include -type f -name '*.h'
}

clang_format=$(formatter)

case "${1:-}" in
--resolve)
	printf '%s\n' "$clang_format"
	;;
"")
	sources | xargs "$clang_format" --style=file -i
	;;
--check)
	sources | xargs "$clang_format" --dry-run --Werror --style=file
	;;
--staged)
	# Formats the staged content, so a commit matches what --check sees.
	temporary=$(mktemp -d)
	trap 'rm -rf "$temporary"' EXIT HUP INT TERM
	failed=0
	for file in $(git diff --cached --name-only --diff-filter=ACMR -- \
		'src/*.c' 'src/*.h' 'tools/reflect/*.c' 'tools/reflect/*.h' \
		'tools/format/*.c' 'tools/format/*.h' \
		'test/*.c' 'test/*.h' 'include/*.h'); do
		git show ":$file" > "$temporary/staged"
		"$clang_format" --style=file --assume-filename="$file" \
			< "$temporary/staged" > "$temporary/formatted"
		if cmp -s "$temporary/staged" "$temporary/formatted"; then
			continue
		fi
		if ! git diff --quiet -- "$file"; then
			echo "$file needs formatting but has unstaged changes; stage or stash them." >&2
			failed=1
			continue
		fi
		cp "$temporary/formatted" "$file"
		git add -- "$file"
		echo "clang-format: formatted $file"
	done
	exit "$failed"
	;;
*)
	echo "usage: tools/clang-format.sh [--check | --staged | --resolve]" >&2
	exit 2
	;;
esac
