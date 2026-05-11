# BCause - B compiler for modern systems

**BCause** is a compiler for the **B** programming language, developed by *Ken Thompson* and *Dennis Ritchie* at *Bell Labs* in *1969*, later getting replaced by **C**. BCause is written in C99 and relies on a minimal set of dependencies, namely `libc` and the GNU binutils.

This repository also includes a `libb.a` implementation, B's standard library. It requires zero dependencies, not even libc.

BCause is implemented as a small single-pass compiler in ~2500 lines of pure C99 code. Therefore, it features small compile times with a very low memory footprint.

This fork also contains a set of reading notes and source annotations for people who want to study BCause as a compact compiler/runtime specimen. If your goal is to understand the implementation, start with [`READING_ORDER.md`](READING_ORDER.md).

## Reading the implementation

BCause is small enough to read, but it is still easy to enter from the wrong end. The recommended route is:

1. [`READING_ORDER.md`](READING_ORDER.md) — linear reading order.
2. [`NOTES.md`](NOTES.md) — high-level learning map.
3. [`src/README.md`](src/README.md) — source tree overview.
4. [`src/compiler/README.md`](src/compiler/README.md) — compiler overview.
5. [`src/libb/README.md`](src/libb/README.md) — runtime overview.
6. [`docs/README.md`](docs/README.md) — long-form documentation index.

The main deep-dive documents are:

- [`docs/compiler-pipeline.md`](docs/compiler-pipeline.md) — source to executable pipeline.
- [`docs/diagnostics-and-parser.md`](docs/diagnostics-and-parser.md) — character-level parser and diagnostics.
- [`docs/data-layout.md`](docs/data-layout.md) — B word, scalar/vector layout, strings, character packing.
- [`docs/expression-lowering.md`](docs/expression-lowering.md) — lvalue/rvalue and expression code generation.
- [`docs/statement-lowering.md`](docs/statement-lowering.md) — labels, jumps, and statement lowering.
- [`docs/abi-and-stack.md`](docs/abi-and-stack.md) — System V AMD64 ABI and stack frame model.
- [`docs/libb-runtime.md`](docs/libb-runtime.md) — freestanding runtime, `_start`, syscalls, and standard functions.
- [`docs/known-limitations.md`](docs/known-limitations.md) — current limitations and likely improvement points.

The source itself also contains explanatory comments. They are meant for readers already comfortable with C and Unix, so the comments focus on BCause-specific control flow, layout, ownership, ABI boundaries, and historical B behavior rather than basic C syntax.

### Current Status

- [x] global variables
- [x] functions
- [x] `auto` & `extrn` variables
- [x] control flow statements
- [x] expressions
- [x] `libb.a` standard library
- [ ] optimization
- [ ] nicer error messages

### Compatibility

Due to BCause's simplicity, only **`gnu-linux-x86_64`**-systems are supported.

- If your system can run *GNU-`make`*, *GNU-`ld`* and *GNU-`as`*, BCause itself should be able to work.
- Because of the reliance on system-calls `libb.a` has to be implemented for each system separately.

> **Note**
> Feel free to submit pull requests to provide more OS support and fix bugs.

### Installation

To install BCause, first clone this repository:
```console
$ git clone https://github.com/spydr06/bcause.git
$ cd ./bcause
```
Then, build the project:
```console
$ make
```
To install BCause on your computer globally, use:
```console
# make install
```
> **Warning**
> this requires root privileges and modifies system files

### Usage

To compile a B source file (`.b`), use:
```console
$ bcause <your file>
```

To get help, type:
```console
$ bcause --help
```

### Testing

The `tests/` directory contains numerous compiler tests. Please update these tests when adding new features. Tests are based on googletest and require `cmake` to be run:

```console
$ make test
```

### Licensing
BCause is licensed under the MIT License. See `LICENSE` in this repository for further information.

### References

- [Bell Labs User's Reference to B](https://web.archive.org/web/20241217103914/https://www.bell-labs.com/usr/dmr/www/kbman.pdf) by Ken Thompson (Jan. 7, 1972)

- Wikipedia entry: [B (programming language)](https://en.wikipedia.org/wiki/B_(programming_language))
