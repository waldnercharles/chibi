# Keep the retained UI and its font rasterizer inside the application module.
# Function scope avoids changing BUILD_SHARED_LIBS for Cute's resident library.
function(chibi_rmlui_dependencies)
    set(BUILD_SHARED_LIBS OFF)
    foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG 0a0221a1347e2f1e07c395263540026e9a0aa7c7 # 2.14.3
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(freetype)
    add_library(Freetype::Freetype ALIAS freetype)

    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
    FetchContent_Declare(rmlui
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG ba95ffe8bfb6370efb2cdcca927eaad4710c5413 # 6.3
        UPDATE_DISCONNECTED TRUE)
    FetchContent_GetProperties(rmlui)
    if(NOT rmlui_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(rmlui)
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()
    # Web behavior RmlUi lacks: CSS color-mix(), so theme roles derive from a
    # few tokens at runtime; HTML's radio groups, which need a name; Vue's
    # bindings, whose v-model leaves an input being edited alone, and whose
    # empty style values unset; CSS's anonymous flex items, so text inside a
    # flex container shows; as in browsers, setting a style to the value it
    # holds lays nothing out; and a handler for data-if, so presence keeps a
    # leaving element shown while its exit transitions run (src/ui/motion.c),
    # as the web's exit animations do.
    # Resolved var() values keep their authored source for the inspector.
    # Text with locally managed content still lays out after font changes.
    # Variables resolving to dp lengths recompute when the pixel ratio changes.
    # A blurred box-shadow blurs its whole shape before the box clips it, as in
    # browsers, so an offset shadow with negative spread keeps its strength.
    # Text fields send HTML's input on each edit and change once on commit
    # (Enter, or leaving the field after an edit); data-value follows input as
    # Vue's v-model does, and data-value-lazy change, as v-model.lazy.
    # data-when, Vue's v-if, builds its element while true and removes it and
    # its views while false, where data-if (v-show) only hides it.
    # As in browsers, input and change events' propagation can be stopped, so
    # a native control keeps its inner fields' events to itself
    # (src/ui/controls).
    # A font-size or line-height set by var() follows its variable, as other
    # properties do, so a density change re-lays out line-height: var(--h).
    # An element moved under the same data model keeps its views bound, as a
    # DOM move keeps a page's bindings; unbinding and binding them anew fails
    # for evaluated text and built data-for children, and leaves views behind.
    # data-template keeps an element's content unbuilt, its markup in
    # rmlui-inner-rml, for native code to mount while it shows (a popover's).
    set(patches "")
    foreach(name color-mix radio-groups data-bindings flex-text same-style data-if-handler variable-source suppressed-text-font-layout variable-unit-invalidation box-shadow-blur input-events data-when event-propagation variable-line-height move-keeps-data data-template)
        list(APPEND patches "${CHIBI_DEPENDENCY_SOURCE_ROOT}/cmake/patches/rmlui-${name}.patch")
    endforeach()
    chibi_apply_patches("${rmlui_SOURCE_DIR}" ${patches})
    add_subdirectory("${rmlui_SOURCE_DIR}" "${rmlui_BINARY_DIR}")
    foreach(target freetype rmlui_core rmlui_debugger)
        set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON)
    endforeach()
endfunction()

chibi_rmlui_dependencies()

