/* Use the configured native compiler and C flags, including sanitizers. */
static const char *const export_compiler[] = {
    "/opt/homebrew/opt/llvm/bin/clang",
    "-fdebug-compilation-dir=build/build/build",
    "-g",
    "-UNDEBUG",

    NULL
};
