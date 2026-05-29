# Lexical Structure

Comments, keywords, literals, and tokens that make up Angara source code.

---

## Comments

```angara
// Single-line comment

/* Multi-line
   comment */
```

## Keywords

Angara reserves the following keywords:

```
let const func class data enum
if orif else while for in
return break continue throw try catch
match case
attach export foreign intrinsic
public private inherits signs uses
true false nil
typeof
```

## Literals

### Integers

Support decimal, hexadecimal, and binary notation with `_` separators:

```angara
42
0xFF
0b1010
1_000_000
```

### Floating-Point

```angara
3.14
2.0
```

### Strings

String literals with escape sequences and multi-line support:

```angara
"hello world"
"escape: \n \t \\ \" "
"""
  Multi-line
  string literal
"""
```

Escape sequences: `\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f`, `\v`, `\a`, octal `\0NN`, hex `\xNN`.

### Booleans and Nil

```angara
true
false
nil
```

## Semicolons

All statements end with a semicolon `;`.
