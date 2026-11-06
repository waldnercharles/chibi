# Discovery must originate below the directory that exports the SDK closure.
include("${CHIBI_ROOT}/cmake/Findcrypto.cmake")
if(CASE STREQUAL "mismatch")
    message(FATAL_ERROR "fixture: mismatched crypto provider was accepted")
endif()
if(NOT TARGET AWS::crypto)
    message(FATAL_ERROR "fixture: upstream crypto discovery created no target")
endif()
set_property(TARGET fixture_sdk PROPERTY INTERFACE_LINK_LIBRARIES "AWS::crypto")
