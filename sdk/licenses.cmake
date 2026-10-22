# Keep notices in the installed SDK, not only in the Python archive wrapper.
# Player and application exports inherit share/chibi/licenses recursively.
install(FILES
    "${CHIBI_SDK_ROOT}/LICENSE.md"
    "${CHIBI_SDK_ROOT}/COPYING"
    "${CHIBI_SDK_ROOT}/COPYING.LESSER"
    "${CHIBI_SDK_ROOT}/COPYING.EXAMPLES"
    DESTINATION share/chibi/licenses/chibi)
install(FILES "${CHIBI_SDK_ROOT}/docs/distribution-licensing.md"
    DESTINATION share/chibi/licenses/chibi/docs)
install(FILES "${CHIBI_SDK_ROOT}/third_party/fmt/LICENSE"
    "${CHIBI_SDK_ROOT}/third_party/fmt/README.md"
    DESTINATION share/chibi/licenses/fmt)
