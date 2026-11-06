#include "syntax_corpus.h"

/* Fixed inputs shared by retained syntax assertions and the authored diagnostic
 * differential. The separate manifest-backed corpus keeps its original bounds. */
const syntax_corpus_case syntax_corpus_cases[SYNTAX_CORPUS_CASE_COUNT] = {
    { "missing-separator", "Broken :: behavior { first := 1 second := 2 }", false },
    { "reserved-name", "Broken :: behavior { true := 1 }", false },
    { "missing-initializer", "Broken :: behavior { on Begin :: () { value := ; } }", false },
    { "unclosed-blocks", "Broken :: behavior { on Begin :: () { if true { }", false },
    { "missing-body", "Broken :: library { MissingBody :: () -> () }", false },
    { "missing-address-operand", "Broken :: behavior { on Begin :: () { nudge(&) } }", false },
    { "recovery-after-error", "Broken :: behavior { broken := ; later := 7 }", false },
    { "utf8-before-edit", "Edit :: behavior {\n// café\ncount := 1\n}\n", true },
    { "error-edit-base", "Edit :: behavior { first := 1; later := 2 }", true },
    { "precedence-continuations",
      "Expressions :: behavior {\n"
      "product := 1 + 2 * 3\n"
      "difference := 9 - 3 - 1\n"
      "vector := {x: 1, y: 2}\n"
      "items := [1,\n2, 3,]\n"
      "selected := items[1]\n"
      "fallback_value := fallback(default: 0)\n"
      "on Begin :: () {\n"
      "computed := 1 // newline continuation\n+ 2\n"
      "other := 3 + /* operator continuation */\n4\n"
      "consume(\nvector.x,\nselected\n)\n"
      "}\n}",
      true },
    { "typed-named-calls",
      "Calls :: behavior { on Begin :: () {\n"
      "await emit self.Flash(Value: bool = true);\n"
      "consume(Owner: other, Position: target.Position, Label: \"text\");\n"
      "consume(Value: to_vec(component(Transform)));\n"
      "} }",
      true },
    { "error-with-descendants",
      "Broken :: behavior { on Begin :: () { value := (1 + ); } @ }",
      false },
    { "crlf-utf8-before-error", "Broken :: behavior {\r\n label := \"é\"; on Begin :: () { value := ; }\r\n}", false },
    { "missing-package-terminator", "package app import \"x\"\n", false },
};

const syntax_corpus_edit syntax_corpus_edits[SYNTAX_CORPUS_EDIT_COUNT] = {
    { "utf8-newline",
      SYNTAX_CORPUS_UTF8,
      sizeof("Edit :: behavior {\n// caf") - 1,
      sizeof("Edit :: behavior {\n// café") - 1,
      "π\n// extra",
      "Edit :: behavior {\n// cafπ\n// extra\ncount := 1\n}\n" },
    { "remove-initializer",
      SYNTAX_CORPUS_DELETE,
      sizeof("Edit :: behavior { first := ") - 1,
      sizeof("Edit :: behavior { first := 1") - 1,
      "",
      "Edit :: behavior { first := ; later := 2 }" },
};
