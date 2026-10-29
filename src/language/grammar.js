// Syntax has no registry, graph, compiler, or editor dependencies.
const K = Object.fromEntries(
  [
    "package",
    "import",
    "behavior",
    "library",
    "system",
    "state",
    "retain",
    "action",
    "event",
    "disabled",
    "on",
    "after",
    "as",
    "bind",
    "events",
    "union",
    "struct",
    "switch",
    "is",
    "not",
    "and",
    "or",
    "macro",
    "entry",
    "exit",
    "query",
    "if",
    "else",
    "while",
    "for",
    "in",
    "case",
    "sequence",
    "branch",
    "return",
    "reduce",
    "unordered",
    "goto",
    "break",
    "continue",
    "await",
    "yield",
    "cancel",
    "true",
    "false",
    "nil",
    "null",
    "plain",
    "service",
    "setting",
    "wildcard",
    "from",
    "split",
    "component",
    "prototype",
    "relationship",
    "pub",
    "delete",
    "view",
    "emit",
  ].map((word) => [word, word]),
);

const CONTEXTUAL = ["package", "import", "struct", "from", "split", "component", "not", "unordered", "view"];

const P = {
  or: 1,
  and: 2,
  bit_or: 3,
  bit_xor: 4,
  bit_and: 5,
  equality: 6,
  comparison: 7,
  // `??` binds below arithmetic and above comparison, as in Swift and Kotlin.
  coalesce: 8,
  shift: 9,
  sum: 10,
  product: 11,
  unary: 12,
  member: 13,
};

function comma(rule) {
  return seq(rule, repeat(seq(",", rule)), optional(","));
}

// Package declarations, after an optional `pub`.
function functionDeclaration($, pub) {
  return seq(repeat(field("attribute", $.attribute)), ...pub, field("name", $._name), "::",
    $._callable_signature, optional(";"));
}
function constant($, pub) {
  return seq(...pub, field("name", $._name), choice("::", seq(":", field("type", $.type), ":")),
    field("value", $._expression), $._end);
}
function record($, pub) {
  return seq(...pub, field("name", $._name), "::", K.struct,
    optional(field("parameters", $.parameter_list)),
    field("body", $.record_body), optional(";"));
}
function union($, pub) {
  return seq(...pub, field("name", $._name), "::", K.union, "{",
    $.union_variant, repeat(seq("|", $.union_variant)), optional("|"), "}", optional(";"));
}

