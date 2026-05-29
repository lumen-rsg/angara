# Module System

How to import, export, and organize code with Angara's module system.

---

## Importing Modules

Use `attach` to import modules:

```angara
attach io;                                       // Import entire module
attach math;                                     // Import entire module
```

### Selective Import

Import specific symbols from a module:

```angara
attach Server, WebSocket, createServer from websocket;
```

### File Path Import

Import from a specific file:

```angara
attach Identifiable from "contracts.an";
```

### Aliased Import

Rename a module on import:

```angara
attach io as stdout;
```

## Module Types

Modules come in three forms:

1. **Native C modules** -- Dynamically loaded shared libraries (`.dylib`/`.so`/`.dll`) installed in `/opt/angara/modules/`
2. **Angara source modules** -- `.an` files compiled on the fly from `/opt/angara/src/modules/` or local paths
3. **Project modules** -- Resolved from the `.abs` build specification

## Exporting Symbols

Use `export` to make functions, classes, data types, enums, and contracts visible to importers:

```angara
export contract Identifiable {
  public:
    let id as i64;
}

export func helper() -> i64 { return 42; }

export data Point {
    let x as i64;
    let y as i64;
}

export enum Color {
    Red,
    Green,
    Blue
}
```

Symbols without `export` are private to their file and cannot be imported by other modules.
