cmake_minimum_required(VERSION 3.20)
project(cbehavior_assets_reflect LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBCLANG_ROOT "" CACHE PATH "LLVM installation prefix")
file(GLOB LLVM_DISTRO_ROOTS /usr/lib/llvm-*)
list(SORT LLVM_DISTRO_ROOTS COMPARE NATURAL ORDER DESCENDING)
set(LLVM_ROOTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm ${LLVM_DISTRO_ROOTS})
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${LLVM_ROOTS}
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang libclang clang-18 libclang-18
    HINTS ${LLVM_ROOTS}
    PATH_SUFFIXES lib)
if(NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY)
    message(FATAL_ERROR "libclang not found")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/resource.cmake")
chibi_reflect_resource_include(LIBCLANG_RESOURCE_INCLUDE_DIR)

set(CHIBI_SDK_BUILD_COMPILER OFF)
set(CHIBI_ENABLE_SJLIT OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../sdk" sdk EXCLUDE_FROM_ALL)
add_executable(reflect reflect.c collect.c identity.c emit.c emit_function.c emit_component.c emit_catalog.c)
target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE_DIR})
target_link_libraries(reflect PRIVATE ${LIBCLANG_LIBRARY} Chibi::capabilities Chibi::execution_contract)

target_compile_definitions(reflect PRIVATE
    CHIBI_REFLECT_RESOURCE_INCLUDE_DIR="${LIBCLANG_RESOURCE_INCLUDE_DIR}")
