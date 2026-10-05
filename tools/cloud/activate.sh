# Bash activation; source this file or use tools/cloud/bin/start_skill.
_chibi_cloud_activate() {
    local repo prefix item name value
    repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
    prefix=${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}
    [[ $prefix == /* ]] || { echo 'CHIBI_CLOUD_TOOLS must be an absolute path.' >&2; return 1; }
    [[ ! $prefix =~ [[:space:]] ]] || { echo 'CHIBI_CLOUD_TOOLS cannot contain whitespace: Make toolchain flags require a single path.' >&2; return 1; }
    if [[ ! -x $prefix/root/usr/bin/clang-19 || ! -x $prefix/python/bin/cmake ]]; then
        printf 'Cloud toolchain missing at %s. Run: bash %s/tools/cloud/setup.sh\n' "$prefix" "$repo" >&2
        return 1
    fi
    # Remove existing occurrences before prepending: repeated sourcing is stable.
    _chibi_prepend() {
        local name=$1 item=$2 part result=$2
        local -a parts
        IFS=: read -r -a parts <<< "${!name-}"
        for part in "${parts[@]}"; do
            [[ -z $part || $part == "$item" ]] || result+=":$part"
        done
        printf -v "$name" '%s' "$result"
        export "${name?}"
    }
    for item in "$prefix/root/usr/lib/llvm-19/bin" "$prefix/root/usr/bin" "$prefix/root/usr/share/emscripten" "$prefix/python/bin" "$prefix/bin" "$repo/tools/cloud/bin"; do
        _chibi_prepend PATH "$item"
    done
    for name in LD_LIBRARY_PATH; do
        _chibi_prepend "$name" "$prefix/root/usr/lib/llvm-19/lib"
        _chibi_prepend "$name" "$prefix/root/usr/lib/x86_64-linux-gnu"
    done
    # CMake's prefix locates native headers/libraries. Global C/C++ include or
    # LIBRARY_PATH overrides would contaminate Emscripten's target sysroot.
    _chibi_prepend CMAKE_PREFIX_PATH "$prefix/root/usr"
    _chibi_prepend PKG_CONFIG_PATH "$prefix/root/usr/share/pkgconfig"
    _chibi_prepend PKG_CONFIG_PATH "$prefix/root/usr/lib/x86_64-linux-gnu/pkgconfig"
    export CC="$prefix/root/usr/bin/clang-19" CXX="$prefix/root/usr/bin/clang++-19"
    export PKG_CONFIG_SYSROOT_DIR="$prefix/root"
    export XDG_CACHE_HOME="$prefix/cache" XDG_RUNTIME_DIR="$prefix/runtime"
    export EM_CONFIG="$prefix/emscripten-config" EM_CACHE="$prefix/emscripten-cache"
    export ASAN_SYMBOLIZER_PATH="$prefix/root/usr/lib/llvm-19/bin/llvm-symbolizer"
    export VK_DRIVER_FILES="$prefix/root/usr/share/vulkan/icd.d/lvp_icd.json"
    export CHIBI_DEPENDENCY_PACKAGES=0
    export CFLAGS="${CFLAGS:--Wno-error=sign-compare}"
    export NATIVE_TOOLCHAIN_FLAGS="${NATIVE_TOOLCHAIN_FLAGS:--DCMAKE_C_FLAGS=-Wno-error=sign-compare -DLIBCLANG_ROOT=$prefix/root/usr/lib/llvm-19}"
    unset -f _chibi_prepend
}
_chibi_cloud_activate
_chibi_activation_status=$?
unset -f _chibi_cloud_activate
return "$_chibi_activation_status"
