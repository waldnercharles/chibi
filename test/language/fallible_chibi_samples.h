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
{ .name = "fallible correlated pointer cannot hide final erasure on true "
          "edge",
  .group = FALLIBLE_SAMPLES,
  .source = FALLIBLE_POINTER_READER
  "Use :: (flag: bool) { a := Fail(); b := a; p := &a; "
  "if flag { b = nil; p = &a; } else { a = nil; p = &b; } "
  "a = nil; if p^ {} }\n",
  .message = FALLIBLE_IGNORED,
  .same_rejection = true },
    { .name = "fallible correlated pointer cannot hide final erasure on false "
              "edge",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) { a := Fail(); b := a; p := &a; "
      "if flag { a = nil; p = &b; } else { b = nil; p = &a; } "
      "a = nil; if p^ {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible joined pointer cannot hide later pointee erasure",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) { a := Fail(); b := a; p := &a; "
      "if flag { p = &b; } a = nil; if p^ {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible pointer join cannot revive erased pointee alias",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) { a := Fail(); b := a; p := &a; "
      "a = nil; if flag { p = &b; } if p^ {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible joined pointer cannot hide later erasure on true edge",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) { a := Fail(); b := a; p := &b; "
      "if flag { p = &a; } a = nil; if p^ {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible pointer join cannot revive erased alias on true edge",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) { a := Fail(); b := a; p := &a; "
      "a = nil; if flag {} else { p = &b; } if p^ {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
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
