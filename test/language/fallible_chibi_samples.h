/* Source outcomes and fixed behavior oracles for the dedicated fallible group.
 * Included inside samples[], after sample and sample_group are declared. */
#define FALLIBLE_READER                                                        \
    "package facts\n"                                                          \
    "@fallible Read :: () -> (value: int, error: int?) { return(7, 3); }\n"
#define FALLIBLE_IGNORED                                                       \
    "Trailing optional result is ignored on a reachable path; check, report, " \
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
    { .name = "fallible ordinary optional remains ordinary",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nMaybe :: () -> int? { return nil; }\nUse :: () "
                "-> int { Maybe(); return 9; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[9]" } } },
    { .name = "fallible unnamed error-only result cannot be dropped",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> int? { return 3; }\n"
                "Use :: () { Fail(); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible unnamed error-only result can be checked",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> int? { return 3; }\n"
                "Use :: () -> int { error := Fail(); if error { return "
                "error; } return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[3]" } } },
    { .name = "fallible progress remains valid with error present",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { result := Read(); progress := result.value; if "
      "result.error {} return progress; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible tuple aliases preserve checking",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { result := Read(); saved := result; second := Read(); "
      "if saved.error {} if second.error {} return saved.value + second.value; "
      "}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[14]" } } },
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
    { .name = "fallible every branch checks",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (flag: bool) -> int { result := Read(); if flag { if "
      "result.error {} } else { if result.error {} } return result.value; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[7]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[7]" } } },
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
    { .name = "fallible constant short circuit does not produce outcome",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () -> int { checked := false && "
                                "Read().error != nil; return 2; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[2]" } } },
    { .name = "fallible errors can be forwarded to helpers",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Observe :: (error: int?) { if error {} }\nUse :: () -> int { result := "
      "Read(); Observe(result.error); return result.value; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible complete outcomes can be returned",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "@fallible Forward :: () -> (value: int, error: int?) { result := "
      "Read(); return(result.value, result.error); "
      "}\nUse :: () -> int { result := Forward(); if result.error {} return "
      "result.value; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible named error can be returned implicitly",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "@fallible Forward :: () -> (value: int, error: int?) { result := "
      "Read(); value = result.value; error = result.error; return; }\nUse :: "
      "() -> int { result := Forward(); if result.error {} return "
      "result.value; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible loop checks each produced error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: () -> int { total: int = 0; for i in 0..<3 { result := Read(); "
      "if result.error {} total += result.value; } return total; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[21]" } } },
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
      .library = "package lib\n@fallible pub Read :: () -> (value: int, error: "
                 "int?) { return(7, nil); }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible imported source error is checked",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nUse :: () -> int { "
                "result := lib.Read(); if result.error {} return result.value; "
                "}\n",
      .library = "package lib\n@fallible pub Read :: () -> (value: int, error: "
                 "int?) { return(7, nil); }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible optional pointer aliases are inspected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> (error: int?) { return "
                "3; }\nUse :: () -> int { error := Fail(); pointer := &error; "
                "copy := pointer; if copy^ {} return 8; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[8]" } } },
    { .name = "fallible pointer comparison does not inspect error",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> (error: int?) { return "
                "3; }\nUse :: () { error := Fail(); pointer := &error; same := "
                "pointer == pointer; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible pointer overwrite cannot erase error",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> (error: int?) { return "
                "3; }\nUse :: () { error := Fail(); pointer := &error; "
                "pointer^ = nil; if error {} }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible marker needs optional result",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Fail :: () -> int { return 0; }\n",
      .message = "@fallible requires a final optional error result",
      .same_rejection = true },
    { .name = "fallible duplicate marker rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible @fallible Fail :: () -> int? { "
                "return nil; }\n",
      .message = "Duplicate attribute '@fallible'",
      .same_rejection = true },
    { .name = "fallible unknown marker rejected",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@falible Fail :: () -> int? { return nil; }\n",
      .message = "Unknown attribute '@falible'",
      .same_rejection = true },
    { .name = "fallible marked async union result retains emitter boundary",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Work :: () -> int? { yield; return "
                "nil; }\nUse :: () { task := Work(); cancel task; }\n",
      .message = "task handles with a union result are not supported yet",
      .line = 3,
      .column = 21 },
    { .name = "fallible marked pointer error async source is unsupported",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n@fallible Work :: () -> ^int? { yield; return "
                "nil; }\nUse :: () { task := Work(); cancel task; }\n",
      .message = "@fallible task result checking is not supported by stage one "
                 "yet",
      .line = 3,
      .column = 21 },
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
      "Observe :: (error: int?) -> int { if error { return error; } return 0; "
      "}\n"
      "Later :: (value: int) -> int { yield; return value; }\n"
      "Use :: () { returned := await Later(Observe(Read().error)); }\n"
      "Stop :: () { task := Later(0); cancel task; }\n" },
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
    { .name = "fallible switch candidate can acknowledge error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Choose :: (error: int?) -> int { if error { return error; } return 0; "
      "}\nUse :: (subject: int) -> int { result := Read(); switch subject { "
      "case "
      "Choose(result.error): return 1; case: return 2; } }\n",
      .calls = { { .function = "Use", .arguments = "[0]", .expected = "[2]" },
                 { .function = "Use", .arguments = "[3]", .expected = "[1]" } } },
    { .name = "fallible later switch candidate can be skipped",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Choose :: (error: int?) -> int { if error { return error; } return 0; "
      "}\nUse :: (subject: int) -> int { result := Read(); switch subject { "
      "case 0: return "
      "result.value; case Choose(result.error): return 1; case: return 2; } "
      "}\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible destination index cannot drop error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER "Use :: () { values: int[] = [0, 0, 0, 0, 0, "
                                "0, 0, 0]; values[Read().value] = 1; }\n",
      .message = FALLIBLE_IGNORED,
      .same_rejection = true },
    { .name = "fallible destination index can acknowledge error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Choose :: (error: int?) -> int { return 0; }\nUse :: () -> int { result "
      ":= Read(); values: int[] = [0]; values[Choose(result.error)] = "
      "result.value; return values[0]; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" } } },
    { .name = "fallible shadowed optional is a different binding",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Use :: (flag: bool) { error := Read().error; if flag { error: int? = "
      "nil; if error {} } }\n",
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
    { .name = "fallible bracket slice start can forward error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Bound :: (error: int?) -> int { if error { return 1; } return 0; }\n"
      "Use :: () -> int { values: int[] = [10, 20, 30, 40]; "
      "selected := values[Bound(Read().error)..<3]; "
      "if selected { return selected[0] * 10 + selected.length(); } return -1; "
      "}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[202]" } } },
    { .name = "fallible bracket slice end can forward error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Bound :: (error: int?) -> int { if error { return 1; } return 0; }\n"
      "Use :: () -> int { values: int[] = [10, 20, 30, 40]; "
      "selected := values[0..<Bound(Read().error)]; "
      "if selected { return selected[0] * 10 + selected.length(); } return -1; "
      "}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[101]" } } },
    { .name = "fallible method slice start can forward error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Bound :: (error: int?) -> int { if error { return 1; } return 0; }\n"
      "Use :: () -> int { values: int[] = [10, 20, 30, 40]; "
      "selected := values.slice(Bound(Read().error)..<3); "
      "if selected { return selected[0] * 10 + selected.length(); } return -1; "
      "}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[202]" } } },
    { .name = "fallible method slice end can forward error",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Bound :: (error: int?) -> int { if error { return 1; } return 0; }\n"
      "Use :: () -> int { values: int[] = [10, 20, 30, 40]; "
      "selected := values.slice(0..<Bound(Read().error)); "
      "if selected { return selected[0] * 10 + selected.length(); } return -1; "
      "}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[101]" } } },
    { .name = "fallible member presence proof does not survive shadowing",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nFailure :: struct { code: int; }\n"
                "@fallible Read :: (fail: bool) -> (value: int, error: "
                "Failure?) { "
                "if fail { return(7, {code: 3}); } return(7, nil); }\n"
                "Use :: (flag: bool) -> int { bad := Read(true); clean := "
                "Read(false); "
                "if clean.error {} if bad.error { "
                "if flag { bad := clean; return bad.error.code; } return "
                "bad.error.code; } return 0; }\n",
      .message = "Optional component requires a presence guard before member "
                 "access",
      .same_rejection = true },
    { .name = "fallible member presence proof keeps the same binding",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nFailure :: struct { code: int; }\n"
                "@fallible Read :: (fail: bool) -> (value: int, error: "
                "Failure?) { "
                "if fail { return(7, {code: 3}); } return(7, nil); }\n"
                "Use :: (flag: bool) -> int { bad := Read(true); clean := "
                "Read(false); "
                "if clean.error {} if bad.error { "
                "if flag { return bad.error.code; } return bad.error.code; } "
                "return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[3]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[3]" } } },
    { .name = "fallible shadowed member accepts its own presence guard",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\nFailure :: struct { code: int; }\n"
                "@fallible Read :: (fail: bool) -> (value: int, error: "
                "Failure?) { "
                "if fail { return(7, {code: 3}); } return(7, nil); }\n"
                "Use :: (flag: bool) -> int { bad := Read(true); clean := "
                "Read(false); "
                "if clean.error {} if bad.error { if flag { bad := clean; "
                "if bad.error { return bad.error.code; } return 11; } "
                "return bad.error.code; } return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[3]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[11]" } } },
    { .name = "fallible guarded transparent union can change alternatives",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n"
                "Failure :: union { Denied | Platform(code: int) }\n"
                "@fallible Read :: (fail: bool) -> Failure? { "
                "if fail { return Failure.Platform(7); } return nil; }\n"
                "Use :: (fail: bool) -> int { error := Read(fail); if error { "
                "error = Failure.Denied; if error { switch error { case "
                ".Denied: return "
                "19; "
                "case .Platform(code): return code; } } } return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[0]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[19]" } } },
    { .name = "fallible nested boolean guard reads the valid payload",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n"
                "@fallible Flag :: (value: bool, absent: bool) -> bool? { "
                "if absent { return nil; } return value; }\n"
                "Use :: (value: bool, absent: bool) -> int { "
                "error := Flag(value, absent); if error { "
                "if error { return 99; } return 17; } return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[false,false]", .expected = "[17]" },
                 { .function = "Use", .arguments = "[true,false]", .expected = "[99]" },
                 { .function = "Use", .arguments = "[false,true]", .expected = "[0]" },
                 { .function = "Use", .arguments = "[true,true]", .expected = "[0]" } } },
