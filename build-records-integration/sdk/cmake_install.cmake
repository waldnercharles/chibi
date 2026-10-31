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
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/ChibiVendorDependencies.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/arena.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/xalloc.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/rcss_map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/text_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/enum.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/cos_sin.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/file_io.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/support/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/support/support/math.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/value.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/text.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/pointer.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/query.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/schema.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/type.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/record.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/copy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/array.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/trace.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/union.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/annotate.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/state_machine.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/value_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/scalar.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/types/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/types/types/value_wire.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_values/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_values/support/asset_value.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_values/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_values/support/asset_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/value_document/types" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/value_document/types/value_document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/capabilities/capabilities/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/capabilities/capabilities/definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/capabilities/capabilities" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/capabilities/capabilities/catalog_snapshot.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/host.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/status.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/service.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/log.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/instruction.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/operation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/state.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/function.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/managed.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/diagnostic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/backend.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/component.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/scalar.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/borrow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/request.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/source_debug.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/value_kind.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/arguments.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution_contract/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution_contract/execution/random.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_capabilities/runtime/native_capabilities.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_capabilities/runtime/native_definitions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_capabilities/runtime/type_definitions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ecs/ecs/ecs.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ecs/ecs/execution.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ecs/ecs" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ecs/ecs/component_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/events/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/events/runtime/events.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/tasks/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/tasks/runtime/tasks.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_action/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_action/runtime/input_action.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/function_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/update.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/module.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/module_builder.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/intrinsics.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/map_intrinsics.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/delay.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/task.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/integer_report.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/ecs_reader.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/component_write.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/execution/execution" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/execution/execution/native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_sljit.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_sljit.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/sljit/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/sljit/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/sljit")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/core_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/core_capabilities/runtime/core_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/array_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/array_capabilities/runtime/arrays.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/array_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/array_capabilities/runtime/arrays_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_capabilities/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/field_capabilities/runtime/fields/native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_capabilities/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/field_capabilities/runtime/fields/definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/state_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/state_capabilities/runtime/state_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/selection_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/selection_capabilities/runtime/selection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/selection_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/selection_capabilities/runtime/selection_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/set_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/set_capabilities/runtime/sets.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/set_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/set_capabilities/runtime/sets_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/comparison_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/comparison_capabilities/runtime/comparisons.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/comparison_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/comparison_capabilities/runtime/comparison_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/conversion_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/conversion_capabilities/runtime/conversions.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/conversion_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/conversion_capabilities/runtime/conversion_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_capabilities/runtime/text_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_capabilities/runtime/text_definition.h")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/host_native_catalog/host" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/host_native_catalog/host/native_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/behavior_runtime/runtime/behaviors/source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/behavior_runtime/runtime/behaviors/package.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/behavior_runtime/runtime/behaviors/module_set.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/behavior_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/behavior_runtime/runtime/event_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_prepared/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_prepared/runtime/native_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_runtime/runtime/scenes/scene.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_runtime/runtime/scenes/physics_settings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_runtime/runtime/scenes/prototypes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_runtime/runtime/scenes/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scheduling/runtime/systems" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scheduling/runtime/systems/scheduler.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/fields/runtime/fields/ir.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/fields/runtime/fields/program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/fields/runtime/fields" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/fields/runtime/fields/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_catalog/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/field_catalog/runtime/material_field.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/field_catalog/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/field_catalog/runtime/material_field_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_documents/runtime/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_documents/runtime/ui/documents.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_documents/runtime/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_documents/runtime/ui/font.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_document_values/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_document_values/ui/document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_search/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_search/support/text_search.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_search/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_search/support/text_rank.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/builtin_fonts/runtime/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/builtin_fonts/runtime/ui/builtin_fonts.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/fonts" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/fonts/" FILES_MATCHING REGEX "/[^/]*license[^/]*$" REGEX "/[^/]*ofl[^/]*$" REGEX "/[^/]*copying[^/]*$")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/fonts/nerd-fonts-symbols-only" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/fonts/nerd-fonts-symbols-only/NOTICES.md")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/fonts/nerd-fonts-symbols-only" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/fonts/nerd-fonts-symbols-only/licenses")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/json" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/json/yyjson.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/yyjson" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/yyjson/LICENSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_json/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_json/support/asset_json.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/ckdl" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/third_party/ckdl/COPYING")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_text/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_text/support/asset_text.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_tree_sitter.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_tree_sitter.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/tree_sitter/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/tree_sitter/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/tree_sitter")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/language/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math/cute_defines.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math/cute_user_config.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math/cute_math.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math/cute_math3d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/cute-math" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/source/cute_framework/LICENSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vector_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/vector_capabilities/runtime/vectors.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vector_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/vector_capabilities/runtime/vectors_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math_capabilities/runtime/math_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/math_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/math_capabilities/runtime/math_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/arithmetic_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/arithmetic_capabilities/runtime/arithmetic.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/arithmetic_capabilities/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/arithmetic_capabilities/runtime/arithmetic_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_types/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_types/runtime/components.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_types/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_types/runtime/component_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/tilemap_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/tilemap_runtime/runtime/tilemap.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/spatial_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/spatial_runtime/runtime/visual_transform.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/spatial_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/spatial_runtime/runtime/visual_parts.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/spatial_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/spatial_runtime/runtime/transform_propagation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/native_bindings/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/native_bindings/runtime/generated_capabilities.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/entities.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/entities_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/entities_source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/entities_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/entity_query.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/tilemap_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/scene_flow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/entity_runtime/runtime/behaviors" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/entity_runtime/runtime/behaviors/scene_flow_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/audio_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/audio_runtime/runtime/audio.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/audio_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/audio_runtime/runtime/audio_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/audio_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/audio_runtime/runtime/audio_analysis.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/url_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/url_runtime/runtime/url.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/url_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/url_runtime/runtime/url_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/particle_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/particle_runtime/runtime/particles.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/particle_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/particle_runtime/runtime/particles_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/particle_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/particle_runtime/runtime/particles_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/save_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/save_runtime/runtime/save.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/save_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/save_runtime/runtime/save_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/save_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/save_runtime/runtime/save_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_runtime/runtime/ui.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_runtime/runtime/ui_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_runtime/runtime/ui_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_runtime/runtime/engine_input.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_runtime/runtime/input_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_runtime/runtime/input_timeline.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_runtime/runtime/source_input.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/materials.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/materials_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/materials_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_mask.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_terrain.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_fluid.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_destruction.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_stream.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_debris.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/material_checkpoint.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/terrain_storage.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/terrain_storage_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/geometry/material_surface_mesh.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/geometry/distance_transform.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/material_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/material_runtime/runtime/geometry/terrain_volume.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_box2d.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_box2d.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/box2d/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/box2d/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/box2d")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_box3d.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_box3d.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/box3d/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/box3d/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/box3d")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/task_pool_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/task_pool_runtime/runtime/task_pool.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_enkits.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_enkits.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/enkits/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/enkits/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/enkits")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics_runtime/runtime/scenes/physics.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics_runtime/runtime/scenes/physics_settings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics_runtime/runtime/physics_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics_runtime/runtime/physics_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics3d_runtime/runtime/scenes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics3d_runtime/runtime/scenes/physics3d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics3d_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics3d_runtime/runtime/physics3d_native.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/physics3d_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/physics3d_runtime/runtime/physics3d_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visibility_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visibility_runtime/runtime/visibility.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visibility_runtime/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visibility_runtime/runtime/visibility_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/engine/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/engine/runtime/engine.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/engine/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/engine/runtime/particle_emitters.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/engine/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/engine/runtime/world.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/declared_components/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/declared_components/runtime/declared_components.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/player/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/player/runtime/player.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/player/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/player/runtime/player_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/host_native_default/host" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/host_native_default/host/native_default.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/bake.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/volume.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/detail.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/effects.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/image.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_runtime/runtime/shaders" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_runtime/runtime/shaders/source_debug.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_runtime/runtime/models/model_program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_runtime/runtime/models/model_inspection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_runtime/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_runtime/runtime/models/prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/mesh_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/mesh_runtime/runtime/geometry/mesh_asset.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/mesh_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/mesh_runtime/runtime/geometry/mesh_pick.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/mesh_runtime/runtime/geometry" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/mesh_runtime/runtime/geometry/mesh_prepared.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_bake/runtime/models" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_bake/runtime/models/model_bake.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/image_catalog/runtime/images/catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/image_catalog/runtime/images/table.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_catalog/runtime/images" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/image_catalog/runtime/images/sky.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/image_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/image_preparation/authoring/image_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/shader_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/shader_catalog_records.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/shader_values.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/scene3d_parts.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/scene3d_parts_table.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/catalog_publication.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/visual_catalog/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/visual_catalog/render/shader_parameters.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_cute.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_cute.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/cute/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/cute/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/cute/2" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/cute/2/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/cute/3" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/cute/3/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/cute/4" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/cute/4/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/cute/5" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/cute/5/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/cute")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_physfs_static.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_physfs_static.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/physfs_static/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/physfs_static/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/physfs_static")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_SDL3_static.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_SDL3_static.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/SDL3_Headers/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/SDL3_Headers/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/SDL3_Headers/2" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/SDL3_Headers/2/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/SDL3_Headers")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_rmlui_core.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_rmlui_core.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/rmlui_core/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/rmlui_core/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/rmlui_core")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_freetype.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_freetype.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/freetype/1" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/freetype/1/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/vendor/freetype/2" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/include/chibi/vendor/freetype/2/" FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/[^/]*\\.hpp$" REGEX "/[^/]*\\.inl$" REGEX "/bindings$" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/freetype")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/platform/platform" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/platform/platform/cute.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/platform/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/platform/support/resources.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/rml.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/rml_backend.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/rml_gpu.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/iframe.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/iframe_profile.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/json_items.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/motion.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/support/ui_style.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/support/number_expr.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui/controls" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/controls/controls.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/popover.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/drag.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/list.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/focus_within.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/autofocus.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui/ui" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui/ui/input_feel.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/appearance_geometry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/audio.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/audio_input.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/audio_web.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/device_controls.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/draw_textures.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/field_image.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/fluid_raster.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/fog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/fullscreen.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/geometry_2d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/gpu_clock.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/gpu_timing.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/grid_2d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/material.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/material_height.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/material_lod.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/material_mesh.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/material_raster.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/mesh_material.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/mesh_projection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/mesh_surface.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/post_2d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/radiance.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/reflections.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_cascades.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_geometry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_gi.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_influence.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_light_shadow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_mesh_cache.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_occlusion.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_particles.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_post.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_shadow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_sky.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_terrain.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_terrain_volume.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_trail.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_visual.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene3d_volume.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/scene_renderer.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/screenshot.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_dir.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_fragment.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_images.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_preview.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/shader_publish.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/skip.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/spatial_groups.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/spot_lights.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/ui.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/ui_loader.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/view.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/view_2d.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/render" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/render/viewport.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/support/file_watch.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/render/runtime" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/render/runtime/camera.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/shaders" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/src/render/shaders/")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi-player")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-player" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-player")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-player")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/tools" TYPE PROGRAM FILES
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/package_sdk.py"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/package_player.py"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/package_studio.py"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/graph.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/behavior_asset.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/local.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/pin_conv.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/registry.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/value_copy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/node.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_model/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_model/graph/model/pin.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/array_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/set_map_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/constant.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/optimize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/core_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/fmt_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/math_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/compare_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/vector_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/state_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/align.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/arrange.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/arrange_path.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/arrange_score.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/asset_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/describe.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/expand.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/find.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/graph_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/layout.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/linked.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/local_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/authoring/pin_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/document/events.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/document/extract.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/document/graph_signature.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/document/library.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/document/serialize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/model/reactive.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/model" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/model/reactive_presentation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/component_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/conv_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/behavior_expression.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/compare_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/math_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/compare_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/vector_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/state_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/vector_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_authoring/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_authoring/graph/nodes/state_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/compiler" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/compiler/validation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/compiler" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/compiler/field_program.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/compiler" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/compiler/material_field.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/document" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/document/problems.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/shader" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/shader/shader_graph.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/nodes/field_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/modeling" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/modeling/model_compile.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/modeling" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/modeling/model_authoring.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_compilation/graph/modeling" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_compilation/graph/modeling/model_profile_paths.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/studio_capabilities/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/studio_capabilities/authoring/studio_schema.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/studio_capabilities/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/studio_capabilities/authoring/studio_definition.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/studio_graph/graph/nodes" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/studio_graph/graph/nodes/studio_nodes.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/studio_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/studio_catalog/authoring/studio_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/component_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/component_presentation/authoring/component_presentation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/input_map/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/input_map/assets/input_map.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_document/assets/project.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_document/assets/project_migrate.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/reference_index/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/reference_index/authoring/reference_index.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/prototype_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/prototype_document/assets/prototype.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_document/assets/scene_document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_document/assets/physics_settings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_source/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_source/support/file_watch.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_source/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_source/support/code_lex.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_source/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_source/support/text_file.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/text_source/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/text_source/support/text_overlay.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_document/assets/ui_document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_document/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_document/support/rcss.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_tree_sitter_css.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_tree_sitter_css.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/tree_sitter_css")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/chibi-vendor" TYPE FILE RENAME "libchibi_vendor_tree_sitter_html.a" FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/lib/chibi-vendor/libchibi_vendor_tree_sitter_html.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/licenses/vendor" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/chibi/.git/chibi/third_party/602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b/share/chibi/licenses/vendor/tree_sitter_html")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_authoring/assets/ui_source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/ui_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/ui_authoring/assets/ui_events.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/compiler/language" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/compiler/language/lower.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_link.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_navigation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_prototype_flow.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_rename.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_intelligence.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_declaration.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/native_bindings.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_literal.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/authoring/authoring/source_references.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/language_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_collection.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_asset.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_prototype_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_packages.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_package_io.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_package_snapshot.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_package_files.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_package_exports.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/source_package_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_catalog/authoring/package_tests.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_presentation/authoring/graph_expression.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_presentation/authoring/presentation_schema.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_presentation/authoring/source_presentation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_presentation/authoring/source_edit.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/source_presentation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/source_presentation/authoring/source_minimize.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/graph_catalog/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/graph_catalog/authoring/graph_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_preparation/authoring/shader_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_preparation/authoring/parts_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_preparation/authoring/mesh_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/shader_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/shader_preparation/authoring/visual_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/component_declarations.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/project_open.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/project_compile.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/renderer_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/project_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/prototype_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/material_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/scene_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/sound_prepare.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/source_graph_library.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_compilation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_compilation/authoring/source_costs.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi-compile")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-compile" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-compile")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi-compile")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_authoring/authoring/scene_hierarchy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scene_authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scene_authoring/authoring/scene_snapshot.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/call_hierarchy.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/authoring/native_prototype.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/prototype_instance.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/field_parameters.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/new_asset.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/asset_source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/reference_adapters.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/assets/refactor.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/asset_authoring/support" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/asset_authoring/support/code_completion.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_authoring/assets/model_source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_authoring/assets/model_validation.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_authoring/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_authoring/assets/model_export.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_import/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_import/assets/model_import.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/model_import/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/model_import/assets/standard_material.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/scenario_document/assets" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/scenario_document/assets/scenario_document.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_format/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_format/authoring/project_format.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/preview.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/preview_source.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/ui_loader.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/ui_check.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/render_preview.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/preview_runtime.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/prototype_preview.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/behavior_loader.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/source_apply.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/studio_project.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/ui_events_adapter.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/engine_preview.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/project_preparation/authoring" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/project_preparation/authoring/application_catalog.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/chibi" TYPE EXECUTABLE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi-build-host")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-build-host" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-build-host")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-build-host")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/tools/native_aot" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/native_aot/" FILES_MATCHING REGEX "/[^/]*\\.chibi$")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/compiler" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/compiler/")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/core" TYPE DIRECTORY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/packages/core/" FILES_MATCHING REGEX "/[^/]*\\.chibi$")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/tools/toolchain" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/toolchain/Build.chibi")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi/tools" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/tools/configure_inputs.py")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/chibi/compiler_host/toolchain" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/public/compiler_host/toolchain/compiler_host.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/chibi" TYPE EXECUTABLE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi-compiler-host")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-compiler-host" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-compiler-host")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/chibi/chibi-compiler-host")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/chibi")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/chibi" TYPE FILE RENAME "engine.json" FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/chibi_engine_metadata.json")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_support.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_support.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_types.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_types.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ecs.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ecs.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_execution.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_execution.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_core_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_core_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_array_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_array_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_field_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_state_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_state_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_selection_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_selection_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_behavior_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_behavior_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_events.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_events.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_asset_values.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_values.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_value_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_value_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_native_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_scene_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_scheduling.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scheduling.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_fields.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fields.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_field_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_field_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_set_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_set_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_host_native_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_native_prepared.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_prepared.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_comparison_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_comparison_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_conversion_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_conversion_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_text_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_fmt.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_fmt.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_documents.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_documents.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_document_values.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document_values.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_text_search.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_search.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_builtin_fonts.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_builtin_fonts.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_builtin_fonts.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_builtin_fonts.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_json.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_json.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_json.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_json.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_asset_json.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_json.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_json.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_json.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_asset_text.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_text.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_text.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_text.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_asset_kdl.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_kdl.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_kdl.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_kdl.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_vector_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_vector_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_vector_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_vector_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_math_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_math_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_math_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_math_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_arithmetic_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_arithmetic_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_arithmetic_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_arithmetic_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_tilemap_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_tilemap_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_tilemap_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_tilemap_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_spatial_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_spatial_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_spatial_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_spatial_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_native_bindings.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_bindings.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_bindings.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_bindings.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_native_types.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_types.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_types.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_native_types.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_entity_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_entity_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_entity_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_entity_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_audio_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_audio_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_audio_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_audio_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_url_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_url_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_url_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_url_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_particle_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_particle_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_particle_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_particle_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_save_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_save_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_save_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_save_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_input_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_material_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_material_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_material_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_material_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_task_pool_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_task_pool_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_task_pool_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_task_pool_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_physics_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_physics3d_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics3d_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics3d_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_physics3d_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_visibility_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visibility_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visibility_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visibility_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_engine.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_engine.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_engine.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_engine.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_declared_components.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_declared_components.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_declared_components.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_declared_components.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_player.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_player.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_player.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_player.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_host_native_default.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_default.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_default.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_host_native_default.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_shader_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_model_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_mesh_runtime.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_mesh_runtime.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_mesh_runtime.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_mesh_runtime.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_model_bake.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_bake.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_bake.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_bake.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_image_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_image_preparation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_preparation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_preparation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_image_preparation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_visual_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visual_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visual_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_visual_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_platform.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_platform.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_platform.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_platform.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_host.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_host.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_host.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_host.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_renderer.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_renderer.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_renderer.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_renderer.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_graph_model.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_model.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_model.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_model.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_graph_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_graph_compilation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_compilation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_compilation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_compilation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_studio_capabilities.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_capabilities.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_capabilities.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_capabilities.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_studio_graph.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_graph.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_graph.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_graph.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_studio_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_studio_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_component_presentation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_component_presentation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_component_presentation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_component_presentation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_input_map.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_map.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_map.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_input_map.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_project_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_reference_index.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_reference_index.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_reference_index.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_reference_index.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_prototype_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_prototype_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_prototype_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_prototype_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_scene_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_text_source.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_source.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_source.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_text_source.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_ui_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_ui_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/language/libchibi_language_syntax.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language_syntax.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language_syntax.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language_syntax.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/language/libchibi_language.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_language.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_compiler.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_source_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_source_presentation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_presentation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_presentation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_source_presentation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_graph_catalog.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_catalog.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_catalog.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_graph_catalog.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_compiler_syntax.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_syntax.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_syntax.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_syntax.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_compiler_json.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_json.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_json.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_json.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_shader_preparation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_preparation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_preparation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_shader_preparation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_project_compilation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_compilation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_compilation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_compilation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_model_import.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_import.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_import.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_import.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_scene_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scene_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_asset_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_asset_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_model_authoring.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_authoring.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_authoring.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_model_authoring.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_scenario_document.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scenario_document.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scenario_document.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_scenario_document.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_project_format.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_format.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_format.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_format.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_project_preparation.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_preparation.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_preparation.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_project_preparation.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/libchibi_compiler_host.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_host.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_host.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libchibi_compiler_host.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi/ChibiTargets.cmake"
         "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets.cmake")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/CMakeFiles/Export/9f18f0f014f8e94da5150280e6fa8c21/ChibiTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Chibi" TYPE FILE FILES
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/ChibiConfig.cmake"
    "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/ChibiConfigVersion.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-records-integration/sdk/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
