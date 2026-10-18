add_library(chibi_language_syntax STATIC
    syntax.c
    src/parser.c
    src/scanner.c)
add_library(Chibi::language_syntax ALIAS chibi_language_syntax)
set_target_properties(chibi_language_syntax PROPERTIES EXPORT_NAME language_syntax)
chibi_sdk_headers(chibi_language_syntax PUBLIC language/syntax.h)
target_include_directories(chibi_language_syntax PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chibi_language_syntax PUBLIC chibi_support chibi_syntax_runtime)
set_source_files_properties(src/parser.c PROPERTIES COMPILE_OPTIONS "-w")

add_library(chibi_language STATIC semantic.c semantic_context.c identity.c component.c
    semantic_expression.c semantic_calls.c semantic_statement.c semantic_declaration.c semantic_effects.c semantic_units.c
    semantic_recovery.c semantic_union.c semantic_record.c semantic_constant.c constant_proof.c scalar_constant.c lexical_binding.c nominal_binding.c named_binding.c semantic_package.c)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/semantic_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/semantic_internal.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/constant_proof.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/constant_proof.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/scalar_constant.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/scalar_constant.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lexical_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/lexical_binding.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/nominal_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/nominal_binding.h" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/named_binding.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/language/named_binding.h" COPYONLY)
add_library(Chibi::language ALIAS chibi_language)
set_target_properties(chibi_language PROPERTIES EXPORT_NAME language)
chibi_sdk_headers(chibi_language PUBLIC language/semantic.h language/identity.h language/component.h)
target_include_directories(chibi_language PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson"
    "${CMAKE_CURRENT_BINARY_DIR}/private")
target_link_libraries(chibi_language PUBLIC chibi_language_syntax chibi_capabilities PRIVATE chibi_json)
