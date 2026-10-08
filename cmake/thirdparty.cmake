add_library(chibi_thirdparty STATIC ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/yyjson/yyjson.c)
target_include_directories(chibi_thirdparty PUBLIC ${CHIBI_DEPENDENCY_SOURCE_ROOT}/third_party/yyjson)
