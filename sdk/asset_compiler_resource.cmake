# Select the compiler-produced resource for this target.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(asset_compiler_variant wasm32)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(asset_compiler_variant native64)
else()
    message(FATAL_ERROR "No asset compiler bootstrap resource for this target")
endif()
set(asset_compiler_resource "${CHIBI_SDK_ROOT}/tools/compiler/asset_compiler/bootstrap/${asset_compiler_variant}")
