#!/bin/sh
set -eu

bin_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if node_bin=$(command -v node 2>/dev/null); then
    :
elif [ -x "/Applications/Codex.app/Contents/Resources/cua_node/bin/node" ]; then
    node_bin="/Applications/Codex.app/Contents/Resources/cua_node/bin/node"
else
    printf '%s\n' 'chibi MCP requires Node.js 22 or newer' >&2
    exit 1
fi
if ! "$node_bin" -e 'process.exit(Number(process.versions.node.split(".")[0]) >= 22 ? 0 : 1)'; then
    printf '%s\n' 'chibi MCP requires Node.js 22 or newer' >&2
    exit 1
fi
if [ "${1:-}" = "--setup" ]; then
    shift
    project_root=${1:-${CHIBI_ROOT:-$PWD}}
    if [ "$#" -gt 0 ]; then shift; fi
    exec "$node_bin" "$bin_dir/../share/chibi/mcp/setup-codex.mjs" "$project_root" --engine "$bin_dir/.." "$@"
fi
exec "$node_bin" "$bin_dir/../share/chibi/mcp/package-launch.mjs" "$@"
