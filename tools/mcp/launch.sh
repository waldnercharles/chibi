#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd -P)
default_project_root="$repo_root/projects/arena2d"
project_root=${CHIBI_ROOT:-}
if [ -z "$project_root" ]; then
    if [ -f "$default_project_root/project.json" ]; then
        project_root="$default_project_root"
    else
        project_root="$repo_root"
    fi
fi
case "$project_root" in
    /*) ;;
    *) project_root="$repo_root/$project_root" ;;
esac
if ! project_root=$(CDPATH= cd -- "$project_root" && pwd -P); then
    printf '%s\n' "chibi MCP project root is not a directory: $project_root" >&2
    exit 1
fi

if node_bin=$(command -v node 2>/dev/null); then
    :
elif [ -x "/Applications/Codex.app/Contents/Resources/cua_node/bin/node" ]; then
    node_bin="/Applications/Codex.app/Contents/Resources/cua_node/bin/node"
else
    printf '%s\n' 'chibi MCP requires Node.js 22 or newer' >&2
    exit 1
fi

# The server answers the handshake immediately and builds chibi_ctl on first use.
printf '%s\n' "chibi MCP starting from project root $project_root" >&2
CHIBI_ROOT="$project_root" exec "$node_bin" "$repo_root/tools/mcp/server.mjs"
