# Install script for directory: /Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/sdk

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/opt/homebrew/opt/llvm/bin/llvm-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/chibi" TYPE FILE FILES
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/LICENSE.md"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/COPYING"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/COPYING.LESSER"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/COPYING.EXAMPLES"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/chibi/docs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/docs/distribution-licensing.md")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/fmt" TYPE FILE FILES
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/fmt/LICENSE"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/fmt/README.md"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/ChibiVendorDependencies.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/arena.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/xalloc.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/rcss_map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/text_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/enum.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/cos_sin.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/file_io.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/support/support/math.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/value.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/text.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/pointer.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/query.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/schema.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/type.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/record.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/copy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/trace.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/union.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/annotate.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/state_machine.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/value_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/scalar.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/types/types/value_wire.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_values/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/asset_values/support/asset_value.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_values/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/asset_values/support/asset_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/value_document/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/value_document/types/value_document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/capabilities/capabilities/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/capabilities/capabilities/definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/capabilities/capabilities/catalog_snapshot.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/host.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/status.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/service.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/log.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/instruction.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/operation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/state.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/function.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/managed.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/diagnostic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/backend.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/component.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/scalar.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/borrow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/request.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/source_debug.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/value_kind.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/arguments.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution_contract/execution/random.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/native_capabilities/runtime/native_capabilities.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/native_capabilities/runtime/native_definitions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/native_capabilities/runtime/type_definitions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ecs/ecs/ecs.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ecs/ecs/execution.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ecs/ecs/component_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/events/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/events/runtime/events.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/tasks/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/tasks/runtime/tasks.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_action/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/input_action/runtime/input_action.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/function_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/update.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/module.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/module_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/intrinsics.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/map_intrinsics.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/delay.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/task.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/integer_report.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/ecs_reader.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/component_write.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/execution/execution/native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_sljit.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/216c7cf5c4fdb5a5a43d18732a9ea78f0b45c416bdf4a5494af0997e69c1b6e6/lib/chibi-vendor/libchibi_vendor_sljit.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/sljit/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/216c7cf5c4fdb5a5a43d18732a9ea78f0b45c416bdf4a5494af0997e69c1b6e6/include/chibi/vendor/sljit/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/216c7cf5c4fdb5a5a43d18732a9ea78f0b45c416bdf4a5494af0997e69c1b6e6/share/chibi/licenses/vendor/sljit")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/core_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/core_capabilities/runtime/core_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/array_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/array_capabilities/runtime/arrays.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/array_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/array_capabilities/runtime/arrays_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_capabilities/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/field_capabilities/runtime/fields/native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_capabilities/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/field_capabilities/runtime/fields/definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/state_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/state_capabilities/runtime/state_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/selection_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/selection_capabilities/runtime/selection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/selection_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/selection_capabilities/runtime/selection_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/set_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/set_capabilities/runtime/sets.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/set_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/set_capabilities/runtime/sets_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/comparison_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/comparison_capabilities/runtime/comparisons.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/comparison_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/comparison_capabilities/runtime/comparison_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/conversion_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/conversion_capabilities/runtime/conversions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/conversion_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/conversion_capabilities/runtime/conversion_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/text_capabilities/runtime/text_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/text_capabilities/runtime/text_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/core/strings" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/packages/core/strings/Strings.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/markdown" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/docs/markdown/Markdown.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/site" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/docs/site/Site.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/reference" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/docs/reference/Reference.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/grammar" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/docs/grammar/Grammar.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/compiler_syntax" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/compiler/compiler_syntax/Syntax.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/docs/syntax_diagnostics" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/compiler/syntax_diagnostics/Diagnostics.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/host_native_catalog/host" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/host_native_catalog/host/native_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/behavior_runtime/runtime/behaviors/source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/behavior_runtime/runtime/behaviors/package.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/behavior_runtime/runtime/behaviors/module_set.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/behavior_runtime/runtime/event_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_prepared/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/native_prepared/runtime/native_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/scene_runtime/runtime/scenes/scene.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/scene_runtime/runtime/scenes/physics_settings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/scene_runtime/runtime/scenes/prototypes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/scene_runtime/runtime/scenes/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scheduling/runtime/systems" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/scheduling/runtime/systems/scheduler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/fields/runtime/fields/ir.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/fields/runtime/fields/program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/fields/runtime/fields/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_catalog/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/field_catalog/runtime/material_field.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_catalog/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/field_catalog/runtime/material_field_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_documents/runtime/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ui_documents/runtime/ui/documents.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_documents/runtime/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ui_documents/runtime/ui/font.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_document_values/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/ui_document_values/ui/document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_search/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/text_search/support/text_search.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_search/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/text_search/support/text_rank.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/bake.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/volume.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/detail.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/effects.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/image.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/shader_runtime/runtime/shaders/source_debug.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/model_runtime/runtime/models/model_program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/model_runtime/runtime/models/model_inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/model_runtime/runtime/models/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/image_catalog/runtime/images/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/image_catalog/runtime/images/table.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/public/image_catalog/runtime/images/sky.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi" TYPE FILE RENAME "engine.json" FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/chibi_engine_metadata.json")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_support.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_types.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_ecs.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_execution.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_core_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_array_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_field_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_state_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_selection_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_behavior_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_events.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_asset_values.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_value_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_native_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_scene_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_scheduling.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_fields.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_field_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_set_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_host_native_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_native_prepared.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_comparison_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_conversion_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_text_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_fmt.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_ui_documents.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_ui_document_values.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_text_search.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_shader_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_model_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/libchibi_image_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets.cmake"
         "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/ChibiConfig.cmake"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/ChibiConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-tools-asan/sdk/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
