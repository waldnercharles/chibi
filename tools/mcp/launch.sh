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
build_dir=${BUILD_DIR:-build}
case "$build_dir" in
    /*) ;;
    *) build_dir="$repo_root/$build_dir" ;;
esac
controller=${CHIBI_CTL:-"$build_dir/chibi_ctl"}

if [ -n "${CHIBI_CTL:-}" ] && [ ! -x "$controller" ]; then
    printf '%s\n' "chibi MCP controller is not executable: $controller" >&2
    exit 1
fi

if [ ! -x "$controller" ]; then
    if ! command -v make >/dev/null 2>&1; then
        printf '%s\n' 'chibi MCP requires native controllers or make' >&2
        exit 1
    fi
    printf '%s\n' 'chibi MCP building native controllers' >&2
    make -C "$repo_root" build BUILD_DIR="$build_dir" >&2
fi

if [ ! -x "$controller" ]; then
	printf '%s\n' "chibi MCP could not build $controller" >&2
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

printf '%s\n' "chibi MCP starting from project root $project_root" >&2
CHIBI_ROOT="$project_root" \
CHIBI_CTL="$controller" \
exec "$node_bin" "$repo_root/tools/mcp/server.mjs"
