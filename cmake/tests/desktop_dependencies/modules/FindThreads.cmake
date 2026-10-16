set(Threads_FOUND TRUE)
set(CMAKE_THREAD_LIBS_INIT "-pthread")
if(NOT TARGET Threads::Threads)
    add_library(Threads::Threads INTERFACE IMPORTED)
    set_target_properties(Threads::Threads PROPERTIES
        INTERFACE_COMPILE_OPTIONS "-pthread"
        INTERFACE_LINK_LIBRARIES "-pthread"
        FIXTURE_ID "discovered-threads")
endif()