module.exports = grammar({
  name: "chibi",
  extras: ($) => [/\s/, $.comment],
  externals: ($) => [$._automatic_terminator],
  word: ($) => $.identifier,
  // Contextual words stay usable as names wherever a name fits.
  reserved: {
    global: ($) =>
      Object.entries(K)
        .filter(([word]) => !CONTEXTUAL.includes(word))
        .map(([, rule]) => rule),
    member: ($) => [],
  },
  conflicts: ($) => [
    [$._name],
    [$._common_declaration, $._statement],
    [$._single_type],
    [$.parameter, $.type_name],
    [$.parameter, $.type_binder],
    [$.type_name],
    [$.record_declaration, $._name],
    [$._public_record, $._name],
    [$.event_declaration, $.builtin_reference],
    [$._single_type, $._expression],
    [$._single_type, $._name],
    [$.argument_list, $.parenthesized_expression],
    [$.parameter, $._expression],
    [$.constant_declaration, $.local_declaration, $.label_statement],
    [$.constant_declaration, $.local_declaration, $.label_statement, $._member_name],
    [$._expression, $._member_name],
    [$.builtin_reference, $._member_name],
    [$.boolean, $._member_name],
    [$.nil, $._member_name],
    [$.type_name, $._call_target],
    [$.type_name, $._expression],
    [$.reduce_statement, $._name],
    [$._name, $.unary_expression],
  ],
  rules: {
    source_file: ($) =>
      choice(
        $.behavior, $.library, $.system, $.component,
        $.prototype_declaration, $.relationship_declaration,
        seq(field("package", $.package_header), repeat(choice(
          $.import_declaration,
          $.behavior, $.system, $.component,
          $.function_declaration, $.constant_declaration,
          $.state_declaration, $.record_declaration, $.union_declaration,
          $.macro_declaration,
          alias($._public_function, $.function_declaration),
          alias($._public_constant, $.constant_declaration),
          alias($._public_record, $.record_declaration),
          alias($._public_union, $.union_declaration),
        ))),
      ),
    package_header: ($) => seq(K.package, field("name", $.identifier), $._end),
    import_declaration: ($) => seq(
      K.import,
      optional(field("alias", $.identifier)),
      field("path", $.string),
      $._end,
    ),
    prototype_declaration: ($) =>
      seq(field("name", $._name), "::", K.prototype,
        optional(field("bases", $.prototype_bases)),
        field("body", $.prototype_body)),
    prototype_bases: ($) => seq("(", comma($._name), ")"),
    prototype_body: ($) =>
      seq("{", repeat(choice($.prototype_component,
        seq($.struct_field, choice(",", $._end)))), "}"),
    prototype_component: ($) =>
      seq(field("name", $._name), field("body", $.struct_expression)),
    relationship_declaration: ($) =>
      seq(field("name", $._name), "::", K.relationship,
        field("body", $.struct_expression)),
    behavior: ($) =>
      seq(
        field("name", $._name),
        "::", K.behavior,
        field("body", $.declaration_body),
      ),
    library: ($) =>
      seq(field("name", $._name), "::", K.library, field("body", alias($.library_body, $.declaration_body))),
    system: ($) =>
      seq(
        field("name", $._name),
        "::", K.system,
        field("parameters", $.query_list),
        field("body", $.system_body),
      ),
    // A component's fields are data: a type, or a number measured by an
    // optional range and unit.
    component: ($) =>
      seq(
        field("name", $._name),
        "::", K.component,
        field("body", $.component_body),
      ),
    component_body: ($) => seq("{", repeat($.component_field), "}"),
    component_field: ($) =>
      seq(
        field("name", $._member_name),
        ":",
        field("type", choice($.type, $.measure, $.measured_type)),
        $._end,
      ),
    // A lone name is a type; a unit standing alone reads as one and resolves
    // to a measure when no type has its name.
    measure: ($) =>
      choice(
        seq(field("range", $.range_type), optional(field("unit", $.unit))),
        field("unit", alias($._compound_unit, $.unit)),
      ),
    range_type: ($) =>
      seq(field("min", $._bound), "..", field("max", $._bound)),
    _bound: ($) => choice($.number, alias($._negative_number, $.unary_expression)),
    _negative_number: ($) => seq(field("operator", "-"), field("operand", $.number)),
    // u, s, u/s, u/s^2: unit names exist only here, so they stay free names.
    unit: ($) => seq($._unit_term, repeat(seq("/", $._unit_term))),
    measured_type: ($) => seq(field("base", $.type), field("unit", $.unit)),
    _signature_type: ($) => choice($.type, $.measured_type, alias($._compound_unit, $.unit)),
    _compound_unit: ($) =>
      choice(
        seq($.identifier, "^", $.number, repeat(seq("/", $._unit_term))),
        seq($.identifier, repeat1(seq("/", $._unit_term))),
      ),
    _unit_term: ($) => seq($.identifier, optional(seq("^", $.number))),
    declaration_body: ($) => seq("{", repeat($._declaration), "}"),
    library_body: ($) => seq("{", repeat(choice($._declaration, $.record_declaration)), "}"),
    record_declaration: ($) => record($, []),
    // `pub` exports a package's declaration to the packages that import it.
    _public_record: ($) => record($, [field("visibility", K.pub)]),
    record_body: ($) => seq("{", repeat($.record_field), "}"),
    record_field: ($) =>
      seq(field("name", $._member_name), ":", field("type", $.type),
        optional(seq("=", field("value", $._expression))), $._end),
    system_body: ($) =>
      seq("{", repeat(choice($._common_declaration,
        alias($._explicit_state_declaration, $.state_declaration), $._statement)), "}"),
    _declaration: ($) =>
      choice($.state_declaration, $._common_declaration),
    _common_declaration: ($) =>
      choice(
        $.action_declaration,
        $.event_declaration,
        $.bind_declaration,
        $.handler,
        $.function_declaration,
        $.constant_declaration,
        $.union_declaration,
        $.macro_declaration,
        $.events_declaration,
        $.entry_declaration,
        $.exit_declaration,
      ),
    // Behavior/library roots retain implicit declarations. Executable bodies
    // require an explicit retained qualifier; ordinary declarations are locals.
    state_declaration: ($) =>
      prec.dynamic(1, choice($._state_declaration_value, $._explicit_state_declaration)),
    _explicit_state_declaration: ($) =>
      seq(
        field("name", $._name), ":", field("storage", K.retain),
        choice(
          seq(field("type", $.type), optional(seq("=", field("value", $._expression)))),
          seq("=", field("value", $._expression)),
        ),
        $._end,
      ),
    _state_declaration_value: ($) =>
      seq(
        choice(
          seq(field("name", $._name), ":=", field("value", $._expression)),
          seq(
            field("name", $._name),
            ":",
            field("type", $.type),
            optional(seq("=", field("value", $._expression))),
          ),
        ),
        $._end,
      ),
    action_declaration: ($) =>
      seq(optional(K.disabled), K.action, $._computation_declaration),
    event_declaration: ($) => seq(K.event, $._computation_declaration),
    _computation_declaration: ($) =>
      seq(
        field("name", $._name),
        optional(seq(":", field("type", choice($.type, $.parameter_list)))),
        "=",
        field("value", $._expression),
        $._end,
      ),
    bind_declaration: ($) =>
      seq(
        K.bind,
        field("target", $._expression),
        "=",
        field("value", $._expression),
        $._end,
      ),
    // `on X :: (a: T)` names payload fields; `after X` binds an action's
    // results.
    handler: ($) =>
      choice(
        seq(
          field("kind", K.on),
          field("name", $._handler_name),
          field("binding", "::"),
          field("parameters", $.parameter_list),
          field("body", $.block),
        ),
        seq(
          field("kind", K.after),
          field("name", $._handler_name),
          optional(field("parameters", $.parameter_list)),
          field("body", $.block),
        ),
      ),
    _handler_name: ($) =>
      choice(
        $._name,
        $.builtin_reference,
        alias(K.entry, $.builtin_reference),
        alias($._handler_path, $.member_expression),
      ),
    _handler_path: ($) =>
      seq(
        field("object", $._handler_name),
        ".",
        field("member", $._member_name),
      ),
    // `@test` before a declaration marks it; analysis decides which names exist.
    attribute: ($) => seq("@", field("name", $.identifier)),
    function_declaration: ($) => functionDeclaration($, []),
    _public_function: ($) => functionDeclaration($, [field("visibility", K.pub)]),
    constant_declaration: ($) => constant($, []),
    _public_constant: ($) => constant($, [field("visibility", K.pub)]),
    union_declaration: ($) => union($, []),
    _public_union: ($) => union($, [field("visibility", K.pub)]),
    union_variant: ($) => choice(
      field("literal", $.string),
      seq(field("name", $._name), optional(field("parameters", $.parameter_list))),
    ),
    macro_declaration: ($) => seq(K.macro, $._callable_declaration),
    _callable_declaration: ($) =>
      seq(
        field("name", $._name),
        $._callable_signature,
      ),
    _callable_signature: ($) =>
      seq(
        field("parameters", $.parameter_list),
        optional(seq("->", field("results", choice($.parameter_list, $._signature_type)))),
        optional("=>"), field("body", $.callable_body),
      ),
    callable_body: ($) =>
      seq("{", repeat(choice($._common_declaration,
        alias($._explicit_state_declaration, $.state_declaration), $._statement)), "}"),
    parameter_list: ($) => seq("(", optional(comma($.parameter)), ")"),
    // Systems and ad hoc queries share left-to-right row bindings.
    query_list: ($) => seq("(", optional(comma(choice(
      alias($._query_parameter, $.parameter), $.query_generator,
      $.query_binding, $.query_guard,
    ))), ")"),
    _query_parameter: ($) => seq(
      field("name", $._name), ":", field("type", $._signature_type),
      optional(seq(K.from, field("source", $._expression))),
    ),
    query_generator: ($) => seq(
      field("name", $._name), optional(seq(":", field("type", $.type))),
      K.in, field("source", $._expression),
    ),
    query_binding: ($) => seq(
      field("name", $._name),
      choice(":=", seq(":", field("type", $.type), "=")),
      field("value", $._expression),
    ),
    query_guard: ($) => seq(K.if, field("condition", $._expression)),
    query_expression: ($) => seq(K.query, field("parameters", $.query_list)),
    parameter: ($) =>
      seq(
        optional(field("comptime", "$")),
        field("name", $._name),
        optional(seq(":", field("type", $._signature_type))),
        optional(seq(choice(K.from, K.split), field("source", $._expression))),
        optional(seq("=", field("value", $._expression))),
      ),
    events_declaration: ($) =>
      seq(K.events, field("name", $._name), field("body", $.declaration_body)),
    entry_declaration: ($) =>
      seq(K.entry, field("name", $._name), field("body", $.block)),
    exit_declaration: ($) => seq(K.exit, field("name", $._name), $._end),
    type: ($) => choice($._single_type, $.union_type),
    union_type: ($) => prec.left(1, seq(
      field("member", alias($._single_type, $.type)),
      repeat1(seq("|", field("member", alias($._single_type, $.type)))),
    )),
    _single_type: ($) => seq(
      repeat(field("trait", choice(K.plain, K.view, K.service, "self", K.setting,
        seq(K.wildcard, "(", $.number, ")")))),
      $._type_primary,
      repeat(choice(seq("[", "]"), "?")),
    ),
    _type_primary: ($) => choice(
      field("name", choice($.type_name, $.type_binder)),
      $.type_application,
      field("map", $.map_type),
      $.pointer_type,
      $.parenthesized_type,
    ),
    pointer_type: ($) => seq("^", field("target", alias($._type_primary, $.type))),
    parenthesized_type: ($) => seq("(", field("type", $.type), ")"),
    // `[string: int]` maps keys of one type to values of another.
    map_type: ($) => seq("[", field("key", $.type), ":", field("value", $.type), "]"),
    type_name: ($) => seq($._name, repeat(seq(".", $._name))),
    type_binder: ($) => seq("$", field("name", $._name)),
    type_application: ($) =>
      seq(field("function", $.type_name), field("arguments", $.argument_list)),
    block: ($) => seq("{", repeat($._statement), "}"),
    _statement: ($) =>
      choice(
        $.local_declaration,
        $.assignment,
        $.reduce_statement,
        $.expression_statement,
        $.if_statement,
        $.while_statement,
        $.for_statement,
        $.switch_statement,
        $.constant_declaration,
        $.sequence_statement,
        $.return_statement,
        $.goto_statement,
        $.break_statement,
        $.continue_statement,
        $.yield_statement,
        $.cancel_statement,
        $.label_statement,
        $.delete_statement,
      ),
    // `delete m[k]` removes a key from a map.
    delete_statement: ($) => seq(K.delete, field("target", $.index_expression), $._end),
    local_declaration: ($) =>
      prec.dynamic(2, seq(
        field("name", $._name),
        choice(":=", seq(":", field("type", $.type), "=")),
        field("value", $._expression),
        $._end,
      )),
    assignment: ($) =>
      seq(
        field("target", $._expression),
        field(
          "operator",
          choice("=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="),
        ),
        field("value", $._expression),
        $._end,
      ),
    expression_statement: ($) => seq($._expression, $._end),
    reduce_statement: ($) => seq(
      K.reduce,
      choice(
        seq(optional(field("ordering", K.unordered)),
          field("target", $._expression), field("operator", "+="),
          field("value", $._expression)),
        seq(field("operator", choice("min", "max")), "(",
          field("target", $._expression), ",", field("value", $._expression), ")"),
      ),
      $._end,
    ),
    if_statement: ($) =>
      prec.right(
        seq(
          K.if,
          field("condition", $._expression),
          field("consequence", $.block),
          optional(
            seq(K.else, field("alternative", choice($.block, $.if_statement))),
          ),
        ),
      ),
    while_statement: ($) =>
      seq(K.while, field("condition", $._expression), field("body", $.block)),
    // `for <` iterates in reverse, as Jai's does.
    for_statement: ($) =>
      seq(
        K.for,
        optional(field("reverse", "<")),
        choice(
          $._range_header,
          $._foreach_header,
          seq("(", choice($._range_header, $._foreach_header), ")"),
        ),
        field("body", $.block),
      ),
    _range_header: ($) =>
      seq(
        field("name", $._name),
        K.in,
        field("first", $._expression),
        field("bound", choice("..", "..<")),
        field("last", $._expression),
      ),
    _foreach_header: ($) =>
      seq(optional(seq(field("key", $._name), ",")),
        optional(field("reference", "&")), field("name", $._name),
        K.in, field("value", $._expression)),
    switch_statement: ($) =>
      seq(K.switch, field("value", $._expression), "{", repeat($.switch_arm), "}"),
    switch_arm: ($) =>
      seq(K.case, optional(field("value", choice($.variant_pattern, $._expression))),
        ":", field("body", choice($.block, alias($._case_statements, $.block)))),
    _case_statements: ($) => repeat1($._statement),
    variant_pattern: ($) => seq(".", field("name", $._name),
      optional(field("bindings", $.pattern_bindings))),
    pattern_bindings: ($) => seq("(", optional(comma($._name)), ")"),
    sequence_statement: ($) =>
      seq(K.sequence, "{", repeat($.branch_statement), "}"),
    branch_statement: ($) => seq(K.branch, field("body", $.block)),
    return_statement: ($) =>
      seq(
        K.return,
        optional(seq(".", field("outcome", $._name))),
        optional(field("value", choice($.argument_list, $._expression))),
        $._end,
      ),
    goto_statement: ($) => seq(K.goto, field("name", $._name), $._end),
    break_statement: ($) => seq(K.break, $._end),
    continue_statement: ($) => seq(K.continue, $._end),
    yield_statement: ($) => seq(K.yield, $._end),
    cancel_statement: ($) => seq(K.cancel, field("value", $._expression),
        optional(seq(K.after, field("duration", $._expression))), $._end),
    label_statement: ($) => choice(
      prec.dynamic(2, seq(field("name", $._name), ":", $._automatic_terminator)),
      seq(field("name", $._name), ":"),
    ),
    _expression: ($) =>
      choice(
        $._name,
        alias("self", $.identifier),
        $.string,
        $.number,
        $.character,
        $.boolean,
        $.nil,
        $.builtin_reference,
        $.member_expression,
        $.index_expression,
        $.call_expression,
        $.emit_expression,
        $.is_expression,
        $.as_expression,
        $.parenthesized_expression,
        $.unary_expression,
        $.address_expression,
        $.dereference_expression,
        $.binary_expression,
        $.array_expression,
        $.map_expression,
        $.struct_expression,
        $.await_expression,
        $.query_expression,
      ),
    builtin_reference: ($) => choice(K.state, K.event),
    member_expression: ($) =>
      prec.left(
        P.member,
        seq(
          field("object", $._expression),
          ".",
          field("member", $._member_name),
        ),
      ),
    index_expression: ($) =>
      prec.left(
        P.member,
        seq(
          field("object", $._expression),
          "[",
          field("index", choice($._expression, $.range_expression)),
          "]",
        ),
      ),
    call_expression: ($) =>
      prec.dynamic(1, prec(
        P.member,
        seq(
          field("function", $._call_target),
          field("arguments", $.argument_list),
        ),
      )),
    // `emit target.Event(payload)` sends an event to the target entity, as
    // does `emit Event(target, payload)`.
    emit_expression: ($) =>
      prec.dynamic(1, prec(
        P.member,
        seq(
          K.emit,
          field("event", choice($._name, $.member_expression)),
          field("arguments", $.argument_list),
        ),
      )),
    // Ranges are arguments to sequence operations, not stored values.
    range_expression: ($) => seq(
      field("start", $._expression), "..<", field("end", $._expression)),
    _call_target: ($) => choice(
      $._name, $.builtin_reference,
      $.member_expression, $.index_expression, $.call_expression,
      $.parenthesized_expression,
    ),
    argument_list: ($) =>
      seq(
        "(",
        optional(
          comma(choice($.named_argument, $.typed_argument, $.range_expression, $._expression)),
        ),
        ")",
      ),
    named_argument: ($) =>
      // A bare identifier after ':' is a value. Type annotations with '=' or
      // 'from'/'split' remain unambiguous even when the value is also a type name.
      prec.dynamic(1, seq(field("name", $.argument_name), ":", field("value", $._expression))),
    typed_argument: ($) =>
      prec.dynamic(2, seq(
        field("name", $.argument_name),
        ":",
        field("type", $.type),
        choice(
          seq(choice(K.from, K.split), field("source", $._expression),
            optional(seq("=", field("value", $._expression)))),
          seq("=", field("value", $._expression)),
        ),
      )),
    argument_name: ($) => seq($._member_name, repeat(seq(".", $._member_name))),
    is_expression: ($) => prec.left(P.comparison, seq(
      field("value", $._expression), K.is,
      choice($._negated_is_pattern, seq(
        field("type", choice($.type, $.variant_pattern)),
        optional(field("binding", $._name)))),
    )),
    _negated_is_pattern: ($) => prec.left(P.member, seq(
      field("negated", K.not),
      field("type", choice($.type, $.variant_pattern)),
      optional(field("binding", $._name)),
    )),
    as_expression: ($) => prec.left(P.comparison, seq(
      field("value", $._expression), K.as, field("type", $.type),
    )),
    parenthesized_expression: ($) =>
      seq("(", field("expression", $._expression), ")"),
    await_expression: ($) =>
      prec(P.unary, seq(K.await, field("value", $._expression))),
    address_expression: ($) => prec(P.unary,
      seq("&", field("operand", $._expression))),
    dereference_expression: ($) => prec.left(P.member,
      seq(field("operand", $._expression), token.immediate(prec(1, "^")))),
    unary_expression: ($) =>
      prec(
        P.unary,
        seq(
          field("operator", choice("!", K.not, "-", "+", "~")),
          field("operand", $._expression),
        ),
      ),
    binary_expression: ($) =>
      choice(
        ...[
          [P.or, "||"],
          [P.or, K.or],
          [P.and, "&&"],
          [P.and, K.and],
          [P.bit_or, "|"],
          [P.bit_xor, "^"],
          [P.bit_and, "&"],
          [P.equality, "=="],
          [P.equality, "!="],
          [P.comparison, "<"],
          [P.comparison, "<="],
          [P.comparison, ">"],
          [P.comparison, ">="],
          [P.comparison, K.in],
          [P.shift, "<<"],
          [P.shift, ">>"],
          [P.sum, "+"],
          [P.sum, "-"],
          [P.product, "*"],
          [P.product, "/"],
          [P.product, "%"],
        ].map(([precedence, operator]) =>
          prec.left(
            precedence,
            seq(
              field("left", $._expression),
              field("operator", operator),
              field("right", $._expression),
            ),
          ),
        ),
        prec.left(P.comparison, seq(
          field("left", $._expression),
          field("operator", $.not_in),
          field("right", $._expression),
        )),
        // `a ?? b ?? c` groups as `a ?? (b ?? c)`.
        prec.right(P.coalesce, seq(
          field("left", $._expression),
          field("operator", "??"),
          field("right", $._expression),
        )),
      ),
    // `k not in m`.
    not_in: ($) => seq(K.not, K.in),
    array_expression: ($) => seq("[", optional(comma($._expression)), "]"),
    // `["Ada": 3]`, and `[:]` for an empty map, as Swift writes them.
    map_expression: ($) => seq("[", choice(":", comma($.map_entry)), "]"),
    map_entry: ($) => seq(field("key", $._expression), ":", field("value", $._expression)),
    struct_expression: ($) => seq("{", optional(comma($.struct_field)), "}"),
    struct_field: ($) =>
      seq(field("name", $._member_name), ":", field("value", $._expression)),
    // Keep the callable path alive beside unary `not` until arguments resolve
    // it; otherwise `not(Value: value)` commits to a parenthesized operand.
    _name: ($) => choice($.identifier,
      ...CONTEXTUAL.map((word) => alias(
        word === "not" ? prec(P.unary, K[word]) : K[word], $.identifier))),
    _member_name: ($) => reserved("member", choice(
      $._name,
      ...Object.entries(K).filter(([word]) => !CONTEXTUAL.includes(word))
        .map(([, rule]) => alias(rule, $.identifier)),
    )),
    identifier: ($) => /[a-zA-Z_][a-zA-Z0-9_]*/,
    number: ($) =>
      /(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/,
    boolean: ($) => choice(K.true, K.false),
    nil: ($) => choice(K.nil, K.null),
    string: ($) => token(seq('"', repeat(choice(/[^"\\\n]/, /\\[^\n]/)), '"')),
    // One character: semantic analysis rejects an empty or longer body.
    character: ($) => token(seq("'", repeat(choice(/[^'\\\n]/, /\\[^\n]/)), "'")),
    comment: ($) =>
      choice(
        token(seq("//", /[^\n]*/)),
        token(seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/")),
      ),
    _end: ($) => choice(";", $._automatic_terminator),
  },
});
