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
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(GEN_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/demo_registry.c)
add_custom_command(
    OUTPUT ${GEN_REGISTRY}
    COMMAND $<TARGET_FILE:reflect>
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_nodes.c
            ${GEN_REGISTRY}
            DEMO
    DEPENDS reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_nodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/interface.h
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/vec3.h
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/transform.h
            ${CMAKE_CURRENT_SOURCE_DIR}/core/annotate.h
    COMMENT "Generating the demo reflection pack"
    VERBATIM
)

set(BASE_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/base_registry.c)
# Pack order is the catalog and lookup order.
set(BASE_REGISTRY_PACKS
    DEMO_REGISTRY_PACK
    CORE_EXTRA_PACK
    CORE_FLOW_PACK
    CORE_ARRAY_PACK
    CORE_STATE_PACK
    CORE_SWITCH_PACK
    CORE_REROUTE_PACK
    CORE_SET_MAP_PACK
    CORE_STRUCT_PACK
)
add_custom_command(
    OUTPUT ${BASE_REGISTRY}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
            ${BASE_REGISTRY}
            ${BASE_REGISTRY_PACKS}
    DEPENDS ${GEN_REGISTRY}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
    COMMENT "Composing the base registry pack list"
    VERBATIM
)

# --- core static lib (compiles the generated reflection pack into it) ---
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.c
)
add_library(core STATIC
    ${CORE_SOURCES}
    third_party/yyjson/yyjson.c
    third_party/fmt/fmt.c
    ${GEN_REGISTRY}
)
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt
)

# --- headless ECS consumer module -------------------------------------------
file(GLOB_RECURSE ECS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.c
)
add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecs PUBLIC core)

# --- generated game component pack ------------------------------------------
set(GAME_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/game_registry.c)
add_custom_command(
    OUTPUT ${GAME_REGISTRY}
    COMMAND $<TARGET_FILE:reflect>
            ${CMAKE_CURRENT_SOURCE_DIR}/game/game_nodes.c
            ${GAME_REGISTRY}
            GAME
    DEPENDS reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/game/game_nodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/game/interface.h
            ${CMAKE_CURRENT_SOURCE_DIR}/game/components.h
            ${CMAKE_CURRENT_SOURCE_DIR}/core/annotate.h
    COMMENT "Generating the game reflection pack"
    VERBATIM
)
add_library(game STATIC game/access.c game/prototype.c game/behavior.c game/live.c game/hot_reload.c game/scene.c ${GAME_REGISTRY})
target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/game
)
target_link_libraries(game PUBLIC ecs core)

# --- host frame loop and optional viewport surface --------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.c
)
add_library(engine STATIC ${ENGINE_SOURCES})
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine PUBLIC game ecs core cute)
target_compile_definitions(engine PRIVATE ENGINE_CUTE_AVAILABLE=1 ENGINE_IMGUI_AVAILABLE=1)
target_compile_options(engine PRIVATE -Wno-macro-redefined)

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
add_executable(app ${APP_SOURCES} ${BASE_REGISTRY})
target_link_libraries(app PRIVATE editor engine demo cute)
set_target_properties(app PROPERTIES LINKER_LANGUAGE CXX)

# --- self-check: builds and evaluates small graphs, asserts outputs ---
add_executable(self_check test/self_check.c app/demo_graph.c app/stacktrace.c editor/preview_gate.c ${BASE_REGISTRY})
target_include_directories(self_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(self_check PRIVATE engine game ecs demo)

# --- activation benchmark: headless per-instance event dispatch -------------
add_executable(bench_activation test/bench_activation.c ${BASE_REGISTRY})
target_include_directories(bench_activation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_activation PRIVATE core demo)

add_executable(scene_benchmark test/scene_benchmark.c ${BASE_REGISTRY})
target_include_directories(scene_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scene_benchmark PRIVATE game demo)

# --- integration: headless pico_unit regression suite for the editor<->core
# interaction path (test/integration.c). Core + demo + pico_unit only; no
# cute/ImGui, so the suite runs without a window host.
add_executable(integration test/integration.c app/demo_graph.c editor/arrange.c ${BASE_REGISTRY})
target_include_directories(integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pico_headers
)
target_link_libraries(integration PRIVATE core demo)

# --- consumer registry pack fixture ---------------------------------------
set(CONSUMER_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/consumer_registry.c)
# Pack order is the catalog and lookup order.
set(CONSUMER_REGISTRY_PACKS ${BASE_REGISTRY_PACKS} CONSUMER_REGISTRY_PACK)
add_custom_command(
    OUTPUT ${CONSUMER_REGISTRY}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
            ${CONSUMER_REGISTRY}
            ${CONSUMER_REGISTRY_PACKS}
    DEPENDS ${GEN_REGISTRY}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/consumer_pack.c
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
    COMMENT "Composing the consumer registry pack list"
    VERBATIM
)
add_executable(registry_consumer
    test/registry_consumer.c
    test/consumer_pack.c
    ${CONSUMER_REGISTRY}
)
target_include_directories(registry_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(registry_consumer PRIVATE core demo)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/game/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/game/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.h
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
  add_dependencies(ecs format)
  add_dependencies(game format)
  add_dependencies(engine format)
  add_dependencies(demo format)
  add_dependencies(editor format)
  add_dependencies(app format)
  add_dependencies(self_check format)
  add_dependencies(bench_activation format)
  add_dependencies(scene_benchmark format)
  add_dependencies(integration format)
  add_dependencies(registry_consumer format)
  add_dependencies(reflect format)
endif()

file(
    CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
)
