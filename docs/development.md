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

Optional parts of the build: `-DMT_WITH_YAML=OFF` leaves out yaml-cpp (a `.yaml` configuration is then
refused with a clear error; CI builds that way too), and `-DMT_WITH_LIBCLANG=ON` adds the libclang
frontend (plan 8.5). The golden cases and unit tests that need a part skip themselves when it is not there.

A second build tree can reuse the first one's downloads instead of fetching
again: pass `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<first>/_deps/<name>-src` for
`TREE_SITTER`, `TREE_SITTER_CPP`, `NLOHMANN_JSON`, `INJA`, `CLI11`, `YAML_CPP` and
`GOOGLETEST` (lower-case name in the path).

The libclang frontend is tested by CI against libclang 14 to 22, one job each (the versions of Ubuntu 24.04 and 26.04). To try one version by hand, install it
(`apt install libclang-17-dev` puts it in `/usr/lib/llvm-17`) and say which, since the CMake module takes the newest
it finds otherwise:

```sh
cmake -S . -B build-llvm17 -G Ninja -DMT_WITH_LIBCLANG=ON \
  -DMT_LIBCLANG_INCLUDE_DIR=/usr/lib/llvm-17/include -DMT_LIBCLANG_LIBRARY=/usr/lib/llvm-17/lib/libclang-17.so
```

`frontend::libclang_major_version()` tells a test which version it runs with, for the few that depend on it.

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
| `roundtrip.qt.gen_tests.<case>`, `roundtrip.qt.gen_impl.<case>` | the same for Qt Test, both directions, when Qt 6 (else Qt 5) with its Test module is installed (otherwise one test, `roundtrip.qt`, is reported as skipped). moc is run by the script; a `gen-impl` slot is run alone and must fail, because after the first exception QTest ends the program (the case `qt_hook` links a handler that fails the slot and returns, `tests/roundtrip/hook/`, and so runs all of them in one program) (`tests/roundtrip/run_qt_case.cmake`, the implementations in `tests/roundtrip/qt/`) |
| `roundtrip.merge` | the TDD session: generate, implement a method, grow the test, `--overwrite merge`; the work survives and passes, the new stubs are red (`tests/roundtrip/run_merge.cmake`) |
| `stubs.host.<case>` | the stubs of *every* gen-impl golden compile with `-fsyntax-only` as strict C++11 (`tests/roundtrip/syntax_case.cmake`) |
| `stubs.cross.<case>` | the same with a cross compiler for a 32-bit target, if `-DMT_CROSS_CXX=arm-linux-gnueabihf-g++` is given (CI does) |

A new gen-impl golden case is a new `stubs.host.*` test the next time CMake
configures; add it to the `roundtrip.*` list in `CMakeLists.txt` when it should be
compiled, linked and run. A new gen-tests golden case is a new
`roundtrip.gen_tests.*` test as soon as `tests/roundtrip/gen-tests/<case>.cpp`
exists (CMake finds it when it configures).

## Running it on code that was not written for it

Unit tests and goldens only say that the tool reads what its authors thought of. `tools/corpus.sh` runs it
on other people's headers (and, for `gen-impl`, on other people's tests) and reports what a unit test would
not: a crash, a hang, a sanitizer report, the `error` messages, and the headers the two frontends read
differently. It is a development tool: the corpus is not vendored, and nothing here is part of the build
or of CI.

```sh
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMT_BUILD_TOOLS=ON -DMT_WITH_LIBCLANG=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-san --target mirrortest mt_dump_model

tools/corpus.sh --bin build-san/mirrortest --dump build-san/mt_dump_model --out /tmp/corpus \
    -I build/_deps/cli11-src/include --config cli11.json build/_deps/cli11-src/include
tools/corpus.sh --tests --bin build-san/mirrortest --out /tmp/corpus-tests \
    build/_deps/googletest-src/googletest/test        # gen-impl on a project's own tests
```

