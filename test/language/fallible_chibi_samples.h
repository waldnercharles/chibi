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
#undef FALLIBLE_READER
#undef FALLIBLE_IGNORED
