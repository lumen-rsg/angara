; Keywords
[
  "let" "const" "func" "return" "if" "orif" "else"
  "for" "while" "in" "attach" "from"
  "class" "inherits" "trait" "uses" "signs" "contract"
  "this" "super" "static" "private" "public" "export"
  "enum" "case" "match" "data"
  "try" "catch" "throw" "break" "continue"
  "foreign" "intrinsic" "union"
] @keyword

; Type-defining keywords
[
  "nil" "string" "int" "float" "bool" "void"
  "i8" "i16" "i32" "i64"
  "u8" "u16" "u32" "u64" "uint"
  "f32" "f64"
  "list" "map" "record" "function" "any" "thread"
] @type.builtin

; Literals
(number) @number
(float) @float
(string) @string
(bool) @boolean
(nil) @constant.builtin
(escape_sequence) @string.escape

; Comments
(comment) @comment

; Types
(type_identifier) @type

; Identifiers
(identifier) @variable

; Function definitions
(function_declaration name: (identifier) @function)
(intrinsic_declaration name: (identifier) @function)

; Parameters
(parameter name: (identifier) @parameter)
(lambda_parameter name: (identifier) @parameter)

; Variadic
(variadic) @punctuation.special

; Operators
[
  "+" "-" "*" "/" "%"
  "==" "!=" ">" ">=" "<" "<="
  "&&" "||" "??"
  "!" "~"
  "&" "|" "^" "<<" ">>"
  "=" "+=" "-=" "*=" "/="
  "++" "--"
  "?."
] @operator

; Punctuation
[
  "(" ")" "{" "}" "[" "]"
  "," "." ":" ";"
  "->"
] @punctuation.delimiter

; Attributes
"@unsafe" @attribute
"@own" @attribute

; Enum variants
(enum_variant name: (identifier) @constant)
