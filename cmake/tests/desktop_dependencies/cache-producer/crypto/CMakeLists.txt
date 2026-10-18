include("${CHIBI_ROOT}/cmake/Findcrypto.cmake")
add_library(s2n INTERFACE)
target_link_libraries(s2n INTERFACE AWS::crypto Threads::Threads dl rt m)
# Match pinned s2n's Linux public crypto and OS-library interface.
fixture_property(s2n INTERFACE_LINK_LIBRARIES "AWS::crypto;Threads::Threads;dl;rt;m")
