/* Source outcomes and fixed behavior oracles for the dedicated fallible group.
 * Included inside samples[], after sample and sample_group are declared. */
#define FALLIBLE_READER                                                        \
    "package facts\n@error Failure :: struct { code: int; }\nRead :: () -> "   \
    "(value: int, error: Failure?) { return(7, {code: 3}); }\n"
#define FALLIBLE_IGNORED                                                       \
    "Error result is ignored on a reachable path; check, report, "             \
    "store or return it"
{ .name = "fallible bare result is ignored",
  .group = FALLIBLE_SAMPLES,
  .source = FALLIBLE_READER "Use :: () { Read(); }\n",
  .message = FALLIBLE_IGNORED,
  .same_rejection = true },
    { .name = "fallible unused tuple is ignored",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () { unused := Read(); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible success projection does not handle error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () -> int { return Read().value; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible unused error local is ignored",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () { result := Read(); unused := result.error; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible unnamed error-only result cannot be dropped",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\nFail "
                ":: () -> Failure? { return {code: 3}; }\nUse :: () { Fail(); "
                "}\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible overwrite cannot erase obligation",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () { result := Read(); error := result.error; error = "
      "nil; if error {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible only one branch checks",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: (flag: bool) { result := Read(); if "
                                "flag { if result.error {} } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible conditional alias cannot hide loss",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (flag: bool) { result := Read(); chosen := result.error; if flag "
      "{ chosen = nil; } if chosen {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible conditional check can be skipped",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: (flag: bool) { result := Read(); "
                                "checked := flag && result.error != nil; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible loop cannot overwrite prior error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (flag: bool) { while flag { result := Read(); } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible loop check might never execute",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: (flag: bool) { result := Read(); while "
                                "flag { if result.error {} } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible imported source keeps marker",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nUse :: () -> int { "
                "result := lib.Read(); return result.value; }\n",
      .library = "package lib\n@error pub Failure :: struct { code: int; } "
                 "\npub Read :: () -> (value: int, error: Failure?) { "
                 "return(7, nil); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible pointer comparison does not inspect error",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\nFail "
                ":: () -> (error: Failure?) { return {code: 3}; }\nUse :: () { "
                "error := Fail(); pointer := &error; same := pointer == "
                "pointer; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible pointer overwrite cannot erase error",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\nFail "
                ":: () -> (error: Failure?) { return {code: 3}; }\nUse :: () { "
                "error := Fail(); pointer := &error; pointer^ = nil; if error "
                "{} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "old function error marker is rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> int? { return nil; }\n",
      .message = "Unknown attribute '@fallible'",
      .same_rejection = true },
    { .name = "duplicate error type marker is rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error @error Failure :: struct { code: int; "
                "}\n",
      .message = "Duplicate attribute '@error'",
      .same_rejection = true },
    { .name = "fallible unknown marker rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@falible Fail :: () -> int? { return nil; }\n",
      .message = "Unknown attribute '@falible'",
      .same_rejection = true },
    { .name = "fallible synchronous argument under await cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Later :: (value: int) -> int { yield; return value; }\n"
      "Use :: () { returned := await Later(Read().value); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible synchronous argument under await can forward error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Observe :: (error: Failure?) -> int { if error { return error.code; } "
      "return 0; }\nLater :: (value: int) -> int { yield; return value; }\nUse "
      ":: () { returned := await Later(Observe(Read().error)); }\nStop :: () { "
      "task := Later(0); cancel task; }\n" },
    { .name = "fallible explicit blank binding still drops error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () { _ := Read(); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible switch candidate cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (subject: int) -> int { switch subject { case "
      "Read().value: return 1; case: return 2; } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible later switch candidate can be skipped",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Choose :: (error: Failure?) -> int { if error { return error.code; } "
      "return 0; }\nUse :: (subject: int) -> int { result := Read(); switch "
      "subject { case 0: return result.value; case Choose(result.error): "
      "return 1; case: return 2; } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible destination index cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () { values: int[] = [0, 0, 0, 0, 0, "
                                "0, 0, 0]; values[Read().value] = 1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible shadowed optional is a different binding",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (flag: bool) { error := Read().error; if flag { error: Failure? "
      "= nil; if error {} } }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible bracket slice start cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { values: int[] = [0, 1, 2, 3, 4, 5, 6, 7]; "
      "selected := values[Read().value..<8]; "
      "if selected { return selected.length(); } return -1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible bracket slice end cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { values: int[] = [0, 1, 2, 3, 4, 5, 6, 7]; "
      "selected := values[0..<Read().value]; "
      "if selected { return selected.length(); } return -1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible method slice start cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { values: int[] = [0, 1, 2, 3, 4, 5, 6, 7]; "
      "selected := values.slice(Read().value..<8); "
      "if selected { return selected.length(); } return -1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible method slice end cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { values: int[] = [0, 1, 2, 3, 4, 5, 6, 7]; "
      "selected := values.slice(0..<Read().value); "
      "if selected { return selected.length(); } return -1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible member presence proof does not survive shadowing",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\nRead "
                ":: (fail: bool) -> (value: int, error: Failure?) { if fail { "
                "return(7, {code: 3}); } return(7, nil); }\nUse :: (flag: "
                "bool) -> int { bad := Read(true); clean := Read(false); if "
                "clean.error {} if bad.error { if flag { bad := clean; return "
                "bad.error.code; } return bad.error.code; } return 0; }\n",
      .message = "Optional component requires a presence guard before member "
                 "access",
      .same_rejection = true },
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
    { .name = "optional condition cannot reuse presence before an alias "
              "mutation",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_MUTATION_READER
      "Use :: () -> int { e: Failure? = {code: 3}; p := &e; "
      "if e && Clear(p) { return e.code; } return 0; }\n",
      .message = "Optional component requires a presence guard before member "
                 "access",
      .same_rejection = true },
    { .name = "optional condition cannot reuse presence before a direct "
              "mutation",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_MUTATION_READER
      "Use :: () -> int { e: Failure? = {code: 3}; "
      "if e && Clear(&e) { return e.code; } return 0; }\n",
      .message = "Optional component requires a presence guard before member "
                 "access",
      .same_rejection = true },
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
    { .name = "fallible sibling assignment does not acknowledge error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { r := Read(); r.value = 5; return r.value; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible tuple alias sibling assignment does not acknowledge "
              "error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () -> int { r := Read(); copy := r; "
                                "copy.value = 5; return copy.value; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "required marked record errors cannot be dropped",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\n"
                "Read :: () -> Failure { return {code: 3}; }\nUse :: () { "
                "Read(); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "required marked union errors cannot be dropped",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: union { Denied | "
                "Platform(code: int) }\n"
                "Read :: () -> Failure { return Failure.Denied; }\nUse :: () { "
                "Read(); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "earlier error-bearing result slots are rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Failure :: struct { code: int; }\n"
                "Bad :: () -> (error: Failure?, value: int) { return(nil, 0); "
                "}\n",
      .message = "An error-bearing result must be the final result",
      .same_rejection = true },
    { .name = "error marker on a callable is rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@error Bad :: () {}\n",
      .message = "Unknown attribute '@error'",
      .same_rejection = true },
#undef FALLIBLE_READER
#undef FALLIBLE_IGNORED
