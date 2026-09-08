cmake_minimum_required(VERSION 3.20)
project(cbehavior_assets_reflect LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB LLVM_ROOTS /usr/lib/llvm-* /opt/homebrew/opt/llvm)
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${LLVM_ROOTS}
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang
    HINTS ${LLVM_ROOTS}
    PATH_SUFFIXES lib)
if(NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY)
    message(FATAL_ERROR "libclang not found")
endif()

add_executable(reflect reflect.c collect.c identity.c emit.c emit_function.c emit_component.c ../../src/support/xalloc.c)
target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE_DIR} ../.. ../../src)
target_link_libraries(reflect PRIVATE ${LIBCLANG_LIBRARY})
