# Build System

Project scaffolding and the `.abs` build specification format.

---

## Creating a Project

```sh
angc init              # Scaffold a new project (interactive)
angc init app          # Application template
angc init lib          # Library template
angc init embedded     # Embedded/bare-metal template
angc init gui          # GUI application template
```

## Building

```sh
angc                   # Build project in current directory (finds .abs file)
angc run               # Build and run
angc clean             # Remove build artifacts
angc publish           # Build and copy to publish directory
```

## Testing

```sh
angc test              # Run test suite
angc test tests/       # Run tests in specific directory
```

## .abs File Format

Angara projects use `.abs` (Angara Build Specification) files to define project configuration. A `.abs` file supports:

- **Project name** and **type** (APP or LIBRARY)
- **Entry point** (main source file)
- **Dependencies** (other Angara modules or libraries)
- **Native module compilation** (inline C/C++ sources)
- **Build profiles** (debug/release)
- **Pre/post build steps** (shell commands)
- **Target triples** (for cross-compilation)
- **Optimization levels**

## Project Layout

A typical Angara project:

```
my_project/
├── project.abs        # Build specification
├── src/
│   └── main.an        # Entry point
├── modules/           # Local modules
│   └── utils.an
└── tests/
    └── test_main.an
```
