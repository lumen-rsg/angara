# Angara

<p align="center">
  <img src="angara.jpg" alt="Angara Language Logo" width="150">
</p>

<p align="center">
  A modern, statically-typed systems programming language designed for clarity, safety, and pragmatic interoperability with C.
</p>

<p align="center">
  <a href="https://github.com/lumen-rsg/angara/actions">
    <img src="https://github.com/lumen-rsg/angara/workflows/CI/badge.svg" alt="CI Status">
  </a>
  <a href="https://github.com/lumen-rsg/angara/wiki">
    <img src="https://img.shields.io/badge/docs-Language%20Guide-blue.svg" alt="Language Guide">
  </a>
  <a href="https://github.com/your-repo/angara/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-lightgrey.svg" alt="License">
  </a>
</p>

---

## What is Angara?

Angara is a new programming language that compiles to C, combining the performance and low-level control of C with the safety and expressiveness of modern language design. It's built for developers who want to write efficient systems software without sacrificing readability or compile-time guarantees.

The core philosophy of Angara is **explicit is better than implicit**. With mandatory type annotations, a robust module system, and a clear Foreign Function Interface (FFI), Angara code is designed to be easy to read, maintain, and reason about.

## Key Features

*   **Modern Type System:** Enjoy powerful features like algebraic data types (`enum`), pattern matching (`match`), optionals for null safety (`?`), and structured data blocks (`data`).
*   **Safety by Default:** Compile-time checks eliminate entire classes of bugs like null pointer exceptions and type mismatches.
*   **Seamless C Interoperability:** The `foreign` keyword provides a first-class FFI to call any C library directly and map C structs to Angara types without writing complex glue code.
*   **Simple Concurrency:** Built-in support for threads (`spawn`, `join`) and mutexes (`Mutex`) makes parallel programming straightforward.
*   **Object-Oriented & Functional:** Supports classes, inheritance, and interfaces (`contract`), while also enabling functional patterns with immutable constants and expressive data types.
*   **Self-Contained Build:** Uses CMake to build the compiler, runtime, and native modules, providing a consistent development experience on Linux and macOS.

## Quick Start: Hello, world!

Here's a taste of what Angara code looks like.

```angara
// Import the standard I/O module.
attach io;

// All programs start with an exported `main` function.
// It must be explicitly typed to return an integer.
export func main() -> i64 {
  io.println(1, "Hello, world!");

  return 0; // Return 0 for success.
}
```

## Building from Source

Angara is easy to build. You'll need a modern C/C++ compiler (GCC or Clang), CMake, and Git. On macOS, you will also need the Homebrew package manager to install dependencies.

#### 1. Clone the Repository

```sh
git clone https://github.com/lumina-rsg/angara.git
cd angara
```

#### 2. Install Dependencies

*   **On Debian/Ubuntu:**
    ```sh
    sudo apt-get update
    sudo apt-get install build-essential cmake pkg-config libcurl4-openssl-dev libwebsockets-dev librabbitmq-dev
    ```
*   **On macOS:**
    ```sh
    brew install cmake pkg-config curl libwebsockets rabbitmq-c
    ```

#### 3. Configure and Build

```sh
# Create a build directory
mkdir build
cd build

# Configure the project with CMake
cmake ..

# Compile everything (the compiler, runtime, and all modules)
make
```

After the build completes, the `angc` compiler and `angara-ls` language server executables will be available in the `build/` directory.

#### 4. Compile and Run Your First Program

```sh
# Create your hello.an file with the code from the example above.
# Then, from the build directory, run the compiler:
./angc ../path/to/your/hello.an

# This will create an executable named 'hello'. Run it:
./hello
# Output: Hello, Ulumina!
```

## Dive Deeper

Ready to learn more? The complete language specification, standard library documentation, and tutorials are available on the **[Official Angara Wiki](https://github.com/lumen-rsg/angara/wiki)**.

## Contributing

Angara is an open-source project, and we welcome contributions! Whether it's improving the compiler, adding new standard library modules, or enhancing the documentation, there are many ways to get involved. Please see our contribution guidelines for more information.

## License

Angara is distributed under the terms of the MIT license. See `LICENSE` for details.
