cmake_minimum_required(VERSION 3.20)
project(c-blueprints LANGUAGES C CXX)

if(APPLE)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CBLUEPRINT_SDKROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

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

# --- SLJIT backend ----------------------------------------------------------
option(CBLUEPRINT_ENABLE_SJLIT "Build the SLJIT graph JIT backend" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # SLJIT emits native machine code into executable memory. Browsers cannot
    # execute that code from WASM linear memory; web uses bytecode instead.
    set(CBLUEPRINT_ENABLE_SJLIT OFF CACHE BOOL "Build the SLJIT graph JIT backend" FORCE)
endif()
set(CBLUEPRINT_SJLIT_SOURCE_DIR "" CACHE PATH "Use an existing SLJIT checkout")
if(CBLUEPRINT_ENABLE_SJLIT)
    if(CBLUEPRINT_SJLIT_SOURCE_DIR)
        set(SLJIT_SOURCE_DIR ${CBLUEPRINT_SJLIT_SOURCE_DIR})
    else()
        FetchContent_Declare(
            sljit
            GIT_REPOSITORY https://github.com/zherczeg/sljit.git
            GIT_TAG        a0020dd49a21020434ff838fc308626c48cd8f05
            UPDATE_DISCONNECTED TRUE
        )
        FetchContent_GetProperties(sljit)
        if(NOT sljit_POPULATED)
            FetchContent_Populate(sljit)
        endif()
        set(SLJIT_SOURCE_DIR ${sljit_SOURCE_DIR})
    endif()
    add_library(cblueprint_sljit STATIC ${SLJIT_SOURCE_DIR}/sljit_src/sljitLir.c)
    target_include_directories(cblueprint_sljit PUBLIC ${SLJIT_SOURCE_DIR}/sljit_src)
    target_compile_definitions(cblueprint_sljit PRIVATE SLJIT_WX_EXECUTABLE_ALLOCATOR=1)
endif()

# libclang (build tool only; runtime stays dependency-free).
set(LIBCLANG_ROOT "" CACHE PATH "LLVM installation prefix")
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES include)
find_library(LIBCLANG_LIBRARY NAMES clang
    HINTS "${LIBCLANG_ROOT}" /opt/homebrew/opt/llvm /usr/local/opt/llvm
    PATH_SUFFIXES lib)
file(GLOB LIBCLANG_RESOURCE_INCLUDE_DIRS
    "${LIBCLANG_ROOT}/lib/clang/*/include"
    "/opt/homebrew/opt/llvm/lib/clang/*/include"
    "/usr/local/opt/llvm/lib/clang/*/include"
)
list(LENGTH LIBCLANG_RESOURCE_INCLUDE_DIRS LIBCLANG_RESOURCE_COUNT)
if(LIBCLANG_RESOURCE_COUNT)
    list(GET LIBCLANG_RESOURCE_INCLUDE_DIRS 0 LIBCLANG_RESOURCE_INCLUDE_DIR)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND
   (NOT LIBCLANG_INCLUDE_DIR OR NOT LIBCLANG_LIBRARY))
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

# --- reflect tool -----------------------------------------------------------
# Reflection runs during the build, so it must always be a native host
# executable. A cross build (notably Emscripten) cannot link libclang into WASM
# or execute a WASM reflect tool during configuration/build.
set(CBLUEPRINT_REFLECT_EXECUTABLE "" CACHE FILEPATH
    "Native reflect executable used when cross-compiling")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(NOT CBLUEPRINT_REFLECT_EXECUTABLE OR
       NOT EXISTS "${CBLUEPRINT_REFLECT_EXECUTABLE}")
        message(FATAL_ERROR
            "Emscripten builds require CBLUEPRINT_REFLECT_EXECUTABLE set to a native reflect tool")
    endif()
    set(CBLUEPRINT_REFLECT_COMMAND "${CBLUEPRINT_REFLECT_EXECUTABLE}")
    set(CBLUEPRINT_REFLECT_DEPENDENCY "${CBLUEPRINT_REFLECT_EXECUTABLE}")
