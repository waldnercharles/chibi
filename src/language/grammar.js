// Syntax has no registry, graph, compiler, or editor dependencies.
const K = Object.fromEntries(
  [
    "behavior",
    "library",
    "system",
    "without",
    "state",
    "shared",
    "action",
    "event",
    "disabled",
    "on",
    "after",
    "as",
    "bind",
    "events",
    "pure",
    "fn",
    "union",
    "switch",
    "is",
    "not",
    "and",
    "or",
    "where",
    "macro",
    "entry",
    "exit",
    "owner",
    "description",
    "domain",
    "dataflow",
    "depth_limit",
    "query",
    "join",
    "if",
    "else",
    "while",
    "for",
    "foreach",
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
    "engine",
    "cast",
    "plain",
    "ref",
    "service",
    "setting",
    "wildcard",
    "from",
    "split",
    "component",
  ].map((word) => [word, word]),
);

const CONTEXTUAL = ["from", "split", "component", "not", "join", "unordered"];

const P = {
  or: 1,
  and: 2,
  bit_or: 3,
  bit_xor: 4,
  bit_and: 5,
  equality: 6,
  comparison: 7,
  shift: 8,
  sum: 9,
  product: 10,
  unary: 11,
  member: 12,
};

function comma(rule) {
  return seq(rule, repeat(seq(",", rule)), optional(","));
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
    [$._common_declaration, $._statement],
    [$._single_type],
    [$.type_name],
    [$._name, $._member_name],
    [$._expression, $._name],
    [$.argument_list, $.parenthesized_expression],
    [$._state_declaration_value, $.local_declaration, $.label_statement],
    [$.local_declaration, $.label_statement],
    [$._state_declaration_value, $.local_declaration],
    [$._expression, $._member_name],
    [$._expression, $._call_target, $._name],
    [$._expression, $._call_target],
    [$.reduce_statement, $._expression],
    [$.reduce_statement, $._call_target],
    [$.measured_type, $._signature_type],
    [$._compound_unit, $._expression],
  ],
  rules: {
    source_file: ($) =>
      choice($.behavior, $.library, $.system, $.component),
    behavior: ($) =>
      seq(
        K.behavior,
        field("name", $._name),
        field("body", $.declaration_body),
      ),
    library: ($) =>
      seq(K.library, field("name", $._name), field("body", $.declaration_body)),
    system: ($) =>
      seq(
        K.system,
        field("name", $._name),
        optional(choice(
          field("parameters", $.parameter_list),
          seq(K.query, field("parameters", $.query_parameter_list)),
        )),
        repeat(field("join", $.query_join)),
        optional(field("without", $.without_clause)),
        optional(seq(K.where, field("where", $._expression))),
        field("body", $.system_body),
      ),
    // A component's fields are data: a type, or a number measured by an
    // optional range and unit.
    component: ($) =>
      seq(
        K.component,
        field("name", $._name),
        field("body", $.component_body),
      ),
    component_body: ($) => seq("{", repeat($.component_field), "}"),
    component_field: ($) =>
      seq(
        field("name", $._name),
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
    without_clause: ($) => seq(K.without, comma($.type)),
    declaration_body: ($) => seq("{", repeat($._declaration), "}"),
    system_body: ($) =>
      seq("{", repeat(choice($._declaration, $._statement)), "}"),
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
        $.metadata_declaration,
        $.query_declaration,
      ),
    // Systems accept both declarations and statements at their root. Prefer
    // persistent state there. Callable bodies require explicit state storage.
    state_declaration: ($) =>
      prec.dynamic(1, seq(
        optional(field("storage", choice(K.state, K.shared))),
        $._state_declaration_value,
      )),
    _explicit_state_declaration: ($) =>
      seq(field("storage", choice(K.state, K.shared)), $._state_declaration_value),
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
          seq(
            field("type", $.type),
            field("name", $._name),
            "=",
            field("value", $._expression),
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
    handler: ($) =>
      seq(
        field("kind", choice(K.on, K.after)),
        field("name", $._handler_name),
        optional(seq(K.as, field("alias", $._name))),
        optional(field("parameters", $.parameter_list)),
        field("body", $.block),
      ),
    _handler_name: ($) =>
      choice(
        $._name,
        $.builtin_reference,
        alias($._handler_path, $.member_expression),
      ),
    _handler_path: ($) =>
      seq(
        field("object", $._handler_name),
        ".",
        field("member", $._member_name),
      ),
    function_declaration: ($) =>
      seq(field("name", $._name), "::", optional(K.pure), optional(K.fn),
        $._callable_signature, optional(";")),
    constant_declaration: ($) =>
      seq(field("name", $._name), "::", field("value", $._expression), $._end),
    union_declaration: ($) =>
      seq(field("name", $._name), "::", K.union, "{",
        $.union_variant, repeat(seq("|", $.union_variant)), optional("|"), "}", optional(";")),
    union_variant: ($) => choice(
      field("literal", $.string),
      seq(field("name", $.identifier), optional(field("parameters", $.parameter_list))),
    ),
    macro_declaration: ($) => seq(K.macro, $._callable_declaration),
    _callable_declaration: ($) =>
      seq(
        field("name", $._name),
        $._callable_signature,
      ),
    _callable_signature: ($) =>
      seq(
        optional(field("type_parameters", $.type_parameter_list)),
        field("parameters", $.parameter_list),
        optional(seq("->", field("results", choice($.parameter_list, $._signature_type)))),
        field("body", $.callable_body),
      ),
    callable_body: ($) =>
      seq("{", repeat(choice($._common_declaration,
        alias($._explicit_state_declaration, $.state_declaration), $._statement)), "}"),
    type_parameter_list: ($) => seq("<", comma($.type_parameter), ">"),
    type_parameter: ($) =>
      seq(
        field("name", $.identifier),
        optional(seq(":", field("type", $.type))),
      ),
    parameter_list: ($) => seq("(", optional(comma($.parameter)), ")"),
    query_parameter_list: ($) => comma($.parameter),
    query_join: ($) => seq(K.join, choice(
      field("parameters", $.query_parameter_list),
      field("generator", $.query_generator),
    )),
    query_generator: ($) => seq(
      field("name", $._name), K.in, field("source", $._expression),
    ),
    parameter: ($) =>
      seq(
        choice(
          seq(
            field("name", $._name),
            optional(seq(":", field("type", $._signature_type))),
          ),
          seq(field("type", $._signature_type), field("name", $._name)),
        ),
        optional(seq(choice(K.from, K.split), field("source", $._expression))),
        optional(seq("=", field("value", $._expression))),
      ),
    events_declaration: ($) =>
      seq(K.events, field("name", $._name), field("body", $.declaration_body)),
    entry_declaration: ($) =>
      seq(K.entry, field("name", $._name), field("body", $.block)),
    exit_declaration: ($) => seq(K.exit, field("name", $._name), $._end),
    metadata_declaration: ($) =>
      seq(
        field(
          "kind",
          choice(K.owner, K.description, K.domain, K.depth_limit, K.dataflow),
        ),
        optional(field("value", choice($._name, $.number))),
        $._end,
      ),
    query_declaration: ($) =>
      seq(K.query, field("value", $._expression), $._end),
    type: ($) => choice($._single_type, $.union_type),
    union_type: ($) => prec.left(1, seq(
      field("member", alias($._single_type, $.type)),
      repeat1(seq("|", field("member", alias($._single_type, $.type)))),
    )),
    _single_type: ($) =>
      seq(
        repeat(
          field(
            "trait",
            choice(
              K.plain,
              K.ref,
              K.service,
              "self",
              K.setting,
              seq(K.wildcard, "(", $.number, ")"),
            ),
          ),
        ),
        field("name", $.type_name),
        optional(field("arguments", $.type_argument_list)),
        repeat(choice(seq("[", "]"), "?")),
      ),
    type_name: ($) => seq($._name, repeat(seq(".", $._name))),
    type_argument_list: ($) => seq("<", comma($.type), ">"),
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
        $.foreach_statement,
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
      ),
    local_declaration: ($) =>
      seq(
        field("name", $._name),
        choice(":=", seq(":", field("type", $.type), "=")),
        field("value", $._expression),
        $._end,
      ),
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
    for_statement: ($) =>
      seq(
        K.for,
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
        "..",
        field("last", $._expression),
      ),
    foreach_statement: ($) =>
      seq(
        K.foreach,
        choice($._foreach_header, seq("(", $._foreach_header, ")")),
        field("body", $.block),
      ),
    _foreach_header: ($) =>
      seq(field("name", $._name), K.in, field("value", $._expression)),
    switch_statement: ($) =>
      seq(K.switch, field("value", $._expression), "{", repeat($.switch_arm), "}"),
    switch_arm: ($) =>
      seq(K.case, optional(field("value", choice($.variant_pattern, $._expression))),
        ":", field("body", choice($.block, alias($._case_statements, $.block)))),
    _case_statements: ($) => repeat1($._statement),
    variant_pattern: ($) => seq(".", field("name", $.identifier),
      optional(field("bindings", $.pattern_bindings))),
    pattern_bindings: ($) => seq("(", optional(comma($.identifier)), ")"),
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
    label_statement: ($) => seq(field("name", $._name), ":"),
    _expression: ($) =>
      choice(
        $.identifier,
        alias(K.unordered, $.identifier),
        alias("self", $.identifier),
        $.string,
        $.number,
        $.boolean,
        $.nil,
        $.builtin_reference,
        $.engine_reference,
        $.member_expression,
        $.index_expression,
        $.call_expression,
        $.cast_expression,
        $.is_expression,
        $.as_expression,
        $.parenthesized_expression,
        $.unary_expression,
        $.binary_expression,
        $.array_expression,
        $.struct_expression,
        $.await_expression,
      ),
    builtin_reference: ($) => choice(K.state, K.event, K.entry),
    engine_reference: ($) => seq(K.engine, "[", field("name", $.string), "]"),
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
          field("index", $._expression),
          "]",
        ),
      ),
    call_expression: ($) =>
      prec.dynamic(1, prec(
        P.member,
        seq(
          field("function", $._call_target),
          optional(field("type_arguments", $.type_argument_list)),
          field("arguments", $.argument_list),
        ),
      )),
    _call_target: ($) => choice(
      $.identifier, alias(K.unordered, $.identifier), $.string, $.engine_reference, $.builtin_reference,
      $.member_expression, $.index_expression, $.call_expression,
      $.parenthesized_expression,
    ),
    argument_list: ($) =>
      seq(
        "(",
        optional(
          comma(choice($.named_argument, $.typed_argument, $._expression)),
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
    cast_expression: ($) =>
      seq(
        K.cast,
        "<",
        field("type", $.type),
        ">",
        "(",
        field("value", $._expression),
        ")",
      ),
    is_expression: ($) => prec.left(P.comparison, seq(
      field("value", $._expression), K.is, optional(field("negated", K.not)),
      field("type", choice($.type, $.variant_pattern)), optional(field("binding", $.identifier)),
    )),
    as_expression: ($) => prec.left(P.comparison, seq(
      field("value", $._expression), K.as, field("type", $.type),
    )),
    parenthesized_expression: ($) =>
      seq("(", field("expression", $._expression), ")"),
    await_expression: ($) =>
      prec(P.unary, seq(K.await, field("value", $._expression))),
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
      ),
    array_expression: ($) => seq("[", optional(comma($._expression)), "]"),
    struct_expression: ($) => seq("{", optional(comma($.struct_field)), "}"),
    struct_field: ($) =>
      seq(field("name", $._member_name), ":", field("value", $._expression)),
    _name: ($) => choice($.identifier, $.string),
    _member_name: ($) => reserved("member", choice($.identifier, $.string)),
    identifier: ($) => /[a-zA-Z_][a-zA-Z0-9_]*/,
    number: ($) =>
      /(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/,
    boolean: ($) => choice(K.true, K.false),
    nil: ($) => choice(K.nil, K.null),
    string: ($) => token(seq('"', repeat(choice(/[^"\\\n]/, /\\[^\n]/)), '"')),
    comment: ($) =>
      choice(
        token(seq("//", /[^\n]*/)),
        token(seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/")),
      ),
    _end: ($) => choice(";", $._automatic_terminator),
  },
});
