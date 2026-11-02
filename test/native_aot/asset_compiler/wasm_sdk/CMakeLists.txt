cmake_minimum_required(VERSION 3.24)
project(ChibiWasmSDKConsumer C)
find_package(ChibiAssetCompilerWasm CONFIG REQUIRED)

add_executable(compiler ../Wasm.c)
target_compile_definitions(compiler PRIVATE CHIBI_ASSET_COMPILER_WASM_PUBLIC)
target_link_libraries(compiler PRIVATE Chibi::asset_compiler_wasm)
target_link_options(compiler PRIVATE -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648)

add_executable(compiler_threads ../WasmThreads.c)
target_link_libraries(compiler_threads PRIVATE Chibi::asset_compiler_wasm_threads)
target_link_options(compiler_threads PRIVATE
    -sINITIAL_MEMORY=268435456 -sDEFAULT_PTHREAD_STACK_SIZE=8388608 -sPTHREAD_POOL_SIZE=2)

foreach(target IN ITEMS compiler compiler_threads)
    target_compile_options(${target} PRIVATE -std=c11 -Wall -Wextra -Werror -UNDEBUG)
    target_link_options(${target} PRIVATE -sSTACK_SIZE=8388608 -sENVIRONMENT=node -sNODERAWFS=1)
endforeach()