#define FALLIBLE_POINTER_READER                                                \
    "package facts\n@fallible Fail :: () -> int? { return 3; }\n"
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
    { .name = "fallible correlated pointers retain valid error handling",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { b = nil; p = &a; } else { a = nil; p = &b; } "
      "if p^ {} return 8; }\n"
      "Mirrored :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { a = nil; p = &b; } else { b = nil; p = &a; } "
      "if p^ {} return 8; }\n"
      "Unrelated :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { b = nil; p = &a; } else { a = nil; p = &b; } "
      "count := 0; count = 1; if p^ {} return 9; }\n"
      "Survivor :: (flag: bool) -> int { a := Fail(); b := a; c := a; p := &a; "
      "if flag { b = nil; p = &a; } else { a = nil; p = &b; } "
      "a = nil; if c {} return 10; }\n"
      "Rebound :: (flag: bool) -> int { a := Fail(); b := a; c := a; p := &a; "
      "if flag { b = nil; p = &a; } else { a = nil; p = &b; } "
      "a = nil; p = &c; if p^ {} return 11; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[8]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[8]" },
                 { .function = "Mirrored", .arguments = "[false]", .expected = "[8]" },
                 { .function = "Mirrored", .arguments = "[true]", .expected = "[8]" },
                 { .function = "Unrelated", .arguments = "[false]", .expected = "[9]" },
                 { .function = "Unrelated", .arguments = "[true]", .expected = "[9]" },
                 { .function = "Survivor", .arguments = "[false]", .expected = "[10]" },
                 { .function = "Survivor", .arguments = "[true]", .expected = "[10]" },
                 { .function = "Rebound", .arguments = "[false]", .expected = "[11]" },
                 { .function = "Rebound", .arguments = "[true]", .expected = "[11]" } } },
    { .name = "fallible joined pointers keep unchanged pointees",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_POINTER_READER
      "Use :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { p = &b; } if p^ {} return 8; }\n"
      "Rebound :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { p = &b; } a = nil; p = &b; if p^ {} return 9; }\n"
      "Survivor :: (flag: bool) -> int { a := Fail(); b := a; p := &a; "
      "if flag { p = &b; } a = nil; if b {} return 10; }\n"
      "Checked :: (flag: bool) -> int { a := Fail(); if a {} b := a; "
      "p := &a; if flag { p = &b; } a = nil; return 11; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[8]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[8]" },
                 { .function = "Rebound", .arguments = "[false]", .expected = "[9]" },
                 { .function = "Rebound", .arguments = "[true]", .expected = "[9]" },
                 { .function = "Survivor", .arguments = "[false]", .expected = "[10]" },
                 { .function = "Survivor", .arguments = "[true]", .expected = "[10]" },
                 { .function = "Checked", .arguments = "[false]", .expected = "[11]" },
                 { .function = "Checked", .arguments = "[true]", .expected = "[11]" } } },
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
    { .name = "optional mutation before a fresh presence guard is safe",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_MUTATION_READER
      "Use :: () -> int { e: Failure? = {code: 3}; p := &e; "
      "if Clear(p) && e { return e.code; } return 17; }\n"
      "Before :: () -> int { e: Failure? = {code: 3}; p := &e; "
      "Clear(p); if e { return e.code; } return 17; }\n"
      "Again :: () -> int { e: Failure? = {code: 3}; p := &e; "
      "if e { Clear(p); if e { return e.code; } } return 17; }\n"
      "Direct :: () -> int { e: Failure? = {code: 3}; "
      "Clear(&e); if e { return e.code; } return 17; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[17]" },
                 { .function = "Before", .arguments = "[]", .expected = "[17]" },
                 { .function = "Again", .arguments = "[]", .expected = "[17]" },
                 { .function = "Direct", .arguments = "[]", .expected = "[17]" } } },
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
    "package facts\n"                                                          \
    "@fallible Flag :: (value: bool, absent: bool) -> (data: int, error: "     \
    "bool?) { if absent { return(7, nil); } return(7, value); }\n"             \
    "TakeBool :: (value: bool) -> int { if value { return 99; } return 17; "   \
    "}\n"
