/**
 * @file Angara grammar for tree-sitter
 * @author cv2
 * @license MIT
 */

module.exports = grammar({
  name: "angara",

  extras: ($) => [
    $.comment,
    /\s/,
  ],

  supertypes: ($) => [
    $._statement,
    $._declaration,
    $._pattern,
  ],

  conflicts: ($) => [
    [$.block, $.record_literal],
    [$.primary_expression, $.simple_type],
    [$.primary_expression, $.generic_type],
    [$.simple_type, $.generic_type],
    [$.simple_type, $.function_type],
    [$._base_type_with_modifiers],
    [$._base_type_with_modifiers, $._base_type_with_modifiers],
    [$.if_statement],
  ],

  word: ($) => $.identifier,

  rules: {
    // ── Top Level ──────────────────────────────────────────

    source_file: ($) => repeat($._declaration),

    // ── Comments ───────────────────────────────────────────

    comment: ($) => token(choice(
      seq("//", /.*/),
      seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/"),
    )),

    // ── Declarations ───────────────────────────────────────

    _declaration: ($) => choice(
      $.function_declaration,
      $.intrinsic_declaration,
      $.foreign_declaration,
      $.class_declaration,
      $.trait_declaration,
      $.contract_declaration,
      $.data_declaration,
      $.enum_declaration,
      $.variable_declaration,
      $.attach_statement,
      $.export_declaration,
      $._statement,
    ),

    export_declaration: ($) => seq(
      "export",
      choice(
        $.function_declaration,
        $.class_declaration,
        $.trait_declaration,
        $.contract_declaration,
        $.data_declaration,
        $.enum_declaration,
        $.variable_declaration,
      ),
    ),

    // ── Function Declaration ───────────────────────────────

    function_declaration: ($) => seq(
      "func",
      field("name", $.identifier),
      optional(field("type_parameters", $.type_parameters)),
      field("parameters", $.parameters),
      optional(seq("->", field("return_type", $._type))),
      choice(field("body", $.block), ";"),
    ),

    type_parameters: ($) => seq(
      "<",
      commaSep1($.type_identifier),
      ">",
    ),

    parameters: ($) => seq(
      "(",
      optional(choice(
        seq("this", optional(seq(",", commaSep1($.parameter)))),
        commaSep1($.parameter),
      )),
      ")",
    ),

    parameter: ($) => seq(
      field("name", $.identifier),
      optional(seq("as", field("type", $._type))),
      // LANG-11: default argument value
      optional(seq("=", field("default_value", $._expression))),
      optional($.variadic),
    ),

    variadic: ($) => "...",

    // ── Intrinsic Declaration ──────────────────────────────

    intrinsic_declaration: ($) => seq(
      "intrinsic",
      "func",
      field("name", $.identifier),
      optional(field("type_parameters", $.type_parameters)),
      field("parameters", $.parameters),
      optional(seq("->", field("return_type", $._type))),
      ";",
    ),

    // ── Foreign Declaration ────────────────────────────────

    foreign_declaration: ($) => seq(
      "foreign",
      choice(
        seq("func", $.func_signature),
        seq("data", field("name", $.identifier), choice(";", seq("{", repeat($.foreign_field), "}"))),
        seq("union", field("name", $.identifier), choice(";", seq("{", repeat($.foreign_field), "}"))),
        seq("const", field("name", $.identifier), "as", field("type", $._type), ";"),
      ),
    ),

    func_signature: ($) => seq(
      field("name", $.identifier),
      optional(field("type_parameters", $.type_parameters)),
      field("parameters", $.parameters),
      optional(seq("->", field("return_type", $._type))),
      ";",
    ),

    foreign_field: ($) => seq(
      field("name", $.identifier),
      "as",
      field("type", $._type),
      ";",
    ),

    // ── Class Declaration ──────────────────────────────────

    class_declaration: ($) => seq(
      "class",
      field("name", $.identifier),
      optional(seq("inherits", field("parent", $.type_identifier))),
      optional(seq("uses", commaSep1(field("trait", $.type_identifier)))),
      optional(seq("signs", commaSep1(field("contract", $.type_identifier)))),
      "{",
      repeat(choice($.access_specifier, $.class_member)),
      "}",
    ),

    access_specifier: ($) => seq(
      choice("public", "private"),
      ":",
    ),

    class_member: ($) => seq(
      optional("static"),
      choice(
        $.variable_declaration,
        $.function_declaration,
      ),
    ),

    // ── Trait Declaration ──────────────────────────────────

    trait_declaration: ($) => seq(
      "trait",
      field("name", $.identifier),
      "{",
      repeat($.function_declaration),
      "}",
    ),

    // ── Contract Declaration ───────────────────────────────

    contract_declaration: ($) => seq(
      "contract",
      field("name", $.identifier),
      "{",
      repeat(choice(
        $.access_specifier,
        $.contract_field,
        $.function_signature,
      )),
      "}",
    ),

    contract_field: ($) => seq(
      choice("let", "const"),
      field("name", $.identifier),
      "as",
      field("type", $._type),
      ";",
    ),

    function_signature: ($) => seq(
      "func",
      field("name", $.identifier),
      optional(field("type_parameters", $.type_parameters)),
      field("parameters", $.parameters),
      optional(seq("->", field("return_type", $._type))),
      ";",
    ),

    // ── Data Declaration ───────────────────────────────────

    data_declaration: ($) => seq(
      "data",
      field("name", $.identifier),
      optional(field("type_parameters", $.type_parameters)),
      "{",
      repeat($.data_field),
      "}",
    ),

    data_field: ($) => seq(
      choice("let", "const"),
      field("name", $.identifier),
      "as",
      field("type", $._type),
      ";",
    ),

    // ── Enum Declaration ───────────────────────────────────

    enum_declaration: ($) => seq(
      "enum",
      field("name", $.identifier),
      "{",
      optional(commaSep($.enum_variant)),
      "}",
    ),

    enum_variant: ($) => seq(
      field("name", $.identifier),
      optional(seq("(", commaSep1($._type), ")")),
    ),

    // ── Variable Declaration ───────────────────────────────

    variable_declaration: ($) => seq(
      choice("let", "const"),
      field("name", $.identifier),
      optional(seq("as", field("type", $._type))),
      optional(seq("=", field("value", $._expression))),
      ";",
    ),

    // ── Attach Statement ───────────────────────────────────

    attach_statement: ($) => seq(
      "attach",
      choice(
        seq(
          field("module", choice($.string, $.identifier)),
          optional(seq("as", field("alias", $.identifier))),
        ),
        seq(
          commaSep1(field("name", $.identifier)),
          "from",
          field("module", choice($.string, $.identifier)),
        ),
      ),
      ";",
    ),

    // ── Statements ─────────────────────────────────────────

    _statement: ($) => choice(
      $.block,
      $.if_statement,
      $.for_statement,
      $.while_statement,
      $.return_statement,
      $.throw_statement,
      $.try_statement,
      $.break_statement,
      $.continue_statement,
      $.unsafe_block,
      $.expression_statement,
      ";",
    ),

    block: ($) => seq(
      "{",
      repeat($._declaration),
      "}",
    ),

    if_statement: ($) => seq(
      "if",
      "(",
      field("condition", choice($._expression, $.if_let)),
      ")",
      field("consequence", $._statement),
      optional(field("alternative", $.else_clause)),
    ),

    else_clause: ($) => choice(
      seq("orif", $.if_statement),
      seq("else", $._statement),
    ),

    if_let: ($) => seq(
      "let",
      field("name", $.identifier),
      optional(seq("as", field("type", $._type))),
      "=",
      field("value", $._expression),
    ),

    for_statement: ($) => seq(
      "for",
      "(",
      choice(
        $.c_style_for,
        $.for_in,
      ),
      ")",
    ),

    c_style_for: ($) => seq(
      field("initializer", choice($.variable_declaration, $.expression_statement)),
      optional(field("condition", $._expression)),
      ";",
      optional(field("update", $._expression)),
    ),

    for_in: ($) => seq(
      field("name", $.identifier),
      "in",
      field("value", $._expression),
    ),

    while_statement: ($) => seq(
      "while",
      "(",
      field("condition", $._expression),
      ")",
      field("body", $.block),
    ),

    return_statement: ($) => seq(
      "return",
      optional(field("value", $._expression)),
      ";",
    ),

    throw_statement: ($) => seq(
      "throw",
      field("value", $._expression),
      ";",
    ),

    try_statement: ($) => seq(
      "try",
      field("body", $.block),
      "catch",
      "(",
      field("name", $.identifier),
      optional(seq("as", field("type", $._type))),
      ")",
      field("handler", $.block),
    ),

    break_statement: ($) => seq("break", ";"),
    continue_statement: ($) => seq("continue", ";"),

    unsafe_block: ($) => seq(
      "@unsafe",
      field("body", $.block),
    ),

    expression_statement: ($) => seq(
      $._expression,
      ";",
    ),

    // ── Expressions ────────────────────────────────────────
    // Layered precedence. Hidden rules (_xxx) don't create named nodes,
    // keeping the parse tree clean. Only _expression and named nodes
    // like binary_expression appear in the output.

    arguments: ($) => seq(
      "(",
      optional(commaSep1(choice(
        alias($.named_argument, $.argument),
        alias($._expression, $.argument),
      ))),
      ")",
    ),

    // LANG-11: named argument at call site (name: value)
    named_argument: ($) => seq(
      field("name", $.identifier),
      ":",
      field("value", $._expression),
    ),

    _expression: ($) => $.binary_expression,

    binary_expression: ($) => choice(
      $.assignment_expr,
      $.ternary_expr,
      $.nil_coalescing_expr,
      $.logical_or_expr,
      $.logical_and_expr,
      $.equality_expr,
      $.comparison_expr,
      $.bitwise_expr,
      $.shift_expr,
      $.additive_expr,
      $.multiplicative_expr,
      $.unary_expr,
      $.postfix_expr,
      $.primary_expression,
    ),

    assignment_expr: ($) => prec.right(1, seq(
      field("left", $.binary_expression),
      field("operator", choice("=", "+=", "-=", "*=", "/=")),
      field("right", $.binary_expression),
    )),

    ternary_expr: ($) => prec.right(2, seq(
      field("condition", $.binary_expression),
      "?",
      field("consequence", $.binary_expression),
      ":",
      field("alternative", $.binary_expression),
    )),

    nil_coalescing_expr: ($) => prec.left(3, seq(
      field("left", $.binary_expression),
      "??",
      field("right", $.binary_expression),
    )),

    logical_or_expr: ($) => prec.left(4, seq(
      field("left", $.binary_expression),
      "||",
      field("right", $.binary_expression),
    )),

    logical_and_expr: ($) => prec.left(5, seq(
      field("left", $.binary_expression),
      "&&",
      field("right", $.binary_expression),
    )),

    equality_expr: ($) => prec.left(6, choice(
      seq(field("left", $.binary_expression), field("operator", choice("==", "!=", "is")), field("right", $.binary_expression)),
      seq(field("left", $.binary_expression), "as", field("type", $._type)),
    )),

    comparison_expr: ($) => prec.left(7, seq(
      field("left", $.binary_expression),
      field("operator", choice(">", ">=", "<", "<=")),
      field("right", $.binary_expression),
    )),

    bitwise_expr: ($) => prec.left(8, seq(
      field("left", $.binary_expression),
      field("operator", choice("&", "|", "^")),
      field("right", $.binary_expression),
    )),

    shift_expr: ($) => prec.left(9, seq(
      field("left", $.binary_expression),
      field("operator", choice("<<", ">>")),
      field("right", $.binary_expression),
    )),

    additive_expr: ($) => prec.left(10, seq(
      field("left", $.binary_expression),
      field("operator", choice("+", "-")),
      field("right", $.binary_expression),
    )),

    multiplicative_expr: ($) => prec.left(11, seq(
      field("left", $.binary_expression),
      field("operator", choice("*", "/", "%")),
      field("right", $.binary_expression),
    )),

    unary_expr: ($) => prec.right(12, seq(
      field("operator", choice("!", "-", "~", "++", "--", "*")),
      field("operand", $.binary_expression),
    )),

    postfix_expr: ($) => prec.left(13, choice(
      seq(field("function", $.binary_expression), field("arguments", $.arguments)),
      seq(field("object", $.binary_expression), ".", field("field", $.identifier)),
      seq(field("object", $.binary_expression), "?.", field("field", $.identifier)),
      seq(field("object", $.binary_expression), "[", field("index", $.binary_expression), "]"),
      seq(field("operand", $.binary_expression), field("operator", choice("++", "--"))),
    )),

    primary_expression: ($) => choice(
      $.number,
      $.float,
      $.string,
      $.bool,
      $.nil,
      "this",
      $.super_expression,
      $.lambda_expression,
      $.identifier,
      $.list_literal,
      $.record_literal,
      $.match_expression,
      seq("(", $.binary_expression, ")"),
    ),

    super_expression: ($) => seq(
      "super",
      choice(
        seq(".", field("field", $.identifier)),
        $.arguments,
      ),
    ),

    lambda_expression: ($) => seq(
      "func",
      field("parameters", $.lambda_parameters),
      optional(seq("->", field("return_type", $._type))),
      field("body", $.block),
    ),

    lambda_parameters: ($) => seq(
      "(",
      optional(commaSep1($.lambda_parameter)),
      ")",
    ),

    lambda_parameter: ($) => seq(
      field("name", $.identifier),
      optional(seq("as", field("type", $._type))),
      // LANG-11: default argument value
      optional(seq("=", field("default_value", $._expression))),
    ),

    list_literal: ($) => seq(
      "[",
      optional(commaSep1($._expression)),
      "]",
    ),

    record_literal: ($) => seq(
      "{",
      optional(commaSep1($.record_entry)),
      "}",
    ),

    record_entry: ($) => seq(
      field("key", choice($.string, $.identifier)),
      ":",
      field("value", $._expression),
    ),

    // ── Match Expression ───────────────────────────────────

    match_expression: ($) => seq(
      "match",
      "(",
      field("value", $._expression),
      ")",
      "{",
      repeat($.match_case),
      "}",
    ),

    match_case: ($) => seq(
      "case",
      field("pattern", $._pattern),
      optional(seq("(", field("binding", $.identifier), ")")),
      ":",
      field("value", choice(
        seq("{", $._expression, "}"),
        $._expression,
      )),
      optional(","),
    ),

    _pattern: ($) => choice(
      $.dotted_pattern,
      $.identifier,
      $.number,
      $.string,
      $.bool,
      $.nil,
    ),

    dotted_pattern: ($) => seq(
      $.identifier,
      repeat1(seq(".", $.identifier)),
    ),

    // ── Literals ───────────────────────────────────────────

    number: ($) => /\d[\d_]*/,
    float: ($) => /\d[\d_]*\.\d[\d_]*([eE][+-]?\d+)?/,
    string: ($) => seq(
      "\"",
      repeat(choice(
        token.immediate(/[^"\\]+/),
        $.escape_sequence,
      )),
      "\"",
    ),
    escape_sequence: ($) => token.immediate(seq("\\", /./)),
    bool: ($) => choice("true", "false"),
    nil: ($) => "nil",

    // ── Types ──────────────────────────────────────────────

    _type: ($) => choice(
      $.owned_type,
      $.pointer_type,
      $._base_type_with_modifiers,
    ),

    owned_type: ($) => seq("@own", $._type),

    pointer_type: ($) => seq(
      optional("^"),
      $.pointer_stars,
      $._base_type_with_modifiers,
    ),

    pointer_stars: ($) => token(repeat1("*")),

    _base_type_with_modifiers: ($) => seq(
      $._base_type,
      repeat(choice(
        "?",
        seq("[", $.number, "]"),
      )),
    ),

    _base_type: ($) => choice(
      $.simple_type,
      $.generic_type,
      $.function_type,
      $.record_type,
    ),

    simple_type: ($) => choice(
      $.type_identifier,
      $.identifier,
      "nil",
      "string", "int", "float", "bool", "void",
      "i8", "i16", "i32", "i64",
      "u8", "u16", "u32", "u64", "uint",
      "f32", "f64",
      "list", "map", "record",
      "function", "any", "thread",
    ),

    generic_type: ($) => seq(
      field("name", choice(
        $.type_identifier,
        "list", "map", "record", "function",
      )),
      "<",
      commaSep1(field("type_argument", $._type)),
      ">",
    ),

    function_type: ($) => seq(
      "function",
      "(",
      optional(commaSep1($._type)),
      ")",
      "->",
      field("return_type", $._type),
    ),

    record_type: ($) => seq(
      "{",
      optional(commaSep1($.record_type_field)),
      "}",
    ),

    record_type_field: ($) => seq(
      field("name", choice($.identifier, $.string)),
      ":",
      field("type", $._type),
    ),

    // ── Identifiers ────────────────────────────────────────

    identifier: ($) => /[_a-zA-Z][_a-zA-Z0-9]*/,
    type_identifier: ($) => /[A-Z][_a-zA-Z0-9]*/,
  },
});

// ── Helpers ────────────────────────────────────────────────

function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}
