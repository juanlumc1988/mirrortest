# mirrortest

[![CI](https://github.com/juanlumc1988/mirrortest/actions/workflows/ci.yml/badge.svg)](https://github.com/juanlumc1988/mirrortest/actions/workflows/ci.yml)

Bidirectional C++ test/code generator: derives class and function stubs from
GoogleTest/QTest tests (TDD), and generates test skeletons from existing
classes. The command it builds is `mirrortest`.

**Status: `gen-impl` works for GoogleTest; `gen-tests` and QTest are next.**
`gen-impl` is complete end to end — parsing, inference, headers and stubs,
`.new` files, merging into earlier output, `--dry-run`/`--check` — and verified
by unit tests, golden files, compile-and-run round trips and a syntax check of
every generated stub as strict C++11. `gen-tests` and QTest are designed and not
written yet; see [docs/plan.md](docs/plan.md) for what is done and what comes next. The
`gen-tests` lines under [Usage](#usage) describe the target behaviour.

## What it does

| Command | Input | Output |
|---|---|---|
| `gen-impl` | GoogleTest / QTest test files | Headers and stubs that make the tests **compile and fail** — the real "red" step of TDD |
| `gen-tests` | Existing headers | Test skeletons that compile against the real API and fail (or skip) until someone writes them |

It generates structure — classes, signatures, includes, test scaffolding —
and never business logic. The output is deterministic, compilable and meant
to be reviewed: every guess is marked `TODO(mt)` with the reason and the
source line it came from.

## Principles

- **Never pretend.** A generated stub never returns a value that could make a
  test pass by accident; it throws (or aborts, without exceptions). A
  generated test never contains an invented assertion; it fails until written.
- **Never destroy.** Existing files are not overwritten by default: a differing
  file gets a `.new` sibling. The merge mode only ever inserts, and only inside
  regions the tool generated and marked.
- **Fail loudly.** What cannot be inferred (an `auto` holding a result of the
  API under test, a chained call on an unknown type) is an error with
  `file:line`, not a silent guess. Every diagnostic has a code.
- **Same output, every time.** No timestamps, no absolute paths, stable order.

## Usage

```console
# TDD: tests first, then the code that makes them compile -- and fail.
$ mirrortest gen-impl tests/foo_test.cpp --out src/
created src/acme/foo.h
created src/acme/foo.cpp

# Legacy code: test skeletons for an existing class.
$ mirrortest gen-tests include/acme/foo.h -I include --out tests/ --framework qtest
created tests/tst_foo.cpp
```

From a test like this:

```cpp
#include <gtest/gtest.h>
#include "acme/foo.h"

TEST(FooTest, StartsWithThreeItems) {
    const acme::Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
    EXPECT_FALSE(foo.empty());
}
```

`gen-impl` writes a header declaring `explicit Foo(int arg1)`,
`unsigned int size() const` and `bool empty() const` — `const` because the
test calls them on a `const` object, `unsigned int` because of the `3u` — and
a source file whose stubs throw `std::logic_error`. The test now compiles and
fails, which is exactly where TDD wants you.

The conventions that make tests readable by the tool — no `auto` on results of
the API under test, literal suffixes where the type matters, optional
`// @mt:` annotations — are in [docs/conventions.md](docs/conventions.md).

### Options that matter first

| Option | Effect |
|---|---|
| `--out DIR` | where to write; or `--out-include DIR` and `--out-src DIR` separately |
| `-I DIR` | a directory with headers that already exist: they are your API, not something to generate |
| `--stub-policy throw\|abort\|default` | what the stubs do (default `throw`); `--no-exceptions` makes `throw` behave as `abort` |
| `--auto-policy error\|todo` | what to do when a type cannot be inferred (default: an error, nothing written) |
| `--std c++11\|...\|c++23` | the standard the generated code must be valid in (default `c++17`) |
| `--overwrite never\|merge\|force` | a differing file gets a `.new` beside it (default), gets the missing members inserted into its regions, or is replaced |
| `--dry-run`, `--check` | show diffs without writing; exit 4 if anything would change |
| `--config FILE.json`, `--templates DIR` | house style: [docs/conventions.md](docs/conventions.md#configuration), [docs/templates.md](docs/templates.md) |
| `--strict`, `--quiet`, `--verbose` | warnings as errors; fewer lines; more lines |

Exit codes: `0` success, `1` the input has errors (nothing is written), `2` usage,
`3` I/O, `4` `--check` found differences. Diagnostics use the GCC format
(`file:line:col: warning: message [mt-code]`), so editors and CI pick them up.

## Building

Requirements: CMake ≥ 3.20, a C++17 and C11 compiler (GCC or Clang), git.

```console
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
$ ctest --test-dir build --output-on-failure
```

Dependencies are fetched at configure time and pinned twice — by tag, and by
the commit the tag pointed to when it was reviewed. A tag that moves upstream
fails the configure instead of silently changing the build.

**Offline / corporate networks.** Point each dependency at a local copy:

```console
$ cmake -S . -B build \
    -DFETCHCONTENT_SOURCE_DIR_TREE_SITTER=/mirror/tree-sitter \
    -DFETCHCONTENT_SOURCE_DIR_TREE_SITTER_CPP=/mirror/tree-sitter-cpp \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/mirror/json \
    -DFETCHCONTENT_SOURCE_DIR_INJA=/mirror/inja \
    -DFETCHCONTENT_SOURCE_DIR_CLI11=/mirror/CLI11 \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/mirror/googletest
```

Git checkouts given this way are still verified against the pinned commits.

The generator runs on the development host. The code it *generates* targets
whatever the project targets — ARM included — and defaults to C++11-valid
output with no RTTI and, on request, no exceptions.

## Documentation

- [docs/design.md](docs/design.md) — the technical design: every decision,
  every inference rule, every diagnostic code. Authoritative.
- [docs/plan.md](docs/plan.md) — implementation plan and progress.
- [docs/conventions.md](docs/conventions.md) — how to write tests the tool
  understands, annotations, stub policies, configuration, every diagnostic.
- [docs/development.md](docs/development.md) — building, testing, sanitizers
  and the code conventions.
- [docs/templates.md](docs/templates.md) — the output templates and their
  variables.
- [docs/technical-analysis.md](docs/technical-analysis.md) — the original
  analysis the design was reviewed against.

## Licence

Apache-2.0; see [LICENSE](LICENSE). Code generated by mirrortest belongs to
whoever runs it and carries no licence header from this project.

Third-party components, fetched at build time and not redistributed here:

| Component | Licence |
|---|---|
| [tree-sitter](https://github.com/tree-sitter/tree-sitter), [tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp) | MIT |
| [nlohmann/json](https://github.com/nlohmann/json), [inja](https://github.com/pantor/inja) | MIT |
| [CLI11](https://github.com/CLIUtils/CLI11), [GoogleTest](https://github.com/google/googletest) | BSD-3-Clause |
