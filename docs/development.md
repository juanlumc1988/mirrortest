# Development notes

How to build, test and extend mirrortest. For *what* the tool does and *why*,
see [design.md](design.md); for the order of work, [plan.md](plan.md).

## Where the decisions are

- [`design.md`](design.md) is authoritative. Every inference rule, file layout,
  diagnostic code and CLI option is specified there. Do not re-decide what it
  already decides; if the implementation forces a change, change the document
  in the same commit and say why.
- [`plan.md`](plan.md) is the ordered task list with acceptance criteria. Work
  the next unchecked task and tick it in the same commit.
- [`technical-analysis.md`](technical-analysis.md) is the original analysis,
  kept for traceability. Where it disagrees with `design.md`, `design.md` wins
  (section 1 lists why).

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMT_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Both GCC and Clang must be clean (`-DCMAKE_C_COMPILER=clang
-DCMAKE_CXX_COMPILER=clang++` for the second). Sanitizers:

```sh
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMT_WERROR=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
```

A second build tree can reuse the first one's downloads instead of fetching
again: pass `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<first>/_deps/<name>-src` for
`TREE_SITTER`, `TREE_SITTER_CPP`, `NLOHMANN_JSON`, `INJA`, `CLI11` and
`GOOGLETEST` (lower-case name in the path).

Clang's sanitizer runtime may be missing on minimal images
(`libclang-rt-18-dev` on Ubuntu 24.04).

Golden files are updated only through the mechanism (`MT_UPDATE_GOLDEN=1`, or
the `update-golden` target), never by hand, and the resulting diff is reviewed
before committing.

### The layers of tests

| CTest name | What it checks |
|---|---|
| `<Suite>.<Test>` | unit tests of every module (`tests/unit/`), C++ fragments as strings |
| `golden.*` | the real command line against the files in `tests/golden/` (output, stderr, exit code, determinism) |
| `roundtrip.<case>` | generates from the test of a golden case, compiles the stubs as strict C++11 and the test as C++14, links, runs, requires every test red, and requires a second run to change nothing (`tests/roundtrip/run_case.cmake`) |
| `roundtrip.gen_tests.<case>` | the same for gen-tests: generates tests from the header of a golden case, compiles an example implementation (C++11, strict) and the tests (C++14, `-Werror`), runs them and requires every one to fail, or to be skipped with `--placeholder skip` (`tests/roundtrip/run_gen_tests_case.cmake`, the implementations in `tests/roundtrip/gen-tests/`) |
| `roundtrip.qt.gen_tests.<case>`, `roundtrip.qt.gen_impl.<case>` | the same for Qt Test, both directions, when Qt 6 (else Qt 5) with its Test module is installed (otherwise one test, `roundtrip.qt`, is reported as skipped). moc is run by the script; a `gen-impl` slot is run alone and must fail, because after the first exception QTest ends the program (`tests/roundtrip/run_qt_case.cmake`, the implementations in `tests/roundtrip/qt/`) |
| `roundtrip.merge` | the TDD session: generate, implement a method, grow the test, `--overwrite merge`; the work survives and passes, the new stubs are red (`tests/roundtrip/run_merge.cmake`) |
| `stubs.host.<case>` | the stubs of *every* gen-impl golden compile with `-fsyntax-only` as strict C++11 (`tests/roundtrip/syntax_case.cmake`) |
| `stubs.arm.<case>` | the same with a cross compiler, if `-DMT_ARM_CXX=arm-linux-gnueabihf-g++` is given (CI does) |

A new gen-impl golden case is a new `stubs.host.*` test the next time CMake
configures; add it to the `roundtrip.*` list in `CMakeLists.txt` when it should be
compiled, linked and run. A new gen-tests golden case is a new
`roundtrip.gen_tests.*` test as soon as `tests/roundtrip/gen-tests/<case>.cpp`
exists (CMake finds it when it configures).

## Conventions

- Every source, CMake and workflow file starts with
  `SPDX-License-Identifier: Apache-2.0` and the
  `Part of mirrortest -- bidirectional C++ test/code generator.` line.
- C++17. `namespace mt::<module>`; header guards `MT_<PATH>_H`; `snake_case`
  for functions and members, `CamelCase` for types; 4-space indent.
- Documentation, comments and everything the tool generates are in English.
  Comments explain *why*, in the style already in `src/` and `include/`.
- Text the tool generates (comments, `TODO(mt)`, stub messages, diagnostics) is
  English.
- Determinism: no `unordered_*` on any path that affects output, no absolute
  paths or timestamps in output, LF line endings.
- Problems in the user's *input* are `Diagnostics` with an `mt-*` code, never
  exceptions. Exceptions are for internal failures and I/O.
- Third-party versions are pinned by tag and commit in
  `cmake/MtDependencies.cmake`; bump both together, deliberately, and rerun the
  golden and round-trip suites.
- Tests: GoogleTest, every module, the happy path and the edge cases the plan
  lists. A generator is only as trustworthy as its golden files.
