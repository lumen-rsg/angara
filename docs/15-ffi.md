# Foreign Function Interface

Call C functions, map C structs, and use pointers -- no glue code required.

---

## Importing Functions

Use `foreign func` to declare a C function's signature. No header include is
needed — the compiler infers linkage from the signature and links against libc
automatically (and you can link additional libraries via the build system).

```angara
foreign func abs(val as i64) -> i64;
foreign func malloc(size as u64) -> *void;
foreign func free(ptr as *void) -> nil;
```

Call them like any Angara function:

```angara
let v = abs(-42);
io.println(1, string(v));               // 42
```

## Importing Structs

Map C structs to Angara using `foreign data`. Fields can use fixed arrays for inline C arrays:

```angara
foreign data utsname {
    sysname  as i8[256];
    nodename as i8[256];
    release  as i8[256];
    version  as i8[256];
    machine  as i8[256];
}
```

Construct and pass by value:

```angara
let info = utsname();
let result as i32 = uname(info);
io.println(1, "OS: " + info.sysname);
io.println(1, "Machine: " + info.machine);
```

## Pointer Types

FFI pointer types for C interop:

```angara
foreign func malloc(size as u64) -> *void;
foreign func free(ptr as *void) -> nil;
foreign func strlen(s as string) -> i64;
```

Pointer types include `*void`, `*i8`, `**char`, and other C pointer types.

## Callbacks

Pass Angara functions as C callback parameters:

```angara
foreign func reduce_sum(arr as *void, count as i32,
                        fn as function(i64, *void) -> i64,
                        userdata as *void) -> i64;

let arr = malloc(5 * 8 as u64);
let sum = reduce_sum(arr, 5 as i32, func(elem as i64, ud as *void) -> i64 {
    return 1 as i64;
});
```

## Foreign Constants

```angara
foreign const errno as i32;
```

## Intrinsic Functions

Compiler-provided builtins for bare-metal and low-level programming. These do not require any header or library:

```angara
intrinsic func peek32(addr as i64) -> i64;
intrinsic func poke32(addr as i64, val as i64) -> nil;
intrinsic func halt() -> nil;
```

## String Ownership

Use `@own` to adopt a C string without copying:

```angara
foreign func strdup(s as string) -> @own string;
```

## Complete Example

```angara
foreign func abs(x as i64) -> i64;
foreign func strlen(s as string) -> i64;

foreign data utsname {
    sysname  as i8[256];
    nodename as i8[256];
    release  as i8[256];
    version  as i8[256];
    machine  as i8[256];
}

foreign func uname(buf as utsname) -> i32;
foreign func malloc(size as u64) -> *void;
foreign func free(ptr as *void) -> nil;

attach io;

export func main() -> i64 {
    // Primitive FFI
    let v = abs(-42);
    io.println(1, string(v));               // 42

    let n = strlen("hello");
    io.println(1, string(n));               // 5

    // Struct FFI with inline char arrays
    let info = utsname();
    let result as i32 = uname(info);
    io.println(1, "  OS: " + info.sysname);
    io.println(1, "  Node: " + info.nodename);
    io.println(1, "  Release: " + info.release);
    io.println(1, "  Machine: " + info.machine);

    return 0;
}
```
