# Languages enabled by the SDK stay in its directory on CMake 4.4 and older.
# These packages make the tooling driver a direct or transitive C++ consumer.
if(TARGET Chibi::task_scheduler OR TARGET Chibi::render)
    enable_language(CXX)
endif()
