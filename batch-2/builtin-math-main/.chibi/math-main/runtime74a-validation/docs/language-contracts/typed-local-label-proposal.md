# Typed locals and labels: approved parse decision

The user approved declaration precedence on 2026-10-17 with "Approve declaration
precedence". This language change is separate from the M4 compiler port.

A complete typed local `name: Type = value` takes precedence over a label and
assignment when its type begins on the same line as the declaration colon.
A label ending at an automatic statement terminator takes precedence over a
declaration spanning that terminator. Write a label and plain assignment as:

```code
ordinary:
result = result + 1;
```

The ambiguous same-line spelling `ordinary: result = expression;` selects a
typed declaration. A type annotation must begin on the same line as its colon.
Bare labels before other statement forms remain valid, including
`finish: goto finish;` and `compound: result += 1;`. Inferred `:=` declarations
keep their existing precedence. At a system root, inferred and typed storage
continue to select persistent state before callable-local syntax.

The unchanged external scanner emits an automatic terminator after a newline
before an identifier, and recognizes closing braces and end of input. Its
continuation-punctuation exclusions remain unchanged. The parser cannot resolve
the ambiguous spelling by knowing whether an identifier denotes a type or a
value; that information belongs to semantic analysis.

The first M4 graph preparation selected a label and assignment for the valid
nested local `item: Package = {...};`. The diagnostic at the `Package` token
came from assignment analysis, and a subsequent use of `item` read a void-valued
label. Neither identifier is reserved. The retained failure is
`.chibi/evidence/toolchain-m4/differential-first.log`.

The regressions cover a nested multiline four-field record declaration, a
newline-separated label and plain assignment, a same-line label and compound
assignment, and both inferred and typed system-root storage. The program case
executes both branches on bytecode and the configured compiled backend,
requiring actual backend entries and results 2 and 9. Its bounded additions are
two syntax parses, two module preparations and four invocations with a
1000-instruction ceiling each. There are no runtime frames, filesystem fixtures,
workers or timing loops.

The original parser fails the amended syntax case at the label-block child
count assertion, with 12 passing cases and one failure in
`.chibi/evidence/toolchain-m4/parser-syntax-baseline.log`. The final language
aggregate passed in 17.670 seconds, including the amended syntax selector's
13 cases and 503 assertions, semantic's 42 cases and 533 assertions, and
program's 125 cases and 10920 assertions. The program regression supports
required activation state. Its preceding focused program run passed in
1.983 seconds, retained in
`.chibi/evidence/toolchain-m4/parser-program-activation.log`. Configuration and
build time are excluded. Other required validation remains separately reported
by the integration owner.

Two other discoveries remain separate. Private local record member writes
were conservatively classified as execution effects; their correction has its
own semantic and ownership regressions. Authored callable-root labels lack the
predeclaration pass used by nested blocks and handler bodies, so they report
`Unknown label`. This parser change does not fix that registration gap; its
real-label execution regression uses a nested block.