else()
    add_executable(reflect reflect/reflect.c core/xalloc.c)
    target_include_directories(reflect PRIVATE ${LIBCLANG_INCLUDE_DIR})
    target_link_libraries(reflect PRIVATE ${LIBCLANG_LIBRARY})
    get_filename_component(LIBCLANG_LIBRARY_DIR ${LIBCLANG_LIBRARY} DIRECTORY)
    set_target_properties(reflect PROPERTIES BUILD_RPATH ${LIBCLANG_LIBRARY_DIR})
    set(CBLUEPRINT_REFLECT_COMMAND $<TARGET_FILE:reflect>)
    set(CBLUEPRINT_REFLECT_DEPENDENCY reflect)
endif()

# --- generated registry ---
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(GEN_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/demo_registry.c)
add_custom_command(
    OUTPUT ${GEN_REGISTRY}
    COMMAND ${CBLUEPRINT_REFLECT_COMMAND}
            ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_nodes.c
            ${GEN_REGISTRY}
            DEMO
    DEPENDS ${CBLUEPRINT_REFLECT_DEPENDENCY}
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
if(CBLUEPRINT_ENABLE_SJLIT)
    target_compile_definitions(core PRIVATE CBLUEPRINT_HAVE_SJLIT=1)
    target_link_libraries(core PRIVATE cblueprint_sljit)
else()
    target_compile_definitions(core PRIVATE CBLUEPRINT_HAVE_SJLIT=0)
endif()

# --- headless ECS consumer module -------------------------------------------
file(GLOB_RECURSE ECS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.c
)
add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecs PUBLIC core)

# --- generated game component pack ------------------------------------------
set(GAME_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/game_registry.c)
set(GAME_COMPONENT_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/game/component_ids.tsv)
set(GAME_REFLECT_ARGS --manifest ${GAME_COMPONENT_MANIFEST} --include ${cute_framework_SOURCE_DIR}/include)
if(LIBCLANG_RESOURCE_INCLUDE_DIR)
    list(APPEND GAME_REFLECT_ARGS --include ${LIBCLANG_RESOURCE_INCLUDE_DIR})
endif()
if(CBLUEPRINT_SDKROOT)
    list(APPEND GAME_REFLECT_ARGS --sysroot ${CBLUEPRINT_SDKROOT})
endif()
add_custom_command(
    OUTPUT ${GAME_REGISTRY}
    COMMAND ${CBLUEPRINT_REFLECT_COMMAND}
            ${CMAKE_CURRENT_SOURCE_DIR}/game/game_nodes.c
            ${GAME_REGISTRY}
            GAME
            ${GAME_REFLECT_ARGS}
    DEPENDS ${CBLUEPRINT_REFLECT_DEPENDENCY}
            ${CMAKE_CURRENT_SOURCE_DIR}/game/game_nodes.c
            ${CMAKE_CURRENT_SOURCE_DIR}/game/interface.h
            ${CMAKE_CURRENT_SOURCE_DIR}/game/components.h
            ${CMAKE_CURRENT_SOURCE_DIR}/game/authored_data_types.h
            ${GAME_COMPONENT_MANIFEST}
            ${CMAKE_CURRENT_SOURCE_DIR}/core/annotate.h
    COMMENT "Generating the game reflection pack"
    VERBATIM
)
add_library(game STATIC game/access.c game/system.c game/query.c game/query_editor.c game/system_document.c game/prototype.c game/behavior.c game/live.c game/hot_reload.c game/scene.c game/scene_document.c game/authored_data.c ${GAME_REGISTRY})
target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/game
)
target_link_libraries(game PUBLIC ecs core cute)

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

