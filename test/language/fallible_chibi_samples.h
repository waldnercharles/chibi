/* Source outcomes and fixed behavior oracles for the dedicated fallible group.
 * Included inside samples[], after sample and sample_group are declared. */
#define FALLIBLE_READER                                                        \
    "package facts\n@error Failure :: struct { code: int; }\nRead :: () -> "   \
    "(value: int, error: Failure?) { return(7, {code: 3}); }\n"
#define FALLIBLE_IGNORED                                                       \
    "Error result is ignored on a reachable path; check, report, "             \
    "store or return it"
#define FALLIBLE_POINTER_READER                                                \
    "package facts\n@error Failure :: struct { code: int; }\nFail :: () -> "   \
    "Failure? { return {code: 3}; }\n"
#undef FALLIBLE_POINTER_READER
#define FALLIBLE_MUTATION_READER                                               \
    "package facts\nFailure :: struct { code: int; }\n"                        \
    "Clear :: (p: ^(Failure?)) -> bool { p^ = nil; return true; }\n"
#undef FALLIBLE_MUTATION_READER
#define FALLIBLE_BOOL_READER                                                   \
    "package facts\nFlag :: (value: bool, absent: bool) -> (data: int, "       \
    "error: bool?) { if absent { return(7, nil); } return(7, value); "         \
    "}\nTakeBool :: (value: bool) -> int { if value { return 99; } return "    \
    "17; }\n"
#define FALLIBLE_BOOL_CALLS                                                     \
    { .function = "Use", .arguments = "[false,false]", .expected = "[17]" },    \
        { .function = "Use", .arguments = "[true,false]", .expected = "[99]" }, \
        { .function = "Use", .arguments = "[false,true]", .expected = "[0]" },  \
        { .function = "Use", .arguments = "[true,true]", .expected = "[0]" }
#undef FALLIBLE_BOOL_CALLS
#undef FALLIBLE_BOOL_READER
#undef FALLIBLE_READER
#undef FALLIBLE_IGNORED