`/tmp/corpus/report.txt` is the answer. Its sections "crashes, hangs, usage errors, sanitizer reports" (the
function-type check of tree-sitter's generated C is left out of it) and "models that could not be made" must be
empty; the slowest runs are for seeing a hang coming. The messages of the errors are for reading: some are the
tool saying no on purpose (`mt-unknown-return`, `mt-unknown-param`, `mt-chained-call` when a test says nothing
about a type). The last section lists the headers whose models differ, and each difference is one of these:

- **A branch of an `#if`.** The syntax-tree reader keeps every branch (design 6.5), libclang only the one
  that builds, so an `#include`, a member, a class of a platform or a block under `#if 0` is in one model and not
  in the other. Expected.
- **A macro of the project.** A syntax tree cannot see through `MYLIB_API` or `DECLARE_THING(x)` and needs
  it in `ignore_macros` (`--config`); a compiler needs nothing. A macro that opens or closes a namespace
  (`NLOHMANN_JSON_NAMESPACE_BEGIN`) cannot be helped that way: listed, the declarations are read but without the
  namespace. Without the right list, a standard library's headers differ almost all (`_GLIBCXX_*`). The
  warning says so, and when the parser loses most of a file because of one, it says that too. A way to find
  the macros is to read the places of the `mt-parse` warnings: the same few names come out.
- **Something only a compiler knows.** What a macro defined in another header declares (`MATCHER(IsTrue, "")`),
  a macro that is a type (`GTEST_FLAG_SAVER_`, which libclang spells as what it expands to), and the world outside
  the header: libclang says "libclang: …" for what it cannot find, and a header that is not self-contained
  (nlohmann's `detail/` headers use what the file that includes them brought) loses what depends on it. Those
  warnings are in the `.cl.model.err` of the header.
- **Text the grammar cannot read** (an SFINAE `decltype(a, b)` return type with template-ids in it,
  `typename T::type* = nullptr`, a pointer to member as a field, a struct that a `typedef` gives its name to,
  design 6.6): the syntax-tree reader says "the parser could not make sense of this text (a macro of the project?
  list it in ignore_macros)" for most of them, and the model around it is partial; libclang is right.
- **Cosmetic.** A parameter `void* const` without a name is spelled `void*` by libclang.
- **Anything else is a bug** of one of the two readers: reduce the header to the smallest one that shows it
  (`mt_dump_model --frontend … FILE` writes the model, `--tree` the syntax tree), fix it and add the
  reduced header to `tests/unit/test_clang_frontend.cpp` (the parity battery, if both readers can read
  it) or to the test file of the reader at fault.

The script exits with a non-zero status when one of those two sections is not empty, and with `-DMT_BUILD_TOOLS=ON` and
`-DMT_WITH_LIBCLANG=ON` the test `tools.corpus` runs it over the header of a golden case, which keeps the script itself working.

`mt_dump_model` writes the model in the form the parity tests compare, so the same two commands that
`corpus.sh` runs can be run by hand on one file: `mt_dump_model --frontend tree-sitter F` and
`mt_dump_model --frontend libclang -I dir F`, and `diff` the two.

### What the first run found (plan 9.1)

The headers of CLI11, GoogleTest and GoogleMock, inja, nlohmann/json, yaml-cpp and tree-sitter at the versions
pinned in `cmake/MtDependencies.cmake` (the project's macros in `ignore_macros` where there are any), 36 headers of
GCC 13's libstdc++, and `gen-impl` over the 75 test files of GoogleTest and GoogleMock: 200 headers with both
readers, 75 test files, built with Clang and the address and undefined-behaviour sanitizers. **No crash, no hang
(the slowest run took 1.5 s), no sanitizer report and no `error`** in the headers; the errors of the test files are
the tool asking for a type.

| | headers | same model | differ |
|---|---|---|---|
| CLI11 | 26 | 18 | 8 |
| GoogleTest, GoogleMock | 38 | 21 | 17 |
| inja | 15 | 14 | 1 |
| nlohmann/json | 46 | 24 | 22 |
| yaml-cpp | 38 | 37 | 1 |
| tree-sitter | 1 | 0 | 1 |
| libstdc++ (a sample) | 36 | 3 | 33 |

Every difference of CLI11, GoogleTest and GoogleMock, inja, yaml-cpp and tree-sitter was read in the two models and
is one of the kinds above; nlohmann's and libstdc++'s were classified by what either reader said about the header
(the macros behind the warnings, the "libclang: …" diagnostics) and by reading a sample. What the run turned into
fixes, each with a unit test made from the smallest header that showed it:

- *libclang*: the extent of a declaration with a trailing `decltype`, of a member that starts with an export
  macro, of one declared by a macro, and of a function template (no parameter count); comments and `#` lines
  inside a declaration; several declarators sharing a type; defaults written by a macro; conversion function
  templates; function-pointer returns; `requires` and dynamic exception specifications; and a header that is
  included back from the end of the file it includes (CLI11's `_inl.hpp` files), which was read twice.
- *tree-sitter*: a destructor `= 0`, a braced default, a declarator that is no name, a class defined outside its
  class (skipped by both readers in the same words), a field `override` that was a macro, a function with a
  pointer-to-member parameter (skipped with a warning), parse errors inside a preprocessor directive (not
  reported: they hide nothing), and the warning for a parse error that takes most of the file with it (a project
  macro such as `CMP_HELPER(LT, <)` at namespace scope had made GoogleTest's `gtest.h` an empty model behind a
  warning that read as a small thing).

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