# --- app: the minimal ECS movement editor -------------------------------
# Legacy scene/behavior sources remain in regression targets below, but the
# shipped app does not compile or expose them.
set(APP_REGISTRY ${CMAKE_CURRENT_BINARY_DIR}/generated/app_registry.c)
set(APP_REGISTRY_PACKS
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
    OUTPUT ${APP_REGISTRY}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
            ${APP_REGISTRY}
            ${APP_REGISTRY_PACKS}
            GAME_REGISTRY_PACK
    DEPENDS ${GAME_REGISTRY}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/compose_registry.py
    COMMENT "Composing the app registry pack list"
    VERBATIM
)
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c
)
list(REMOVE_ITEM APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/app/input_behavior.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/demo_graph.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/movement_authoring.c
)
add_executable(app ${APP_SOURCES} ${APP_REGISTRY})
target_link_libraries(app PRIVATE editor engine game cute)
set_target_properties(app PROPERTIES LINKER_LANGUAGE CXX)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set_target_properties(app PROPERTIES SUFFIX ".html")
    target_link_options(app PRIVATE
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/content@content"
    )
endif()

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

add_executable(system_benchmark test/system_benchmark.c ${APP_REGISTRY})
target_include_directories(system_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(system_benchmark PRIVATE game ecs)

# --- integration: headless pico_unit regression suite for the editor<->core
# interaction path (test/integration.c). Core + demo + pico_unit only; no
# cute/ImGui, so the suite runs without a window host.
add_executable(editor_runtime_check test/editor_runtime_check.c ${BASE_REGISTRY})
target_include_directories(editor_runtime_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_runtime_check PRIVATE editor engine game ecs demo cute)
set_target_properties(editor_runtime_check PROPERTIES LINKER_LANGUAGE CXX)

add_executable(prototype_authoring_check
    test/prototype_authoring_check.c
    app/scene_authoring.c
    ${BASE_REGISTRY}
)
target_include_directories(prototype_authoring_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(prototype_authoring_check PRIVATE editor engine game ecs demo cute)
set_target_properties(prototype_authoring_check PROPERTIES LINKER_LANGUAGE CXX)

add_executable(input_behavior_check test/input_behavior_check.c ${APP_REGISTRY})
target_include_directories(input_behavior_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(input_behavior_check PRIVATE engine game ecs demo cute)
set_target_properties(input_behavior_check PROPERTIES LINKER_LANGUAGE CXX)

add_executable(movement_system_check test/movement_system_check.c ${APP_REGISTRY})
target_include_directories(movement_system_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(movement_system_check PRIVATE game ecs)
set_target_properties(movement_system_check PROPERTIES LINKER_LANGUAGE CXX)

add_executable(query_expression_check test/query_expression_check.c ${APP_REGISTRY})
target_include_directories(query_expression_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(query_expression_check PRIVATE game ecs)
set_target_properties(query_expression_check PROPERTIES LINKER_LANGUAGE CXX)

add_executable(query_benchmark test/query_benchmark.c ${APP_REGISTRY})
target_include_directories(query_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(query_benchmark PRIVATE game ecs)

add_executable(authored_data_check test/authored_data_check.c ${APP_REGISTRY})
target_include_directories(authored_data_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(authored_data_check PRIVATE game ecs)
set_target_properties(query_benchmark PROPERTIES LINKER_LANGUAGE CXX)

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

# --- clang-format (format runs before every build target) -------------------
# The web toolchain's clang-format may not understand the project's native
# configuration. Formatting is a native developer check, not a web dependency.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  find_program(CLANG_FORMAT clang-format)
endif()
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
  add_dependencies(system_benchmark format)
  add_dependencies(integration format)
  add_dependencies(editor_runtime_check format)
  add_dependencies(prototype_authoring_check format)
  add_dependencies(input_behavior_check format)
  add_dependencies(movement_system_check format)
  add_dependencies(query_expression_check format)
  add_dependencies(query_benchmark format)
  add_dependencies(authored_data_check format)
  add_dependencies(registry_consumer format)
  if(TARGET reflect)
    add_dependencies(reflect format)
  endif()
endif()

file(
    CREATE_LINK
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
)
