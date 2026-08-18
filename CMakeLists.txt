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

# libclang (build tool only; runtime stays dependency-free)
set(LIBCLANG_INCLUDE /opt/homebrew/opt/llvm/include)
set(LIBCLANG_LIB /opt/homebrew/opt/llvm/lib)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/generated)

# --- reflect tool ---
add_executable(reflect reflect/reflect.c)
target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE})
target_link_directories(reflect PRIVATE ${LIBCLANG_LIB})
target_link_libraries(reflect PRIVATE clang)
set_target_properties(reflect PROPERTIES BUILD_RPATH ${LIBCLANG_LIB})

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
add_library(core STATIC
    core/core.c
    core/graph.c
    core/eval.c
    core/arena.c
    core/nodes.c
    core/type.c
    core/const_nodes.c
    core/serialize.c
    core/pin_fmt.c
    third_party/yyjson/yyjson.c
    ${GEN_REGISTRY}
)
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
)

# --- demo nodes (real TUs; interface.h declares what Blueprints sees) ---
add_library(demo STATIC
    demo/vec3.c
    demo/math.c
)
target_include_directories(demo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
)

# --- imgui-node-editor C23 port (dcimgui + ckit) ---
add_library(ne STATIC
    editor/ne/ne_canvas.c
    editor/ne/ne.c
    third_party/ckit/ckit.c
)
target_include_directories(ne PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ckit
)
target_link_libraries(ne PUBLIC cute)

# --- editor canvas (bridge: graph model <-> ne) ---
add_library(editor STATIC editor/editor.c editor/eval_view.c)
target_link_libraries(editor PRIVATE core ne)

# --- app: cute_framework host loop ---
add_executable(app app/main.c app/demo_graph.c)
target_link_libraries(app PRIVATE editor core demo cute)
set_target_properties(app PROPERTIES LINKER_LANGUAGE CXX)

# --- self-check: builds and evaluates small graphs, asserts outputs ---
add_executable(self_check test/self_check.c app/demo_graph.c)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/ne/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/ne/*.h
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
  add_dependencies(ne format)
  add_dependencies(app format)
  add_dependencies(self_check format)
  add_dependencies(reflect format)
endif()
