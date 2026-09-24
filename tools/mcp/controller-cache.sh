#!/bin/sh
# Shares chibi_ctl across worktrees, keyed by the committed native sources.
#   controller-cache.sh             print the cache path; nothing when native sources are dirty
#   controller-cache.sh publish BIN copy BIN into the cache when its entry is missing
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
native_paths="CMakeLists.txt cmake include src third_party tools/chibi_ctl tools/reflect"

cache_path() {
    cd "$repo_root"
    # Uncommitted native edits have no shareable key.
    [ -z "$(git status --porcelain -- $native_paths)" ] || return 0
    key=$(git ls-tree HEAD -- $native_paths | git hash-object --stdin)
    printf '%s\n' "$(git rev-parse --path-format=absolute --git-common-dir)/chibi-mcp/$key/chibi_ctl"
}

cache=$(cache_path)
if [ "${1:-}" != publish ]; then
    [ -z "$cache" ] || printf '%s\n' "$cache"
    exit 0
fi

binary=$2
if [ -z "$cache" ] || [ -x "$cache" ] || [ ! -x "$binary" ]; then
    exit 0
fi
mkdir -p "$(dirname -- "$cache")"
cp "$binary" "$cache.$$"
mv -f "$cache.$$" "$cache"
printf '%s\n' "chibi MCP published $cache" >&2

# Keep the newest entries; each is a full controller binary.
cd "$(dirname -- "$(dirname -- "$cache")")"
ls -t | tail -n +9 | while read -r stale; do rm -rf "$stale"; done
