/* Generated from the reviewed Chibi fixture; do not edit this copy. */
static const char build_workflow_source[] = "package build_workflow\n"
"\n"
"Build :: () -> int {\n"
"    configure := build.run([\n"
"        \"cmake\", \"-S\", \".\", \"-B\", \"build self-hosting\",\n"
"        \"-DCHIBI_NOTE=space \\\"quote\\\";$literal*\"\n"
"    ]);\n"
"    if (configure != 0) {\n"
"        return configure;\n"
"    }\n"
"    return build.run([\"cmake\", \"--build\", \"build self-hosting\"]);\n"
"}\n"
"";
