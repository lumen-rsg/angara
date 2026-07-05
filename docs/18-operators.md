# Operators

All operators and their precedence.

---

## Arithmetic

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition / string concatenation | `3 + 4` → `7` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication / string repetition | `"ab" * 3` → `"ababab"` |
| `/` | Division | `10 / 3` → `3` |
| `%` | Modulus | `10 % 3` → `1` |
| `++` | Increment | `x++` |
| `--` | Decrement | `x--` |

## Assignment

| Operator | Description | Example |
|----------|-------------|---------|
| `=` | Assignment | `x = 42` |
| `+=` | Add and assign | `x += 5` |
| `-=` | Subtract and assign | `x -= 3` |
| `*=` | Multiply and assign | `x *= 2` |
| `/=` | Divide and assign | `x /= 2` |

## Comparison

| Operator | Description | Example |
|----------|-------------|---------|
| `==` | Equal | `10 == 10` → `true` |
| `!=` | Not equal | `10 != 5` → `true` |
| `<` | Less than | `3 < 5` → `true` |
| `>` | Greater than | `5 > 3` → `true` |
| `<=` | Less than or equal | `3 <= 3` → `true` |
| `>=` | Greater than or equal | `5 >= 5` → `true` |
| `is` | Type test | `x is string` |
| `as` | Type cast | `x as i64` |

## Logical

| Operator | Description | Example |
|----------|-------------|---------|
| `&&` | Logical AND | `true && false` → `false` |
| `\|\|` | Logical OR | `true \|\| false` → `true` |
| `!` | Logical NOT | `!false` → `true` |

## Bitwise

| Operator | Description | Example |
|----------|-------------|---------|
| `&` | Bitwise AND | `0xFF & 0x0F` |
| `\|` | Bitwise OR | `0x0F \| 0xF0` |
| `^` | Bitwise XOR | `0xFF ^ 0x0F` |
| `~` | Bitwise NOT | `~0x0F` |
| `<<` | Left shift | `1 << 4` |
| `>>` | Right shift | `16 >> 2` |

## Other

| Operator | Description | Example |
|----------|-------------|---------|
| `? :` | Ternary conditional | `x > 0 ? x : -x` |
| `??` | Nil coalescing | `name ?? "default"` |
| `?.` | Optional chaining | `user?.name` |
| `..` | Range (exclusive) | `0..10` |
| `...` | Range (inclusive) | `0...10` |

## Built-in Functions

| Function | Description | Example |
|----------|-------------|---------|
| `typeof(x)` | Runtime type name as string | `typeof(42)` → `"i64"` |
| `len(x)` | Length of strings and lists | `len("hello")` → `5` |
| `string(x)` | Convert any value to string | `string(3.14)` → `"3.14"` |
| `char(x)` | Convert integer code point to char | `char(65)` → `'A'` |
| `i64(x)` | Cast to i64 | `i64(3.7)` → `3` |
| `f64(x)` | Cast to f64 | `f64(42)` → `42.0` |

## Operator Precedence

From lowest to highest:

| Priority | Operators | Description |
|----------|-----------|-------------|
| 1 (lowest) | `=`, `+=`, `-=`, `*=`, `/=` | Assignment |
| 2 | `..`, `...` | Range |
| 3 | `??` | Nil coalescing |
| 4 | `? :` | Ternary |
| 5 | `\|\|` | Logical OR |
| 6 | `&&` | Logical AND |
| 7 | `==`, `!=`, `is`, `as` | Equality / Type test / Cast |
| 8 | `<`, `>`, `<=`, `>=` | Comparison |
| 9 | `\|`, `^`, `&` | Bitwise OR / XOR / AND |
| 10 | `<<`, `>>` | Bitwise shift |
| 11 | `+`, `-` | Addition / Subtraction |
| 12 | `*`, `/`, `%` | Multiplication / Division |
| 13 | `!`, `-`, `~`, `++`, `--` | Unary |
| 14 | `.`, `?.`, `()`, `[]` | Call / Member / Subscript |
| 15 (highest) | literals, identifiers, `()` | Primary |
