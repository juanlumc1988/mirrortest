# Technical analysis: bidirectional tests ↔ C++ code generator

> English translation of the original analysis, which was written in Spanish.
> The content is unchanged; it is kept for traceability. Where it disagrees
> with [design.md](design.md), `design.md` wins (its section 1 lists why).

## 1. Scope

CLI tool with two modes on top of a common core:

| Mode | Input | Output |
|---|---|---|
| `gen-impl` (TDD) | GoogleTest / QTest tests (`.cpp`) | Headers and *stubs* of classes/functions that make the tests compile ("red" phase) |
| `gen-tests` | Existing headers/classes (`.h`/`.hpp`) | GoogleTest / QTest test skeletons |

Goal: generate code that is **deterministic, compilable and reviewable**. It does not try to infer business logic, only structure (classes, signatures, includes, test skeleton).

## 2. Architecture

```
          +-----------+        +-----------+
 tests -> | Tests     |        | Code      | <- headers
          | frontend  |        | frontend  |
          +-----+-----+        +-----+-----+
                |                    |
                v                    v
          +-----------------------------------+
          |  Neutral intermediate model (IR)  |
          |  Class, Method, Param, Type, Test |
          +-----------------+-----------------+
                |                    |
                v                    v
          +-----------+        +-----------+
          | Code      |        | Tests     |
          | backend   |        | backend   |
          | (emitter) |        | GTest/QTest|
          +-----------+        +-----------+
```

Principles:
- **IR independent of framework and parser.** Frontends and backends only talk to the IR. This allows adding Catch2/doctest, or changing the parser, without touching the rest.
- **Frontends separated by direction**, because the parser requirements differ (see §3).
- **Emission through templates** (inja or mustache), not by concatenating strings in C++. Style changes do not require recompiling.

### Minimal IR

```cpp
struct Type   { std::string spelling; bool isConst, isRef, isPtr; /* + origin: Inferred | Declared */ };
struct Param  { Type type; std::string name; };
struct Method { std::string name; Type ret; std::vector<Param> params;
                bool isConst, isStatic, isVirtual, isCtor, isDtor; Access access; };
struct Class  { std::string name; std::vector<std::string> ns, bases;
                std::vector<Method> methods; std::vector<Field> fields; };
struct TestCase { std::string suite, name; std::vector<Stmt> body; Fixture* fixture; };
```

Important: `Type` must record whether it is **declared** (explicit in the source code) or **inferred** (deduced from a literal or from the context). This feeds the backend's warnings/TODOs.

## 3. Parser choice (key decision)

| Option | Pros | Cons |
|---|---|---|
| **libclang / LibTooling** | Complete semantic AST, resolved types, templates, expanded macros | Heavy dependency; needs a correct *compile_commands.json* and *include paths*; on code that does not compile (tests without an implementation) it reports errors and a recovery AST that is not reliable |
| **tree-sitter-cpp** | Syntactic only, error tolerant, fast, no toolchain | No semantics: does not resolve types or overloads |

Recommendation (own judgement, to be validated with a prototype):
- **tests → code**: tree-sitter. The input code **does not compile by definition** (it references classes that do not exist), so a semantic parser breaks exactly on the use case. What gets extracted syntactically: the `TEST/TEST_F/TEST_P` macros, `QCOMPARE/QVERIFY`, variable declarations, method calls, `#include`.
- **code → tests**: libclang. Here the code does compile and real types, constness, visibility, inheritance and templates are needed.

This implies two frontends, but both produce the same IR. A simpler alternative for an MVP: tree-sitter in both directions, assuming "clean" headers (without heavy metaprogramming).

## 4. Tests → code direction (the hard one)

A test does not by itself determine return types, constructors or visibility. A **subset of conventions** has to be fixed and documented.

