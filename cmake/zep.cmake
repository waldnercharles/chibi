if(NOT CHIBI_DEPENDENCY_PACKAGE)
    include(cmake/zep_dependencies.cmake)
endif()

# The C binding (a pass-through, as src/ui/rml.cpp is) and the kit primitive
# built on it.
add_library(chibi_zep STATIC src/editor/text/zep_bridge.cpp)
target_compile_features(chibi_zep PRIVATE cxx_std_17)
target_include_directories(chibi_zep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chibi_zep PRIVATE zep)
target_sources(chibi_rmlui PRIVATE src/editor/rmlui/kit_code.c)
target_link_libraries(chibi_rmlui PUBLIC chibi_zep)
