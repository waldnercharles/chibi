function(chibi_install_product target)
    # Packaged executable bytes must already have their intended installed RPATH.
    set_property(TARGET ${target} PROPERTY BUILD_WITH_INSTALL_RPATH ON)
    install(TARGETS ${target} RUNTIME DESTINATION bin)
endfunction()