### What can be extracted reliably
- Class name: `Foo foo(args);` / `Foo::bar()` / `new Foo(...)`.
- Methods used: `foo.bar(a, b)` → name and arity.
- Constness: if the object is `const Foo&` and the method is called on it.
- Return type from the assertion context: `EXPECT_EQ(foo.size(), 3u)` → `unsigned`, `EXPECT_TRUE(foo.empty())` → `bool`, `EXPECT_STREQ(foo.name(), "x")` → `const char*`.
- Parameter types from literals (`1` → `int`, `1.0` → `double`, `"a"` → `const char*`) or from variables declared with an explicit type.
- Expected includes: from the test's `#include "foo.h"`.
- Fixtures: `TEST_F(Fixture, ...)` → class `Fixture : ::testing::Test` and the members used.

### What can NOT be inferred (needs a convention or an annotation)
- Types declared with `auto`. Policy: an error with a clear message, or a `TODO` in the stub.
- Return type when the result is not compared (`foo.run();`) → `void` by default, with a warning.
- Visibility, `virtual`, inheritance, copy/move constructors.
- Namespaces (only if the test uses `ns::Foo` or `using`).

### Proposed conventions
1. No `auto` on variables that exercise the API under test.
2. One class under test per *suite* (suite name = class name, or `Foo` in `FooTest`).
3. Literals are written with a suffix when it matters (`3u`, `1.0f`).
4. Optional comment annotations for what is ambiguous, e.g. `// @gen: returns std::vector<int>`.

### Emitter behaviour
- Generates `foo.h` and `foo.cpp` with **stubs that compile and make the test fail** (default return value, or `throw std::logic_error("not implemented")`). This guarantees a real "red" phase.
- **Idempotent and non-destructive**: if the file exists it is not overwritten; only new members are merged (markers `// <gen:begin> ... // <gen:end>`) or the output goes to a `.new` file with a warning. This is the most important requirement for the tool to be usable in a real TDD flow.
- Every inferred type or decision carries a `// TODO(inferred): ...` comment.

## 5. Code → tests direction

### What is generated
- One test file per class; one case per public method (plus constructors).
- Base cases: construction, method with default/boundary values of the type (0, -1, `max`, empty), comparison of the return value with a `TODO`.
- For constness and `noexcept`: tests that check that the calls compile on a `const` object.
- Classes with pure virtual dependencies: optionally generate **gmock mocks** (`MOCK_METHOD`) for interfaces.
- Templates: one case per instantiation listed in a configuration file, not inferred.

### Design principle
Generated tests are **skeletons with placeholder assertions** (`FAIL() << "TODO"` or `GTEST_SKIP()`), never invented assertions that pass by chance. A generated test that passes without anyone having reviewed it is worse than none.

## 6. Framework backends

### GoogleTest
- `TEST`, `TEST_F` (fixture), `TEST_P` + `INSTANTIATE_TEST_SUITE_P` (parameterized).
- Assertions: `EXPECT_*`/`ASSERT_*`; gmock for mocks.
- Process entry point: `gtest_main`, no extra code.

### QTest
Differences to keep in mind (the easiest ones to get wrong):
- A test is a **`QObject` class** with `Q_OBJECT` and the cases as **`private slots`**; it requires **moc** (AUTOMOC in CMake).
- Life cycle: `initTestCase()`, `init()`, `cleanup()`, `cleanupTestCase()`.
- Data-driven: for `void foo()` there is `void foo_data()` with `QTest::addColumn` / `QTest::newRow`.
- Assertions: `QVERIFY`, `QCOMPARE` (types must match exactly; beware of `int` vs `unsigned`), `QVERIFY2`; there is no `QTEST_THROW`-like macro: use `QVERIFY_EXCEPTION_THROWN`.
- Entry point: `QTEST_MAIN`, `QTEST_APPLESS_MAIN` (without `QApplication`) or `QTEST_GUILESS_MAIN`; if the class is in the `.cpp`, the `.moc` file must be included at the end.

The GTest/QTest asymmetry (macros vs. classes + slots + moc) is the reason for the IR to model `TestCase`/`Fixture` abstractly and for each backend to decide the concrete form.

## 7. Standard and dependencies

The generator runs on the host (not on the ARM target), so it has no memory/size limitations; what does matter is that the **generated output** is compatible with the standard of the target project (flag `--std=c++11|14|17`).

