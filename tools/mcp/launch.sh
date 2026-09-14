#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd -P)
controller="$repo_root/build/chibi_ctl"
renderer="$repo_root/build/chibi_render_ctl"

if [ ! -x "$controller" ] || [ ! -x "$renderer" ]; then
    if ! command -v make >/dev/null 2>&1; then
        printf '%s\n' 'chibi MCP requires native controllers or make' >&2
        exit 1
    fi
    printf '%s\n' 'chibi MCP building native controllers' >&2
    make -C "$repo_root" build >&2
fi

if [ ! -x "$controller" ] || [ ! -x "$renderer" ]; then
    printf '%s\n' "chibi MCP could not build $controller and $renderer" >&2
    exit 1
fi

if node_bin=$(command -v node 2>/dev/null); then
    :
elif [ -x "/Applications/Codex.app/Contents/Resources/cua_node/bin/node" ]; then
    node_bin="/Applications/Codex.app/Contents/Resources/cua_node/bin/node"
else
    printf '%s\n' 'chibi MCP requires Node.js 20 or newer' >&2
    exit 1
fi

printf '%s\n' "chibi MCP starting from $repo_root" >&2
CHIBI_ROOT="$repo_root" \
CHIBI_CTL="$controller" \
CHIBI_RENDER_CTL="$renderer" \
exec "$node_bin" "$repo_root/tools/mcp/server.mjs"
