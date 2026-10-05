# Bash activation; source this file or use tools/cloud/bin/start_skill.
_chibi_cloud_activate() {
    local repo prefix item
    repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
    prefix=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
    [[ $prefix == /* ]] || { echo 'CHIBI_CLOUD_TOOLS must be an absolute path.' >&2; return 1; }
    [[ ! $prefix =~ [[:space:]] ]] || { echo 'CHIBI_CLOUD_TOOLS cannot contain whitespace: Make toolchain flags require a single path.' >&2; return 1; }
    if [[ ! -x $prefix/root/usr/bin/clang-19 || ! -x $prefix/python/bin/cmake ]]; then
        printf 'Cloud toolchain missing at %s. Run: bash %s/tools/cloud/setup.sh\n' "$prefix" "$repo" >&2
        return 1
    fi
    # Keep existing tool choices. Missing tools fall back to the verified prefix;
    # preflight rejects incompatible existing tools instead of shadowing them.
    if [[ ${CHIBI_CLOUD_KEEP_PATH-} != 1 ]]; then
        for item in "$repo/tools/cloud/bin" "$prefix/bin" "$prefix/python/bin" "$prefix/root/usr/share/emscripten" "$prefix/root/usr/bin" "$prefix/root/usr/lib/llvm-19/bin"; do
            case :$PATH: in *:"$item":*) ;; *) PATH+=":$item" ;; esac
        done
        export PATH
    fi
    # Preserve explicit search paths, including empty overrides.
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH-$prefix/root/usr/lib/x86_64-linux-gnu:$prefix/root/usr/lib/llvm-19/lib}"
    export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH-$prefix/root/usr}"
    export PKG_CONFIG_PATH="${PKG_CONFIG_PATH-$prefix/root/usr/lib/x86_64-linux-gnu/pkgconfig:$prefix/root/usr/share/pkgconfig}"
    if [[ ! ${CMAKE_TOOLCHAIN_FILE+x} ]]; then
        export CC="${CC-$prefix/root/usr/bin/clang-19}" CXX="${CXX-$prefix/root/usr/bin/clang++-19}"
    fi
    export PKG_CONFIG_SYSROOT_DIR="${PKG_CONFIG_SYSROOT_DIR-$prefix/root}"
    export XDG_CACHE_HOME="${XDG_CACHE_HOME-$prefix/cache}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR-$prefix/runtime}"
    export EM_CONFIG="${EM_CONFIG-$prefix/emscripten-config}" EM_CACHE="${EM_CACHE-$prefix/emscripten-cache}"
    export ASAN_SYMBOLIZER_PATH="${ASAN_SYMBOLIZER_PATH-$prefix/root/usr/lib/llvm-19/bin/llvm-symbolizer}"
    export VK_DRIVER_FILES="${VK_DRIVER_FILES-$prefix/root/usr/share/vulkan/icd.d/lvp_icd.json}"
    export CHIBI_DEPENDENCY_PACKAGES="${CHIBI_DEPENDENCY_PACKAGES-0}"
    export CFLAGS="${CFLAGS--Wno-error=sign-compare}"
    export NATIVE_TOOLCHAIN_FLAGS="${NATIVE_TOOLCHAIN_FLAGS--DLIBCLANG_ROOT=$prefix/root/usr/lib/llvm-19}"
}
_chibi_cloud_activate
_chibi_activation_status=$?
unset -f _chibi_cloud_activate
return "$_chibi_activation_status"
