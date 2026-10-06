add_library(chibi_thirdparty STATIC ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/yyjson/yyjson.c ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/fmt/fmt.c)
target_include_directories(chibi_thirdparty PUBLIC ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/yyjson ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/fmt)
set_source_files_properties(${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/fmt/fmt.c PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