#define FALLIBLE_BOOL_CALLS                                                     \
    { .function = "Use", .arguments = "[false,false]", .expected = "[17]" },    \
        { .function = "Use", .arguments = "[true,false]", .expected = "[99]" }, \
        { .function = "Use", .arguments = "[false,true]", .expected = "[0]" },  \
        { .function = "Use", .arguments = "[true,true]", .expected = "[0]" }
    { .name = "fallible nested boolean member keeps its payload on false edge",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_BOOL_READER
      "Use :: (value: bool, absent: bool) -> int { r := Flag(value, absent); "
      "if r.error { if r.error { return 99; } else { return TakeBool(r.error); "
      "} } return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
    { .name = "fallible negated boolean member reads the present payload",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_BOOL_READER
      "Use :: (value: bool, absent: bool) -> int { r := Flag(value, absent); "
      "if r.error { if !r.error { return TakeBool(r.error); } return 99; } "
      "return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
    { .name = "fallible boolean member and operand reads the present payload",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_BOOL_READER
      "Use :: (value: bool, absent: bool) -> int { r := Flag(value, absent); "
      "if r.error { if true && r.error { return 99; } return "
      "TakeBool(r.error); } return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
    { .name = "fallible boolean member or operand reads the present payload",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_BOOL_READER
      "Use :: (value: bool, absent: bool) -> int { r := Flag(value, absent); "
      "if r.error { if false || r.error { return 99; } return "
      "TakeBool(r.error); } return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
    { .name = "fallible boolean member nil comparison still tests presence",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_BOOL_READER
      "Use :: (value: bool, absent: bool) -> int { r := Flag(value, absent); "
      "if r.error != nil { return TakeBool(r.error); } return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
    { .name = "fallible unnamed boolean keeps its payload on false edge",
      .group = FALLIBLE_SAMPLES,
      .source = "package facts\n"
                "@fallible Flag :: (value: bool, absent: bool) -> bool? { "
                "if absent { return nil; } return value; }\n"
                "TakeBool :: (value: bool) -> int { if value { return 99; } "
                "return 17; }\n"
                "Use :: (value: bool, absent: bool) -> int { error := "
                "Flag(value, absent); "
                "if error { if error { return 99; } else { return "
                "TakeBool(error); } } return 0; }\n",
      .calls = { FALLIBLE_BOOL_CALLS } },
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
    { .name = "fallible sibling assignments permit actual error handling",
      .group = FALLIBLE_SAMPLES,
      .source = FALLIBLE_READER
      "Forward :: (error: int?) { if error {} }\n"
      "Use :: () -> int { r := Read(); r.value = 5; if r.error {} return "
      "r.value; }\n"
      "Alias :: () -> int { r := Read(); copy := r; copy.value = 5; "
      "Forward(copy.error); return copy.value; }\n"
      "Original :: () -> int { r := Read(); copy := r; copy.value = 5; if "
      "r.error {} return copy.value; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[5]" },
                 { .function = "Alias", .arguments = "[]", .expected = "[5]" },
                 { .function = "Original", .arguments = "[]", .expected = "[5]" } } },
#undef FALLIBLE_READER
#undef FALLIBLE_IGNORED
