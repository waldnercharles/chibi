include_guard(GLOBAL)

# Pair these in the same directory around dependency includes. Macros keep
# dependency-provided variables in the caller's scope.
macro(chibi_dependency_options_begin)
    get_directory_property(_chibi_project_options COMPILE_OPTIONS)
    set(_chibi_dependency_options "${_chibi_project_options}")
    list(FILTER _chibi_dependency_options EXCLUDE REGEX "^-W")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_chibi_dependency_options}")
endmacro()

macro(chibi_dependency_options_end)
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_chibi_project_options}")
    unset(_chibi_project_options)
    unset(_chibi_dependency_options)
endmacro()
