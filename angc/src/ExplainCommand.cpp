#include "CLI.h"
#include "Colors.h"
#include <iostream>
#include <map>
#include <string>

namespace angara {

// ─── Helpers ────────────────────────────────────────────────────────────────

namespace {

/// Print a category overview when no individual entry matches.
void printCategoryFallback(const std::string& code, char prefix) {
    int num = 0;
    try { num = std::stoi(code.substr(1)); } catch (...) {}

    if (prefix == 'E') {
        if (num >= 1 && num <= 24) {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a lexer error.\n\n"
                      << "The compiler could not tokenize your source code. "
                      << "Lexer errors occur during the first stage of compilation "
                      << "and indicate malformed tokens — unterminated strings, invalid "
                      << "escape sequences, bad numeric literals, or unexpected characters.\n\n"
                      << CLR_DIM << "  Check the exact line and column reported by the compiler. "
                      << "Most lexer errors are straightforward to fix once you locate the "
                      << "offending token.\n" << CLR_RESET;
        } else if (num >= 100 && num <= 247) {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a parser / syntax error.\n\n"
                      << "The compiler could not parse your code structure. "
                      << "Parser errors mean the tokens are valid but their arrangement "
                      << "does not form a valid Angara construct — missing delimiters, "
                      << "malformed declarations, or incorrect statement structure.\n\n"
                      << CLR_DIM << "  Look at the reported location. The error message tells "
                      << "you what the parser expected (e.g., 'Expected ';' after ...'). "
                      << "Often the actual mistake is a few tokens earlier.\n" << CLR_RESET;
        } else if (num >= 248 && num <= 471) {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a type-checker / semantic error.\n\n"
                      << "The compiler understood your syntax but found a semantic issue: "
                      << "type mismatch, undefined symbol, contract/trait violation, "
                      << "invalid operation on a type, or a scoping problem.\n\n"
                      << CLR_DIM << "  The error message includes the specific types or symbols "
                      << "involved. Check that types are compatible, all names are spelled "
                      << "correctly, and trait/contract requirements are satisfied.\n" << CLR_RESET;
        } else if (num >= 501 && num <= 516) {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a Chaperone memory-safety error.\n\n"
                      << "The Chaperone is Angara's compile-time ownership and borrow checker. "
                      << "It tracks every allocation and ensures no leaks, use-after-free, "
                      << "double-free, or data races.\n\n"
                      << CLR_DIM << "  See CHAPERONE.md for the full memory-model reference, "
                      << "or run 'angc explain E501' for the most common error (leak). "
                      << "Inside @unsafe blocks, these are downgraded to warnings.\n" << CLR_RESET;
        } else if (num >= 900 && num <= 904) {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a kernel-mode restriction error.\n\n"
                      << "When compiling with --kernel, certain runtime features are "
                      << "unavailable because the kernel has no libc, no dynamic loader, "
                      << "and no pthreads.\n\n"
                      << CLR_DIM << "  Remove the unsupported construct or compile without "
                      << "--kernel if you are building a userspace program.\n" << CLR_RESET;
        } else {
            std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET
                      << " is a compiler error.\n"
                      << "  No detailed explanation is available for this code yet.\n"
                      << CLR_DIM << "  The error message printed by the compiler should "
                      << "describe the issue.\n" << CLR_RESET;
        }
    } else if (prefix == 'W') {
        std::cout << CLR_BOLD << CLR_YELLOW << code << CLR_RESET
                  << " is a compiler warning.\n";
        std::cout << CLR_DIM << "  Suppress with: -Wno-" << code << "\n" << CLR_RESET;
        if (num >= 510 && num <= 522) {
            std::cout << "  This is a Chaperone warning. See CHAPERONE.md for details.\n";
        } else {
            std::cout << "  No detailed explanation is available for this code yet.\n";
        }
    }
}

} // anonymous namespace

// ─── handleExplain ──────────────────────────────────────────────────────────

int CLI::handleExplain(std::vector<std::string> args) {
    if (args.empty()) {
        std::cerr << CLR_RED
                  << "[ERROR] 'explain' requires an error or warning code.\n"
                  << CLR_RESET
                  << "Usage: angc explain <code>\n"
                  << "Examples:\n"
                  << "  angc explain W003    — explain unused-variable warning\n"
                  << "  angc explain E501    — explain Chaperone leak error\n"
                  << "  angc explain E257    — explain 'if let' type error\n"
                  << "\nCodes are printed by the compiler in diagnostics, e.g.:\n"
                  << "  [E377] Undefined variable 'x'.\n"
                  << "  [W003] Unused variable 'y'.\n";
        return 1;
    }

    std::string code = args[0];

    // ── Explanation database ────────────────────────────────────────────
    //
    // Format: {code, {title, body}}
    // Body uses "\n" for line breaks. The CLI prints:
    //   CODE: Title
    //   Body text
    //
    // Categories:
    //   E001–E024   Lexer errors
    //   E100–E247   Parser / syntax errors
    //   E248–E471   Type-checker / semantic errors
    //   E501–E516   Chaperone memory-safety errors
    //   E900–E904   Kernel-mode restriction errors
    //   W001–W522   Warnings (various)

    static const std::map<std::string, std::pair<std::string, std::string>> explanations = {

    // ═══════════════════════════════════════════════════════════════════════
    //  LEXER ERRORS  (E001–E024)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // The lexer is the first stage of compilation. It converts raw source
    // text into tokens. Lexer errors mean the source contains something
    // that isn't valid Angara token syntax.

    {"E001", {"Unterminated block comment",
        "A block comment starting with /* was never closed with */.\n\n"
        "  Example:\n"
        "    /* This comment\n"
        "       is never closed!\n"
        "    func main() {}   // Still inside the comment!\n\n"
        "  Fix: Add */ to close the comment. Every /* must have a matching */."}},

    {"E002", {"Unterminated string literal",
        "A string literal starting with \" was never closed with a matching \".\n\n"
        "  Example:\n"
        "    let s = \"hello world;    // Missing closing quote\n\n"
        "  Fix: Add the closing \" at the end of the string. Strings cannot\n"
        "  span multiple lines — use \"\"\"...\"\"\" for multi-line strings."}},

    {"E003", {"Unterminated string literal (ends with backslash)",
        "A string literal ends with a backslash, which escapes the closing quote.\n\n"
        "  Example:\n"
        "    let s = \"hello\\\\\";    // The \\\" is an escaped quote, not a closer\n\n"
        "  Fix: If you meant a trailing backslash, double it: \"hello\\\\\\\\\".\n"
        "  If the string should end, remove the trailing backslash."}},

    {"E004", {"Invalid escape sequence",
        "An escape sequence in a string or char literal is malformed or out of range.\n"
        "This covers octal escapes (\\0NN must be 0–255) and hex escapes\n"
        "(\\xNN must be 0–255, and must have two hex digits).\n\n"
        "  Example:\n"
        "    let s = \"\\xGG\";        // 'G' is not a hex digit\n"
        "    let c = '\\777';         // 511 > 255 (octal range exceeded)\n\n"
        "  Fix: Use valid escape values. For code points > 255, use \\u{...}."}},

    {"E005", {"Invalid Unicode escape",
        "A Unicode escape sequence \\u{...} is malformed. This covers:\n"
        "  • Unterminated — missing closing '}'\n"
        "  • Empty — \\u{} with no hex digits\n"
        "  • Too many digits — more than 6 hex digits\n"
        "  • Value too large — exceeds U+10FFFF\n"
        "  • Surrogate — in range U+D800–U+DFFF (reserved for UTF-16)\n\n"
        "  Example:\n"
        "    let s = \"\\u{D800}\";    // Surrogate range, not a valid codepoint\n"
        "    let s = \"\\u{110000}\";  // Exceeds Unicode maximum\n\n"
        "  Fix: Use a valid Unicode scalar value (U+0000–U+10FFFF, excluding surrogates)."}},

    {"E006", {"Unknown escape sequence",
        "A backslash followed by a character that is not a recognised escape.\n\n"
        "  Valid escapes: \\\\\", \\\\\\\\, \\n, \\r, \\t, \\b, \\f, \\v, \\a,\n"
        "  \\0NN (octal), \\xNN (hex), \\u{...} (Unicode).\n\n"
        "  Example:\n"
        "    let s = \"\\q\";          // '\\q' is not a valid escape\n\n"
        "  Fix: Use a valid escape, or double the backslash for a literal backslash."}},

    {"E007", {"Unterminated interpolated string",
        "An interpolated string (one containing ${...}) was not closed.\n\n"
        "  Fix: Add the closing \" at the end of the string."}},

    {"E008", {"Unterminated multi-line string",
        "A multi-line string literal (\"\"\"...\"\"\") was not closed.\n"
        "Also covers unterminated raw strings.\n\n"
        "  Fix: Add the closing \"\"\" at the end."}},

    {"E009", {"Invalid byte string or hex literal",
        "Either a byte string literal was not terminated, or a hex literal\n"
        "(0x...) has no digits after the prefix.\n\n"
        "  Example:\n"
        "    let x = 0x;             // No hex digits after 0x\n\n"
        "  Fix: Add hex digits after 0x, or close the byte string."}},

    {"E010", {"Numeric separator must be followed by a digit (hex)",
        "The underscore digit separator '_' in a hex literal must be followed\n"
        "by a valid hex digit.\n\n"
        "  Example:\n"
        "    let x = 0xFF_;          // Trailing separator\n"
        "    let x = 0x__FF;         // Double separator\n\n"
        "  Fix: Place '_' between digits only, e.g., 0xFF_FF_FF."}},

    {"E011", {"Expected binary digits after 0b",
        "A binary literal prefix 0b has no digits after it.\n\n"
        "  Example:\n"
        "    let x = 0b;             // Empty binary literal\n\n"
        "  Fix: Add binary digits (0 or 1), e.g., 0b1010."}},

    {"E012", {"Numeric separator must be followed by a digit (binary)",
        "The underscore '_' in a binary literal must be followed by a binary digit.\n\n"
        "  Fix: Place '_' between digits only, e.g., 0b1010_0101."}},

    {"E013", {"Numeric separator must be followed by a digit (decimal)",
        "The underscore '_' in a decimal literal must be followed by a digit.\n\n"
        "  Fix: Place '_' between digits only, e.g., 1_000_000."}},

    {"E014", {"Numeric separator must be followed by a digit (float)",
        "The underscore '_' in a floating-point literal must be followed by a digit.\n\n"
        "  Fix: Place '_' between digits only."}},

    {"E015", {"Unexpected character",
        "The lexer encountered a character that has no meaning in Angara syntax.\n\n"
        "  Example:\n"
        "    let x = 42 # comment;   // '#' is not a comment character in Angara\n\n"
        "  Fix: Remove the unexpected character. Angara uses // and /* */ for comments."}},

    {"E016", {"Empty char literal",
        "A char literal must contain exactly one character. '' is not allowed.\n\n"
        "  Fix: Provide a character, e.g., 'a', or use a string \"\" for empty text."}},

    {"E017", {"Multi-character char literal",
        "A char literal must contain exactly one code point.\n\n"
        "  Example:\n"
        "    let c = 'ab';           // Two characters in a char literal\n\n"
        "  Fix: Use a single character, or use a string for multiple characters."}},

    {"E018", {"Unterminated char literal",
        "A char literal starting with ' was never closed with a matching '.\n\n"
        "  Fix: Add the closing quote. Char literals contain exactly one character."}},

    {"E019", {"Expected octal digits after 0o",
        "An octal literal prefix 0o has no digits after it.\n\n"
        "  Fix: Add octal digits (0–7), e.g., 0o755."}},

    {"E020", {"Numeric separator must be followed by a digit (octal)",
        "The underscore '_' in an octal literal must be followed by an octal digit.\n\n"
        "  Fix: Place '_' between digits only."}},

    {"E021", {"Expected digits after exponent",
        "A floating-point literal has 'e' or 'E' but no exponent digits.\n\n"
        "  Example:\n"
        "    let x = 1.5e;           // Missing exponent value\n\n"
        "  Fix: Add the exponent, e.g., 1.5e10."}},

    {"E022", {"Numeric separator must be followed by a digit (exponent)",
        "The underscore '_' in the exponent part of a float must be followed by a digit.\n\n"
        "  Fix: Place '_' between digits only."}},

    {"E023", {"Invalid numeric suffix",
        "A numeric literal has an unrecognised type suffix.\n\n"
        "  Example:\n"
        "    let x = 42_kg;          // 'kg' is not a valid suffix\n\n"
        "  Fix: Remove the suffix or use an explicit type annotation: let x as i64 = 42;"}},

    {"E024", {"Invalid numeric literal",
        "A numeric literal is malformed (e.g., leading zeros in a way that\n"
        "isn't a valid octal prefix).\n\n"
        "  Fix: Check the number syntax and correct it."}},

    // ═══════════════════════════════════════════════════════════════════════
    //  PARSER / SYNTAX ERRORS  (E100–E247)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // The parser builds an Abstract Syntax Tree from tokens. Parser errors
    // mean the tokens don't form valid Angara syntax — missing punctuation,
    // incorrect declaration forms, or structural issues.

    // --- Type annotations & modifiers ---
    {"E101", {"Unsupported type modifier",
        "Only '@own' is supported as a type modifier. Other annotations like\n"
        "@unsafe are not valid on type annotations.\n\n"
        "  Fix: Remove the unsupported modifier, or use '@own' if you need\n"
        "  ownership semantics on the type."}},

    {"E102", {"Expected a field name in record type",
        "A record type annotation expects field names (identifiers or strings).\n\n"
        "  Example:\n"
        "    func process(data as { 42: string });  // 42 is not a valid field name\n\n"
        "  Fix: Use valid identifiers or string keys for record fields."}},

    {"E108", {"Expected '>' after generic type arguments",
        "A generic type's argument list was opened with '<' but not closed with '>'.\n\n"
        "  Example:\n"
        "    let x as list<i64;      // Missing '>'\n\n"
        "  Fix: Add the closing '>'."}},

    {"E109", {"Array size error",
        "A fixed-size array type has an invalid size (must be a positive integer).\n\n"
        "  Example:\n"
        "    let buf as i8[0];       // Array size must be positive\n"
        "    let buf as i8[-1];      // Array size must be positive\n\n"
        "  Fix: Use a positive integer size, or use list<T> for dynamic sizing."}},

    {"E111", {"Expected a type annotation",
        "The parser expected a type annotation but found something else.\n"
        "Valid types: primitive (i64, string, ...), function type,\n"
        "record type, or user-defined type.\n\n"
        "  Fix: Add a valid type annotation after 'as'."}},

    {"E113", {"Invalid tuple type",
        "A tuple type requires at least two element types. For a single-element\n"
        "tuple, add a trailing comma: (T,).\n\n"
        "  Example:\n"
        "    let x as (i64);         // Not a tuple — just i64 in parentheses\n"
        "    let x as (i64,);        // Single-element tuple\n\n"
        "  Fix: Use a bare type for single elements, or add a trailing comma."}},

    {"E114", {"Raw array element type must be primitive",
        "Raw C-style arrays (T[N]) only support primitive element types\n"
        "(i8–u64, f32–f64). For complex types, use list<T>.\n\n"
        "  Fix: Use list<T> for complex element types, or use a primitive type."}},

    // --- Trait / contract / data / class declarations ---
    {"E118", {"Trait body can only contain method declarations",
        "A trait body may only contain 'func' declarations (method signatures).\n"
        "Fields, 'let', 'const', and other constructs are not allowed.\n\n"
        "  Fix: Move fields to the implementing class. Traits define behaviour only."}},

    {"E124", {"Data fields cannot have default initializers",
        "Data block fields cannot have default values — they are provided through\n"
        "the auto-generated constructor.\n\n"
        "  Example:\n"
        "    data Point {\n"
        "        let x as i64 = 0;   // Error: no defaults allowed\n"
        "    }\n\n"
        "  Fix: Remove the default value. Provide it when constructing: Point(0, 0)."}},

    {"E128", {"Intrinsic function cannot have a body",
        "An 'intrinsic func' is a compiler builtin — it is replaced at compile time\n"
        "and cannot have an Angara implementation.\n\n"
        "  Fix: Remove the body. The intrinsic is implemented by the compiler backend."}},

    {"E129", {"Expected 'func' after 'intrinsic'",
        "The 'intrinsic' keyword must be followed by 'func'. Only functions\n"
        "can be intrinsic.\n\n"
        "  Fix: Add 'func' after 'intrinsic', or remove 'intrinsic' for non-functions."}},

    {"E130", {"Foreign function cannot have a body",
        "A 'foreign func' declaration imports a C function — its implementation\n"
        "comes from an external library, not from Angara code.\n\n"
        "  Fix: Remove the body (replace '{ ... }' with ';')."}},

    {"E136", {"Expected declaration after 'foreign'",
        "'foreign' must be followed by 'func', 'data', 'union', or 'const'.\n\n"
        "  Fix: Specify what kind of foreign declaration this is."}},

    {"E142", {"'private' is not allowed in a contract",
        "All contract members are implicitly public — the implementing class\n"
        "must be able to fulfil all requirements.\n\n"
        "  Fix: Remove the 'private' specifier."}},

    {"E147", {"Contract methods cannot have a body",
        "Contract methods declare a signature that implementing classes must fulfil.\n"
        "They cannot have an implementation in the contract itself.\n\n"
        "  Fix: Replace '{ ... }' with ';' to declare the signature only."}},

    // --- Attach / import ---
    {"E171", {"Expected module path or name after 'from'",
        "In 'attach X from Y', the 'from' must be followed by a module path\n"
        "(string literal) or module name (identifier).\n\n"
        "  Fix: Provide the module specifier, e.g., attach X from \"io\"."}},

    {"E173", {"Expected module name or path after 'attach'",
        "An 'attach' statement must name what to import.\n\n"
        "  Fix: Provide the module name or a symbol list."}},

    // --- Function declarations ---
    {"E192", {"Variadic parameter must be last",
        "The '...' variadic parameter must be the last parameter in the list.\n\n"
        "  Example:\n"
        "    func printf(fmt as *i8, ...args, extra as i64);  // Error\n"
        "    func printf(fmt as *i8, extra as i64, ...args);  // OK (but unusual)\n"
        "    func printf(fmt as *i8, ...args);                // Typical\n\n"
        "  Fix: Move '...' to the end of the parameter list."}},

    // --- Expression parsing ---
    {"E215", {"Too many function call arguments",
        "A function call has more than 255 arguments, which exceeds the compiler's\n"
        "internal limit.\n\n"
        "  Fix: Reduce the number of arguments. Consider passing a list or record instead."}},

    {"E231", {"Invalid 'super' usage",
        "'super' must be followed by '.' (to call a superclass method) or\n"
        "'(' (to call the parent constructor).\n\n"
        "  Fix: Use 'super.method()' or 'super(args)'."}},

    {"E236", {"Empty parentheses not a valid expression",
        "Empty parentheses '()' are not a valid expression in Angara.\n"
        "(Unlike some languages, Angara does not have a unit/void value.)\n\n"
        "  Fix: Use 'nil' if you need a null value, or remove the empty parentheses."}},

    {"E237", {"Expected an expression",
        "The parser expected an expression (literal, variable, call, if, match,\n"
        "list, or record) but found something else.\n\n"
        "  Fix: Provide a valid expression at this position."}},

    // --- Block / statement structure ---
    {"E176", {"Expected '}' to close block",
        "A block opened with '{' was not closed with '}'.\n\n"
        "  Fix: Add the missing '}'. Check that all braces are balanced."}},

    {"E177", {"Expected ';' after 'break'",
        "The 'break' statement must be followed by a semicolon.\n\n"
        "  Fix: Add ';' after 'break'."}},

    {"E178", {"Expected ';' after 'continue'",
        "The 'continue' statement must be followed by a semicolon.\n\n"
        "  Fix: Add ';' after 'continue'."}},

    {"E200", {"Expected ';' after 'throw'",
        "The 'throw' statement must be followed by a semicolon after the value.\n\n"
        "  Fix: Add ';' after the thrown expression."}},

    // --- Error recovery ---
    {"E115", {"Expected ';' after expression statement",
        "All statements in Angara end with a semicolon.\n\n"
        "  Fix: Add ';' at the end of the statement."}},

    // ═══════════════════════════════════════════════════════════════════════
    //  TYPE-CHECKER / SEMANTIC ERRORS  (E248–E471)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // The type checker validates that your program makes semantic sense:
    // types are compatible, names resolve correctly, contracts and traits
    // are satisfied, and operations are valid on the given types.

    // --- Symbols & scope ---
    {"E250", {"Unknown type",
        "A type name used in an annotation could not be resolved. This means\n"
        "the type is not defined, not in scope, or misspelled.\n\n"
        "  Example:\n"
        "    let x as Foobar;        // 'Foobar' is not defined anywhere\n\n"
        "  Fix: Check the spelling, ensure the type is defined (or attached from\n"
        "  a module), and that it's in scope."}},

    {"E263", {"Symbol already declared",
        "A variable or symbol with this name already exists in the current scope.\n\n"
        "  Example:\n"
        "    let x = 1;\n"
        "    let x = 2;              // Error: 'x' already declared\n\n"
        "  Fix: Use a different name, or remove the duplicate declaration.\n"
        "  If you meant to reassign, use 'x = 2;' (without 'let')."}},

    {"E377", {"Undefined variable",
        "A variable name could not be resolved — it is not declared in the\n"
        "current scope or any enclosing scope.\n\n"
        "  Example:\n"
        "    func foo() {\n"
        "        io.println(1, x);   // 'x' is not defined\n"
        "    }\n\n"
        "  Fix: Declare the variable before using it, check spelling, or attach\n"
        "  the module that exports it."}},

    // --- Type mismatches ---
    {"E265", {"Return type mismatch",
        "The value returned from a function does not match the declared return type.\n\n"
        "  Example:\n"
        "    func foo() -> i64 {\n"
        "        return \"hello\";    // Expected i64, got string\n"
        "    }\n\n"
        "  Fix: Either change the return value to match the declared type, or\n"
        "  change the function's return type annotation."}},

    {"E275", {"Type mismatch in declaration",
        "The initializer expression's type does not match the declared type.\n\n"
        "  Example:\n"
        "    let x as i64 = \"hello\"; // Type mismatch\n\n"
        "  Fix: Make the initializer match the declared type, or change the\n"
        "  type annotation."}},

    {"E319", {"Assignment type mismatch",
        "The right-hand side of an assignment has an incompatible type.\n\n"
        "  Fix: Ensure both sides have compatible types."}},

    // --- Control flow ---
    {"E257", {"'if let' requires an optional type",
        "The 'if let' pattern checks whether an optional contains a value.\n"
        "The right-hand side must be an optional type (T?) or 'any'.\n\n"
        "  Example:\n"
        "    let x as i64 = 42;\n"
        "    if (let val = x) { }    // Error: x is not optional\n\n"
        "  Fix: Use 'if let' with an optional-typed expression, or use a plain\n"
        "  'if (condition)' for boolean checks."}},

    {"E261", {"Iterating over 'any' requires @unsafe",
        "Iterating over a value of type 'any' is not statically safe because\n"
        "the runtime type is unknown. Use an @unsafe block to opt in.\n\n"
        "  Fix: Wrap the iteration in @unsafe { ... }, or narrow the type first."}},

    {"E262", {"Cannot iterate over this type",
        "Only lists and strings can be iterated with 'for ... in ...'.\n\n"
        "  Example:\n"
        "    for (x in 42) { }       // i64 is not iterable\n\n"
        "  Fix: Iterate over a list or string."}},

    {"E264", {"'return' outside function",
        "'return' can only be used inside a function body.\n\n"
        "  Fix: Remove the 'return', or wrap the code in a function."}},

    {"E267", {"Can only throw Exception objects",
        "The 'throw' statement requires an object of type 'Exception' (or a\n"
        "subclass of Exception).\n\n"
        "  Example:\n"
        "    throw 42;               // Error: i64 is not an Exception\n\n"
        "  Fix: Throw an Exception instance: throw Exception(\"message\")."}},

    {"E311", {"'break' outside loop",
        "'break' can only be used inside a loop body (while, for).\n\n"
        "  Fix: Remove the 'break', or place it inside a loop."}},

    {"E312", {"'continue' outside loop",
        "'continue' can only be used inside a loop body (while, for).\n\n"
        "  Fix: Remove the 'continue', or place it inside a loop."}},

    // --- Functions & methods ---
    {"E268", {"'this' outside method",
        "'this' can only be used inside a class method, not in a standalone\n"
        "function or at module scope.\n\n"
        "  Fix: Use 'this' only in class methods, or pass the object as a parameter."}},

    {"E269", {"Parameter missing type annotation",
        "Every function parameter must have an explicit type annotation.\n"
        "Angara does not infer parameter types.\n\n"
        "  Example:\n"
        "    func foo(x) { }         // Error: 'x' has no type\n"
        "    func foo(x as i64) { }  // Correct\n\n"
        "  Fix: Add 'as <type>' after the parameter name."}},

    {"E326", {"Value is not callable",
        "Only functions, classes, and data types can be called with '(...)'.\n\n"
        "  Example:\n"
        "    let x as i64 = 42;\n"
        "    x();                    // Error: i64 is not callable\n\n"
        "  Fix: Check that you are calling the right expression. Maybe you meant\n"
        "  to access a field or method?"}},

    {"E325", {"Calling 'any' requires @unsafe",
        "Calling a value of type 'any' requires an @unsafe block because the\n"
        "compiler cannot statically verify the call signature.\n\n"
        "  Fix: Wrap the call in @unsafe { ... }, or use a more specific type."}},

    // --- Async ---
    {"E415", {"'async' must be followed by a named function",
        "'async' can only be applied to named function declarations, not to\n"
        "anonymous function expressions or lambdas.\n\n"
        "  Fix: Declare a named 'async func': async func name() -> T { ... }"}},

    {"E416", {"Expected 'func' after 'async'",
        "The 'async' keyword must be immediately followed by 'func'.\n\n"
        "  Fix: Change to 'async func ...'."}},

    {"E417", {"Async function cannot be foreign",
        "An 'async' function cannot be 'foreign' — foreign functions are\n"
        "synchronous C imports and cannot participate in the async state machine.\n\n"
        "  Fix: Remove either 'async' or 'foreign'. If you need async C interop,\n"
        "  wrap the foreign call in a non-foreign async function."}},

    {"E418", {"Async function cannot be intrinsic",
        "An 'async' function cannot be 'intrinsic' — intrinsics are synchronous\n"
        "compiler builtins.\n\n"
        "  Fix: Remove either 'async' or 'intrinsic'."}},

    {"E419", {"'await' outside async function",
        "The 'await' keyword can only be used inside an 'async func' body.\n\n"
        "  Example:\n"
        "    func foo() {\n"
        "        let x = await bar(); // Error: not in async func\n"
        "    }\n\n"
        "  Fix: Mark the enclosing function as 'async func', or remove the 'await'."}},

    {"E420", {"Await on non-Future type",
        "The 'await' expression requires a Future<T> value. You can only await\n"
        "the result of an async function call.\n\n"
        "  Fix: Ensure you are awaiting a call to an async function."}},

    // --- Classes & inheritance ---
    {"E268", {"'this' outside method",
        "'this' can only be used inside a class method body.\n\n"
        "  Fix: Move the code into a class method, or use an explicit parameter."}},

    {"E285", {"Superclass not defined",
        "The class named after 'inherits' is not defined anywhere.\n\n"
        "  Fix: Check spelling, or define the superclass before the subclass."}},

    {"E286", {"Cannot inherit from non-class",
        "'inherits' must name a class. You cannot inherit from data types,\n"
        "enums, contracts, or traits.\n\n"
        "  Fix: Inherit from a class, or implement a trait/contract instead."}},

    {"E287", {"Inheritance cycle detected",
        "A class cannot inherit from itself (directly or indirectly).\n\n"
        "  Fix: Break the cycle in the inheritance chain."}},

    // --- Contracts & traits ---
    {"E291", {"Contract not defined",
        "The contract named after 'signs' is not defined in scope.\n\n"
        "  Fix: Check spelling, define the contract, or attach the module that\n"
        "  exports it."}},

    {"E292", {"'signs' requires a contract",
        "'signs' must be followed by a contract name, not a class, trait, or\n"
        "other type.\n\n"
        "  Fix: Use a contract name, or change to 'uses' for traits."}},

    {"E293", {"Class does not fulfil contract — missing field",
        "The class declared 'signs <contract>' but does not provide all required\n"
        "fields specified by the contract.\n\n"
        "  Fix: Add the missing fields to the class."}},

    {"E301", {"Contract method signature mismatch",
        "A method in the class has a different signature than the one required\n"
        "by the contract. The parameter types, return type, or parameter count\n"
        "doesn't match.\n\n"
        "  Fix: Match the contract's method signature exactly."}},

    {"E302", {"Trait not defined",
        "The trait named after 'uses' is not defined in scope.\n\n"
        "  Fix: Check spelling, define the trait, or attach the module that\n"
        "  exports it."}},

    {"E303", {"'uses' requires a trait",
        "'uses' must be followed by a trait name, not a class, contract, or\n"
        "other type.\n\n"
        "  Fix: Use a trait name, or change to 'signs' for contracts."}},

    {"E304", {"Class does not implement required trait method",
        "The class declared 'uses <trait>' but does not implement all methods\n"
        "required by the trait.\n\n"
        "  Fix: Add the missing method implementations to the class."}},

    // --- Generics ---
    {"E251", {"Generic type argument count mismatch",
        "The number of type arguments provided does not match the generic type's\n"
        "declaration.\n\n"
        "  Example:\n"
        "    let x as list<i64, string>;  // list expects exactly 1 argument\n"
        "    let y as Future;             // Future expects 1 type argument\n\n"
        "  Fix: Provide the correct number of type arguments."}},

    {"E254", {"Unknown generic type",
        "The type name before '<...>' is not a known generic type.\n\n"
        "  Fix: Check spelling, or use a non-generic type without '<...>'."}},

    // --- Operators ---
    {"E353", {"Operator '*' type error",
        "The '*' operator can only be used with:\n"
        "  • Two numbers (arithmetic multiplication)\n"
        "  • string * number (string repetition)\n\n"
        "  Fix: Ensure both operands have compatible types."}},

    {"E354", {"Operator '+' type error",
        "The '+' operator can only be used with:\n"
        "  • Two numbers (addition)\n"
        "  • Two strings (concatenation)\n\n"
        "  Fix: Ensure both operands are numbers or both are strings."}},

    {"E360", {"Operator '!' requires boolean operand",
        "The logical NOT operator '!' can only be applied to boolean values.\n\n"
        "  Example:\n"
        "    let x = !42;             // Error: i64 is not bool\n\n"
        "  Fix: Use a boolean expression, or convert: !(x != 0)."}},

    // --- Optionals ---
    {"E333", {"Cannot access member on optional — use '?.'",
        "You tried to access a field or method on an optional-typed value without\n"
        "unwrapping it first.\n\n"
        "  Example:\n"
        "    let opt as string? = nil;\n"
        "    let len = opt.length();  // Error: 'opt' might be nil\n\n"
        "  Fix: Use '?.' for safe access: opt?.length(), or unwrap first:\n"
        "    if (let val = opt) { val.length(); }"}},

    {"E362", {"Logical operator on optional — unwrap first",
        "When using '&&' or '||' on optional values, the unwrapped types\n"
        "must be compatible.\n\n"
        "  Fix: Use '??' to provide a default, or unwrap before the logical op."}},

    {"E363", {"'??' left-hand side must be optional",
        "The null-coalescing operator '??' requires an optional-typed left operand.\n\n"
        "  Example:\n"
        "    let x = 42 ?? 0;        // Error: 42 is not optional\n\n"
        "  Fix: Use '??' only with optional-typed values (T?)."}},

    // --- Match expressions ---
    {"E365", {"Match must have at least one case",
        "A 'match' expression must have at least one case arm.\n\n"
        "  Fix: Add at least one 'case' to the match."}},

    {"E367", {"Non-exhaustive match",
        "Not all possible values of the matched type are covered by the case arms.\n\n"
        "  Example:\n"
        "    enum Color { Red, Green, Blue }\n"
        "    match (c) {\n"
        "        case Red { ... }\n"
        "        case Green { ... }\n"
        "        // Missing Blue!\n"
        "    }\n\n"
        "  Fix: Add case arms for all missing variants, or add a default case '_'."}},

    {"E371", {"Unreachable match case",
        "A match case arm can never be reached because a previous case already\n"
        "covers it.\n\n"
        "  Fix: Remove the unreachable case, or reorder the arms so the more\n"
        "  specific arm comes first."}},

    // --- FFI ---
    {"E379", {"Pointer dereference only valid for FFI pointers",
        "The '*' dereference operator can only be used on FFI pointer types\n"
        "(*i8, *void, etc.), not on Angara reference types.\n\n"
        "  Fix: Use the appropriate access method for the type. Dereference is\n"
        "  for low-level FFI code only."}},

    {"E469", {"Integer-to-pointer cast requires @unsafe",
        "Casting an integer to a pointer type (or vice versa) requires an\n"
        "@unsafe block because it bypasses type safety.\n\n"
        "  Fix: Wrap the cast in @unsafe { ... }, or reconsider the design."}},

    {"E470", {"Cast may lose data",
        "This cast could lose information (e.g., i64 → i32, f64 → i32).\n\n"
        "  Fix: Use @unsafe to suppress, or use a cast that preserves the value."}},

    // --- Type aliases ---
    {"E248", {"Symbol already declared (duplicate)",
        "A top-level symbol (class, trait, contract, data, enum, type alias, or\n"
        "function) with this name is already declared in the same scope.\n\n"
        "  Fix: Rename one of the declarations, or remove the duplicate."}},

    // --- Annotations ---
    {"E393", {"Expected '(' after annotation",
        "Annotations like @consumes and @escape require parentheses with\n"
        "parameter indices.\n\n"
        "  Fix: Add parentheses, e.g., @consumes(0)."}},

    {"E394", {"Invalid annotation parameter",
        "The parameter inside an annotation must be a valid integer index\n"
        "referring to a function parameter.\n\n"
        "  Fix: Provide a valid parameter index (0-based)."}},

    {"E395", {"Expected ')' after annotation parameters",
        "An annotation's parameter list must be closed with ')'.\n\n"
        "  Fix: Add the closing ')'."}},

    // --- Interpolated strings ---
    {"E398", {"Expected '\"' after '$' in string interpolation",
        "In string interpolation, '${' must be followed by an expression and\n"
        "then '}'.\n\n"
        "  Fix: Ensure the interpolation hole has the form ${expression}."}},

    {"E399", {"Unterminated interpolated string",
        "An interpolated string was opened but never properly closed.\n\n"
        "  Fix: Close the string with '\"' and ensure all ${...} holes are closed."}},

    {"E400", {"Unterminated expression hole in interpolated string",
        "A '${' inside an interpolated string was not closed with '}'.\n\n"
        "  Fix: Add the closing '}'."}},

    // --- Other common type errors ---
    {"E384", {"Type-check on 'any' requires @unsafe",
        "Using the 'is' operator to check the runtime type of an 'any' value\n"
        "requires an @unsafe block.\n\n"
        "  Fix: Wrap in @unsafe { ... }, or use a statically-typed value."}},

    {"E376", {"'this' outside class method",
        "Cannot use 'this' outside of a class method body.\n\n"
        "  Fix: Move the code into a class method."}},

    {"E372", {"'super' outside class method",
        "Cannot use 'super' outside of a class method.\n\n"
        "  Fix: Move the code into a class method."}},

    {"E373", {"'super' but no superclass",
        "'super' is used but the enclosing class does not have 'inherits'.\n\n"
        "  Fix: Add 'inherits <ParentClass>' to the class, or remove 'super'."}},

    {"E375", {"Superclass method is private",
        "The method you are trying to call via 'super' is declared 'private'\n"
        "in the superclass.\n\n"
        "  Fix: Change the superclass method to 'protected' or 'public', or\n"
        "  don't try to override/call it."}},

    // ═══════════════════════════════════════════════════════════════════════
    //  CHAPERONE MEMORY-SAFETY ERRORS  (E501–E516)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // The Chaperone is Angara's compile-time ownership and borrow checker.
    // It statically verifies that every allocation is released exactly once,
    // no use-after-free occurs, and no data races exist.
    //
    // See CHAPERONE.md for the full design document.

    {"E501", {"Unfolded molecule — memory leak",
        "A tracked (owned) allocation is not dropped on at least one control-flow\n"
        "path. The allocation goes out of scope without being released.\n\n"
        "  Common causes:\n"
        "  • Early return without dropping a tracked variable\n"
        "  • Throwing an exception without dropping tracked variables\n"
        "  • Reassigning a tracked variable (the old value leaks)\n"
        "  • Module-level (global) tracked allocation — no scope to drop it\n\n"
        "  Example:\n"
        "    func read(path as string) -> string? {\n"
        "        let buf = Buffer(1024);\n"
        "        if (buf.open(path) < 0) {\n"
        "            return nil;         // Leak! 'buf' not dropped\n"
        "        }\n"
        "        let data = buf.read_all();\n"
        "        drop buf;\n"
        "        return data;\n"
        "    }\n\n"
        "  Fix: Add 'drop buf;' before the early return, or wrap the allocation\n"
        "  in a try/finally that drops it on every path. Also consider using\n"
        "  @consumes on foreign functions that take ownership."}},

    {"E502", {"Dead reference — use-after-free",
        "A variable is used after it has already been dropped. This would\n"
        "read freed memory at runtime.\n\n"
        "  Example:\n"
        "    let buf = Buffer(1024);\n"
        "    drop buf;\n"
        "    buf.write(data);            // Use-after-free!\n\n"
        "  Fix: Move the 'drop' to after all uses of the variable, or restructure\n"
        "  the code so the variable is not needed after the drop."}},

    {"E503", {"Double denaturation — double-free",
        "A variable is dropped more than once, or dropped after its ownership\n"
        "was transferred to another variable.\n\n"
        "  Common causes:\n"
        "  • Explicit 'drop x;' followed by another 'drop x;'\n"
        "  • 'drop x;' after 'let y = x;' (ownership moved to y)\n"
        "  • 'drop' called on a variable whose ownership was consumed by a\n"
        "    @consumes-annotated foreign function\n"
        "  • drop called on a variable that was captured by a closure\n\n"
        "  Example:\n"
        "    let a = Buffer(100);\n"
        "    let b = a;                  // Ownership moves from a to b\n"
        "    drop a;                     // Double-free! 'a' no longer owns it\n"
        "    drop b;\n\n"
        "  Fix: Only drop the current owner. After 'let b = a', 'b' owns the\n"
        "  allocation — drop 'b', not 'a'."}},

    {"E504", {"Tangled molecule — reference cycle",
        "The ownership graph contains a cycle: a tracked type contains fields\n"
        "of tracked types that eventually refer back to the original type.\n"
        "This would cause a leak (reference counting can't collect cycles).\n\n"
        "  Example:\n"
        "    class Node {\n"
        "        let next as Node;       // Cycle! Node → Node → ...\n"
        "    }\n\n"
        "  Fix: Use 'ref<T>' for the back-edge to break the cycle:\n"
        "    class Node {\n"
        "        let next as ref<Node>;  // Non-owning reference\n"
        "    }\n"
        "  The owning chain must form a DAG (directed acyclic graph)."}},

    {"E505", {"Escaped molecule — tracked allocation escapes",
        "A tracked (owned) allocation is placed into an untracked container\n"
        "(a list or record literal) or captured by a closure. The container\n"
        "or closure isn't tracked, so the allocation can never be dropped.\n\n"
        "  Common causes:\n"
        "  • Inserting an owned value into a list literal: [owned_val]\n"
        "  • Storing an owned value in a record: { field: owned_val }\n"
        "  • Capturing an owned value in a closure/lambda\n"
        "  • Passing an owned value to a function that stores it (use @escape)\n\n"
        "  Example:\n"
        "    let buf = Buffer(1024);\n"
        "    let items = [buf];          // 'buf' escapes into untracked list!\n"
        "    // 'buf' can never be dropped now\n\n"
        "  Fix: Drop the value before it escapes, or use a tracked container.\n"
        "  For foreign functions that store arguments, use @escape(i) annotation."}},

    {"E506", {"Incomplete fold — loop-body drop",
        "A tracked variable is dropped inside a loop body without being\n"
        "reassigned on every iteration. On the second iteration, the variable\n"
        "is dropped again — a double-free.\n\n"
        "  Example:\n"
        "    let buf = Buffer(100);\n"
        "    while (cond) {\n"
        "        buf.write(data);\n"
        "        drop buf;               // Dropped on iteration 1\n"
        "        // On iteration 2, buf is already dropped — double-free!\n"
        "    }\n\n"
        "  Fix: Reassign 'buf' after each drop, or move the allocation inside\n"
        "  the loop so a fresh one is created each iteration."}},

    {"E507", {"Moved molecule — use-after-move",
        "A variable whose ownership was moved (via 'let y = x' or 'x = y'\n"
        "of a tracked type, or 'this.f = y' in a method) is read after the move.\n"
        "The variable no longer owns the allocation.\n\n"
        "  Example:\n"
        "    let a = Buffer(100);\n"
        "    let b = a;                  // Ownership moved to b\n"
        "    a.write(data);              // Use-after-move! 'a' no longer owns\n\n"
        "  Fix: Use the new owner ('b') instead of the old one ('a')."}},

    {"E509", {"Dangling borrow — ref after drop",
        "A 'ref<T>' (non-owning reference) is read after the value it refers\n"
        "to has been dropped or moved. This would read freed memory.\n\n"
        "  Example:\n"
        "    let buf = Buffer(100);\n"
        "    let r as ref<Buffer> = &buf;\n"
        "    drop buf;\n"
        "    r.write(data);              // Dangling borrow! 'buf' was dropped\n\n"
        "  Fix: Ensure all refs go out of scope before dropping the referent.\n"
        "  Drop the refs first, then drop the owned value."}},

    {"E510", {"Thread escape — cross-thread use-after-transfer",
        "A tracked allocation's ownership was transferred to another thread\n"
        "via spawn(). Using or dropping it in the parent thread afterward\n"
        "is a data race / use-after-transfer / double-free.\n\n"
        "  Example:\n"
        "    let buf = Buffer(100);\n"
        "    spawn(worker, buf);         // Ownership moves to spawned thread\n"
        "    drop buf;                   // Error: buf no longer owned here\n\n"
        "  Fix: After spawn(), don't use the transferred variable. The spawned\n"
        "  thread is responsible for dropping it."}},

    {"E511", {"Not sendable — type cannot cross thread boundary",
        "A tracked type is not marked '@sendable' and cannot be transferred\n"
        "to another thread via spawn().\n\n"
        "  Example:\n"
        "    class Widget {               // Not @sendable\n"
        "        let state as i64;\n"
        "    }\n"
        "    let w = Widget();\n"
        "    spawn(worker, w);           // Error: Widget is not @sendable\n\n"
        "  Fix: Add '@sendable' to the type declaration:\n"
        "    @sendable class Widget { ... }\n"
        "  This asserts that the type is safe to transfer across threads."}},

    {"E512", {"Double lock — potential deadlock",
        "A Mutex was locked while already locked by the same thread.\n"
        "This would cause a deadlock at runtime.\n\n"
        "  Fix: Unlock before locking again, or restructure to avoid nested\n"
        "  locking of the same mutex."}},

    {"E513", {"Double unlock — mutex not locked",
        "A Mutex was unlocked without being locked. This is a logic error\n"
        "that would cause undefined behaviour at runtime.\n\n"
        "  Fix: Ensure every unlock() has a matching lock() before it."}},

    {"E514", {"Shared borrow across thread boundary (warning)",
        "A ref<T> to a tracked value exists in the parent thread while the\n"
        "value is transferred to another thread via spawn(). The ref and the\n"
        "spawned thread can access the same memory concurrently — a data race.\n\n"
        "  Note: This fires as W514 (warning) for Sync types, and E515 (error)\n"
        "  for non-Sync types.\n\n"
        "  Fix: Drop all refs to the value before calling spawn()."}},

    {"E515", {"Non-Sync ref across thread boundary",
        "A ref<T> to a non-Sync type exists when the value is spawned.\n"
        "Without synchronisation, concurrent access through the ref and the\n"
        "spawned thread is a data race.\n\n"
        "  Fix: Either mark the type '@sync' and add synchronisation (e.g.,\n"
        "  Mutex), or drop the ref before spawning."}},

    {"E516", {"Async escape — non-Send across await",
        "A tracked value that is not marked @sendable is held across an\n"
        "'await' point. The async function may resume on a different thread,\n"
        "so all tracked values that survive an await must be Send.\n\n"
        "  Example:\n"
        "    async func process() {\n"
        "        let buf = Buffer(100);  // Not @sendable\n"
        "        let x = await fetch();  // Await point\n"
        "        buf.write(x);           // buf is live across await!\n"
        "    }\n\n"
        "  Fix: Add '@sendable' to the type, drop the value before the await,\n"
        "  or restructure to avoid holding non-Send values across await points."}},

    // ═══════════════════════════════════════════════════════════════════════
    //  KERNEL-MODE RESTRICTION ERRORS  (E900–E904)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // When compiling with --kernel, certain userspace features are
    // unavailable because the kernel has no libc, no dynamic loader,
    // and no pthreads.

    {"E900", {"'throw' not allowed in kernel mode",
        "'throw' (exceptions) requires setjmp/longjmp and formatted output\n"
        "routines which are not available in the kernel.\n\n"
        "  Fix: Use error-return codes instead of exceptions in kernel code."}},

    {"E901", {"'try'/'catch' not allowed in kernel mode",
        "Exception handling (try/catch) requires runtime support that is not\n"
        "available in the kernel.\n\n"
        "  Fix: Use explicit error checking instead of try/catch."}},

    {"E902", {"'spawn()' not allowed in kernel mode",
        "spawn() creates a new thread via pthreads, which are not available\n"
        "in the kernel.\n\n"
        "  Fix: Use kernel-appropriate concurrency primitives (work queues,\n"
        "  kthreads) through FFI instead of spawn()."}},

    {"E903", {"'Mutex' not allowed in kernel mode",
        "Mutex uses pthread mutexes, which are not available in the kernel.\n\n"
        "  Fix: Use kernel synchronisation primitives (spinlocks, mutexes\n"
        "  from <linux/mutex.h>) through FFI."}},

    {"E904", {"'attach' not allowed in kernel mode",
        "Native module attachment requires the dynamic loader (dlopen),\n"
        "which is not available in the kernel.\n\n"
        "  Fix: Link required functionality statically, or use FFI to call\n"
        "  kernel functions directly."}},

    // ═══════════════════════════════════════════════════════════════════════
    //  WARNINGS  (W001–W522)
    // ═══════════════════════════════════════════════════════════════════════
    //
    // Warnings do not prevent compilation but indicate code that is likely
    // wrong. Use -Wno-<code> to suppress individual warnings, or -Werror
    // to treat all warnings as errors.

    {"W001", {"Division or modulo by zero",
        "The compiler detected that the divisor in a division or modulo\n"
        "operation is the constant zero.\n\n"
        "  Example:\n"
        "    let x = 10 / 0;            // W001\n"
        "    let y = 10 % 0;            // W001\n\n"
        "  Fix: Ensure the divisor is non-zero. If this is intentional in a\n"
        "  generic context, suppress with -Wno-W001."}},

    {"W002", {"Using 'nil' in a logical expression",
        "The compiler detected 'nil' used as an operand of '&&' or '||'.\n"
        "Since nil is falsy, 'nil && x' always evaluates to nil and\n"
        "'nil || x' always evaluates to x.\n\n"
        "  Example:\n"
        "    let result = nil && true;   // W002: always nil\n\n"
        "  Fix: Check if you meant to use an optional check instead, or\n"
        "  suppress with -Wno-W002 if this is intentional."}},

    {"W003", {"Unused variable",
        "A variable was declared but never read before going out of scope.\n\n"
        "  Example:\n"
        "    func foo() {\n"
        "        let x = 42;             // W003: 'x' is never used\n"
        "    }\n\n"
        "  Fix: Either use the variable, prefix it with '_' to signal\n"
        "  intentional discard ('_x'), or remove the declaration entirely.\n"
        "  Suppress with -Wno-W003."}},

    {"W260", {"Type compatibility check bypassed by @unsafe",
        "A type compatibility check that would normally be an error is\n"
        "downgraded to a warning because it occurs inside an @unsafe block.\n\n"
        "  This warns that the compiler could not statically verify the\n"
        "  type compatibility, but allowed it due to @unsafe.\n\n"
        "  Fix: Verify that the types are actually compatible. Suppress with\n"
        "  -Wno-W260 if intentional."}},

    {"W270", {"Variable-related warning",
        "A variable declaration has a potential issue detected by the compiler.\n\n"
        "  Fix: Review the variable declaration and the compiler's message.\n"
        "  Suppress with -Wno-W270."}},

    {"W271", {"Unused import",
        "A symbol attached (imported) from a module is never used in the file.\n\n"
        "  Example:\n"
        "    attach io;                  // W271 if no io.* calls\n"
        "    export func main() -> i64 {\n"
        "        return 0;\n"
        "    }\n\n"
        "  Fix: Remove the unused import, or use the imported symbol.\n"
        "  Suppress with -Wno-W271."}},

    // Chaperone warnings — see CHAPERONE.md for full details.

    {"W510", {"Asymmetric if/else branch handling",
        "A tracked variable is handled differently on the if and else branches\n"
        "(e.g., dropped in one branch but not the other). This suggests a\n"
        "potential logic error or leak.\n\n"
        "  Fix: Ensure consistent handling of tracked variables in all branches.\n"
        "  Suppress with -Wno-W510."}},

    {"W514", {"Sync ref across thread boundary",
        "A ref<T> to a Sync type exists when the value is spawned to another\n"
        "thread. The type is marked @sync, but you must still ensure proper\n"
        "synchronisation (e.g., via Mutex).\n\n"
        "  Fix: Verify that synchronisation is in place, or drop the ref before\n"
        "  spawning. Suppress with -Wno-W514 if already synchronised."}},

    {"W520", {"Chaperone ownership pattern warning",
        "The Chaperone detected an ownership pattern that may indicate a bug —\n"
        "possibly a missing @consumes or @escape annotation on a foreign/module\n"
        "function.\n\n"
        "  Fix: Review the ownership flow. You may need to add @consumes or\n"
        "  @escape annotations to foreign function declarations.\n"
        "  Suppress with -Wno-W520."}},

    {"W521", {"Built-in type may leak",
        "A built-in heap-allocated type (string, list, record) is not being\n"
        "tracked with the same rigour as owned/class types. This is a warning\n"
        "rather than a hard error to avoid breaking existing code.\n\n"
        "  Fix: Ensure the value is properly managed. In most cases this is\n"
        "  benign (built-in types are reference-counted internally), but be\n"
        "  aware of potential leaks in long-running or kernel code.\n"
        "  Suppress with -Wno-W521."}},

    {"W522", {"Chaperone analysis warning",
        "The Chaperone detected a potential issue during ownership analysis.\n\n"
        "  Fix: Review the diagnostic message from the compiler for specifics.\n"
        "  Suppress with -Wno-W522."}},

    }; // end explanations map

    // ── Lookup & output ──────────────────────────────────────────────────

    auto it = explanations.find(code);
    if (it != explanations.end()) {
        // Detailed explanation available
        std::cout << CLR_BOLD << CLR_CYAN << code << CLR_RESET
                  << ": " << it->second.first << "\n\n";
        std::cout << it->second.second << "\n";
    } else if (!code.empty() && (code[0] == 'W' || code[0] == 'E')) {
        // Fallback with category-specific guidance
        printCategoryFallback(code, code[0]);
    } else {
        std::cerr << CLR_RED << "[ERROR] Unknown code format: " << code
                  << CLR_RESET << "\n";
        std::cerr << "  Expected a code like W003 or E377.\n";
        std::cerr << "  Codes are printed in compiler diagnostics, e.g.:\n";
        std::cerr << "    [E377] Undefined variable 'x'.\n";
        std::cerr << "    [W003] Unused variable 'y'.\n";
        return 1;
    }

    return 0;
}

} // namespace angara
