cmake_minimum_required(VERSION 3.20)
project(c-blueprints LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- cute_framework (window host + dcimgui), pinned commit ---
include(FetchContent)
set(CF_FRAMEWORK_STATIC ON CACHE BOOL "" FORCE)
set(CF_CUTE_SHADERC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cute_framework
    GIT_REPOSITORY https://github.com/RandyGaul/cute_framework.git
    GIT_TAG        ad13c5ea7262fdede97a3a39c77eef3da286ebcf
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(cute_framework)

# libclang (build tool only; runtime stays dependency-free).
set(LIBCLANG_ROOT "" CACHE PATH "LLVM installation prefix")
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES lib)
if(NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY)
  message(FATAL_ERROR "libclang not found; set LIBCLANG_ROOT to an LLVM installation")
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# --- embedded editor icon font ---
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(NERD_FONT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/nerd-fonts-symbols-only/SymbolsNerdFont-Regular.ttf)
set(GEN_NERD_FONT ${CMAKE_CURRENT_BINARY_DIR}/generated/editor_nerd_symbols_font.c)
add_custom_command(
    OUTPUT ${GEN_NERD_FONT}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py
            ${NERD_FONT}
            ${GEN_NERD_FONT}
            editor_nerd_symbols_font
    DEPENDS ${NERD_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.py
    COMMENT "Embedding Nerd Fonts symbols"
    VERBATIM
)

# --- reflect tool ---
add_executable(reflect reflect/reflect.c core/xalloc.c)
target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE_DIR})
target_link_libraries(reflect PRIVATE ${LIBCLANG_LIBRARY})
get_filename_component(LIBCLANG_LIBRARY_DIR ${LIBCLANG_LIBRARY} DIRECTORY)
set_target_properties(reflect PROPERTIES BUILD_RPATH ${LIBCLANG_LIBRARY_DIR})

# --- generated registry ---
set(GEN_REGISTRY ${CMAKE_CURRENT_SOURCE_DIR}/generated/registry.c)
add_custom_command(
    OUTPUT ${GEN_REGISTRY}
    COMMAND $<TARGET_FILE:reflect>
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_nodes.c
            ${GEN_REGISTRY}
    DEPENDS reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_nodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/interface.h
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/vec3.h
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/transform.h
            ${CMAKE_CURRENT_SOURCE_DIR}/core/annotate.h
    COMMENT "Generating generated/registry.c"
    VERBATIM
)

# --- core static lib (compiles the generated registry into it) ---
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.c
)
add_library(core STATIC
    ${CORE_SOURCES}
    third_party/yyjson/yyjson.c
    ${GEN_REGISTRY}
)
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
)

# --- demo nodes (real TUs; interface.h declares what Blueprints sees) ---
# demo/demo_nodes.c is reflect input, not a compiled TU, so it stays explicit.
add_library(demo STATIC
    demo/vec3.c
    demo/math.c
)
target_include_directories(demo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
)

# --- editor canvas (graph model + native dcimgui widget) ---
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.c
)
add_library(editor STATIC ${EDITOR_SOURCES} ${GEN_NERD_FONT})
target_include_directories(editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor PRIVATE core cute)

# --- app: cute_framework host loop ---
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c
)
add_executable(app ${APP_SOURCES})
target_link_libraries(app PRIVATE editor core demo cute)
set_target_properties(app PROPERTIES LINKER_LANGUAGE CXX)

# --- self-check: builds and evaluates small graphs, asserts outputs ---
add_executable(self_check test/self_check.c app/demo_graph.c app/stacktrace.c editor/preview_gate.c)
target_include_directories(self_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(self_check PRIVATE core demo)

# --- clang-format (format runs before every build target) ---
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/demo/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/demo/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/reflect/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reflect/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.h
  )
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} --style=file -i ${FORMAT_SOURCES}
    COMMENT "Formatting C sources"
    VERBATIM)
  add_custom_target(check-format
    COMMAND ${CLANG_FORMAT} --dry-run --Werror --style=file ${FORMAT_SOURCES}
    COMMENT "Checking C source formatting"
    VERBATIM)
  add_dependencies(core format)
  add_dependencies(demo format)
  add_dependencies(editor format)
  add_dependencies(app format)
  add_dependencies(self_check format)
  add_dependencies(reflect format)
endif()

file(
    CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
)
