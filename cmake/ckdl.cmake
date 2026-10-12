# KDL 2 parses directly into native asset values.
add_library(ckdl STATIC
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/bigint.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/compat.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/emitter.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/parser.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/str.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/tokenizer.c
    ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src/utf8.c
)
target_include_directories(ckdl PUBLIC ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/include)
target_include_directories(ckdl PRIVATE ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/ckdl/src)
target_compile_definitions(ckdl PUBLIC KDL_STATIC_LIB=1 PRIVATE BUILDING_KDL=1)
target_compile_options(ckdl PRIVATE -w)