- Generator: **C++17**. `std::filesystem`, `std::optional`, `std::string_view`, structured bindings. A stable, well-known compiler in any environment.
- Minimal dependencies pinned by version: parser (tree-sitter and/or libclang), template engine (inja), CLI (CLI11), JSON/YAML for configuration (nlohmann/json or yaml-cpp), tests (GoogleTest).
- Management: CMake + `FetchContent` with pinned *tags* (or vcpkg/Conan with a lockfile) for reproducible builds.

Note: the code generated for the ARM target must respect the project's policy (exceptions, RTTI, STL). Make it configurable: `--no-exceptions` (stubs without `throw`), `--no-rtti`, and avoid the STL in stubs if the project does not use it.

## 8. Testing strategy for the generator itself

It is a text generator; the most reliable technique is **golden files**:

1. **Unit tests of the IR and of the extractors** (input: a C++ fragment as a string; output: the expected IR).
2. **Emission golden tests**: IR → expected file versioned in `tests/golden/`. A diff fails the test; updating goldens is explicit (`--update-golden`).
3. **Round-trip tests** (the most valuable):
   - tests → `gen-impl` → compile (must compile, tests red).
   - code → `gen-tests` → compile (must compile, tests in controlled `SKIP`/`FAIL`).
   - Run in CI with GCC and Clang.
4. **Corpus of edge cases**: templates, nested namespaces, `const`/`static`/`noexcept`, overloads, inheritance, operators, `enum class`, smart pointers.
5. **Sanitizers** (ASan/UBSan) in CI on the generator.

## 9. Proposed repository structure

```
.
├── CMakeLists.txt
├── cmake/
├── include/ <project>/
│   ├── ir/            # Class, Method, Type, TestCase
│   ├── frontend/      # tests_frontend, code_frontend
│   ├── backend/       # gtest_backend, qtest_backend, impl_backend
│   └── core/          # config, diagnostics, file merge
├── src/
├── templates/         # inja: gtest/, qtest/, impl/
├── apps/cli/          # main: gen-impl, gen-tests
├── tests/
│   ├── unit/
│   ├── golden/
│   └── roundtrip/     # example CMake projects that get compiled
├── examples/
└── docs/              # supported conventions (§4)
```

## 10. Configuration and CLI

```
tool gen-impl  --tests tests/foo_test.cpp --out src/ --framework gtest --std c++14
tool gen-tests --header include/foo.h     --out tests/ --framework qtest
```

Common options: `--dry-run` (prints a diff), `--overwrite=never|merge|force`, `--config tool.yaml`, `-I` / `--compile-commands`.
Configuration (`tool.yaml`): file name template, default namespace, include style, template instantiations, type map (`unsigned` → `uint32_t`), exception policy.

## 11. Risks

| Risk | Mitigation |
|---|---|
| Wrong type inference | Mark the origin as inferred, `TODO`, fail loudly on `auto` |
| Overwriting user code | Non-destructive default mode + generated-region markers |
| Semantic parser on code that does not compile | tree-sitter for tests → code |
| Drift of QTest/GTest macros between versions | Pin versions; golden tests per version |
| Unbounded scope (templates, metaprogramming) | Declare a supported subset; an explicit error for the rest |
| Generated tests that pass without value | Placeholder assertions that fail or skip |

## 12. Roadmap

1. **gen-impl MVP, GoogleTest**: simple classes, methods with types inferred from literals and assertions, stubs, non-destructive mode, golden + round-trip.
2. **gen-tests MVP, GoogleTest**: non-template classes, one test per public method, basic fixture.
3. **QTest backend** in both directions (slots, moc, `_data`).
4. Fixtures and parameterized tests; gmock mocks from interfaces.
5. Templates, inheritance and nested namespaces.
6. Smart merge of existing files and advanced configuration.

## 13. Open decisions (to be settled with a spike)

- tree-sitter in both directions, or libclang only for code → tests? Prototype both with 5–10 real cases from your project and compare the errors.
- Merge granularity: by marked region or by AST?
- Format of the `// @gen:` annotations and whether the configuration file is also allowed to carry them.
- Policy for `auto` and for types that cannot be inferred: error, or `TODO` + continue?
