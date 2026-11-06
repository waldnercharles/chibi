# Compatibility reader while CMake constructs SDKs and consumer metadata.
set(version_file "${CMAKE_CURRENT_LIST_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_file}")
file(READ "${version_file}" CHIBI_SDK_VERSION)
string(STRIP "${CHIBI_SDK_VERSION}" CHIBI_SDK_VERSION)
if(CHIBI_SDK_VERSION STREQUAL "")
    message(FATAL_ERROR "sdk/VERSION must declare the SDK version")
endif()
