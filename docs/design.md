# mirrortest — technical design

Reference document for the implementation. It starts from
[`technical-analysis.md`](technical-analysis.md) (cited as "AT §n"), reviews it,
corrects what does not hold up and closes the decisions the analysis left open.
If this document and the analysis disagree, **this one wins**. The order of work
is in [`plan.md`](plan.md).

The decisions marked **[confirmed]** (R8, R15, R18, D1, D2, D6) were validated
by the project owner when the design was closed.

---

## 1. Review of the analysis (double-check)

Findings of the review. Those marked *verified* were checked against real code
in this repository (parser spike, Qt 5.15.13 and 6.4.2 headers, GoogleTest
README per version).

| # | AT | What it says | Problem | Resolution |
|---|---|---|---|---|
| R1 | §4 | Fixtures: `TEST_F(Fixture, …)` → generate `class Fixture : ::testing::Test` | In gen-impl the fixture is **input**: `TEST_F` requires it to be defined earlier in the test itself. It is not generated; it is read in order to type its members. | The tests frontend parses the fixtures of the file (§7.4). Only gen-tests generates fixtures. |
| R2 | §4 | Stub = "default return value, or `throw`" | A default value can make the test **pass** (`EXPECT_EQ(v.size(), 0u)`, `EXPECT_FALSE(v.empty())`): it breaks the *red* phase and contradicts AT §5. | Default policy `throw`; without exceptions `abort`; `default` only as an opt-in and with the `mt-stub-may-pass` warning (§8.3). |
| R3 | §4 | — | A destructor that throws calls `std::terminate` (implicitly `noexcept`); a constructor that throws hides which methods fail. | Constructors and destructors with an empty body; only the methods fail. |
| R4 | §4 | `"a"` → `const char*` | In `EXPECT_EQ(foo.name(), "x")` a `const char*` return compares **pointers** (GoogleTest documents this). It compiles and gives a meaningless result. | Context rules: in `EXPECT_EQ/NE` a string literal gives `std::string`; in `EXPECT_STR*` it gives `const char*`; in `QCOMPARE` it gives `QString` (§7.6). |
| R5 | §6 | `QCOMPARE` requires identical types | *Verified*: Qt 5.15 and Qt 6.4 have `template<T1,T2> qCompare(const T1&, const T2&)` via `==`. The requirement belonged to old versions. | The exact type of the literal is still inferred (avoids `-Wsign-compare` and keeps parity with GTest), but it is no longer a compilation requirement. |
| R6 | §6 | Exceptions: use `QVERIFY_EXCEPTION_THROWN` | *Verified*: deprecated since Qt 6.3 in favour of `QVERIFY_THROWS_EXCEPTION(Type, expr)` — **reversed argument order** — which does not exist in Qt 5. | The frontend recognises both; the backend chooses according to `--qt 5\|6` (§9.6). |
| R7 | §7 | Output compatible with `--std=c++11` | *Verified*: GoogleTest 1.12.x is the last one supporting C++11; 1.13–1.16 require C++14; 1.17 requires C++17. Tests generated for C++11 only compile with GTest ≤ 1.12. | Documented. GTest 1.16.0 is pinned so that C++14 output can be tested. The stub code (no GTest) is tested in C++11. |
| R8 | §3 | Decide the parser with a spike | *Verified* (spike): tree-sitter-cpp 0.23.4 parses GTest without errors. QTest and Qt headers do **not**: `Q_OBJECT`, `slots`, `signals`, `Q_PROPERTY(…)` and export macros generate `ERROR` nodes and declarations get lost (11 errors in a typical header). With those macros replaced by spaces: 0 errors. | tree-sitter in both directions + a *blanking* preprocessing step that preserves offsets (§5.2). libclang stays as a possible future optional frontend. **[confirmed]** |
| R9 | §11 | — | tree-sitter does not preprocess: it sees **all** branches of `#if/#else`. Code with per-platform `#ifdef`s can produce duplicate or contradictory declarations. | All branches are visited; exact duplicates are merged; different variants are kept with an `mt-conditional` note (§6.5). |
| R10 | §4 | Types of integer literals | The type of a large integer depends on the data model: `long` is 32 bits on 32-bit targets (ILP32) and on 64-bit Windows (LLP64) and 64 on 64-bit Linux (LP64). The generator runs on the host but the code may be built for another data model. | `long` is never deduced from magnitude, only from a suffix. Without a suffix and outside `int` → `long long` + warning. Suffix `l` with a value above 32 bits → a warning that a 32-bit `long` cannot hold it (§7.5). |
| R11 | §4 | — | *Verified*: tree-sitter lexes `-1` as a single `number_literal`, not as a unary minus. | The literal classifier accepts a sign. |
| R12 | §4 | `Foo foo(args);` | `Foo f();` is the *most vexing parse*: it declares a function. *Verified*: tree-sitter gives it as a `function_declarator`. | `mt-vexing-parse` warning; no constructor is inferred. |
| R13 | §4 | `Foo::bar()` | *Verified*: `Foo::create()` and `ns::create()` are syntactically identical (`namespace_identifier`). | Explicit scope-resolution rule (§7.7). |
| R14 | §2 | IR with `Fixture*` and `Type{isConst,isRef,isPtr}` | A raw pointer in a value model; the booleans cannot represent `const char* const&`, pointer-to-const vs const pointer, `&&`. `noexcept`, `= 0`, `override`, `= delete/default`, `explicit`, ref-qualifiers, Qt roles and provenance are missing. | IR in `include/mirrortest/ir/model.h` (§4): authoritative `spelling` + derived fields, `Origin`, `reason`, `SourceLoc`. Names in `snake_case`, like the owner's other repositories. |
| R15 | §4, §12 | "Smart" merge in phase 6, yet "it is the most important requirement" | Contradiction: without merge, the TDD loop (new test → regenerate) always ends in a manual `.new`. | Merge by marked regions in phase 3, right after the gen-impl MVP (§11). **[confirmed]** |
| R16 | §11 (merge) | — | Comparing by full signature would add spurious overloads when the user changes `int` → `std::uint32_t`; and the return type does not distinguish overloads. | Merge key = (name, arity); by types only for overloads of one arity that the run wants; differences in types or `const` → warning, never a new member (§11.2). |
| R17 | §2 | inja templates editable "without recompiling" | If they are loaded from disk, the binary depends on an installation path; and putting logic in templates leaves it untested. | Default templates **embedded** in the binary + `--templates DIR` to override. The logic (signatures, TODOs, default values) lives in C++ and reaches the template already formatted (§15). |
| R18 | §10 | `tool.yaml` | YAML adds yaml-cpp; nlohmann/json is already mandatory because of inja. | Configuration in JSON for the MVP; the loader is isolated so that YAML can be added later. **[confirmed]** |
| R19 | §5 | Placeholder arguments in the generated tests | `{}` is ambiguous between `f(int)` and `f(double)` and does not bind to `T&`. Besides, **running** legacy methods with zero values can hang or touch hardware and take down the whole test binary. | Named locals of exact type; the placeholder (`FAIL()`/`QFAIL`) goes **first**: what follows compiles (it checks the API) but does not run (§9.2). |
| R20 | §5 | Tests on a `const` object | — | `const Foo& const_sut = sut;` for `const` methods; `static_assert(noexcept(…))` for `noexcept` (§9.2). |
| R21 | §4 | Free functions | Calls to the project's own macros (`MY_EXPECT_OK(x)`) or to libc (`strlen`) would be taken for functions under test. | ALL_CAPS identifiers = macros; a list of known C/standard functions + `external_functions` in the config (§7.8). |
| R22 | §8 | Stubs | With `-Werror -Wunused-parameter` (common in strict projects) the stubs do not compile. | Commented-out parameter names in the definitions: `int Foo::f(int /*value*/)`. |
| R23 | §2, §8 | Deterministic | Not made concrete. | No timestamps or absolute paths; stable order (vectors, never `unordered_*` in emission); LF; final newline; no trailing spaces. The goldens verify it (§16). |
| R24 | §7 | Dependencies through `FetchContent` with tags | Tags can move. *Verified* in this environment: GitHub's `/archive/` tarballs are blocked by the proxy; `git clone` works. | Shallow git by tag **and** verification of the commit SHA after the download (configure fails on mismatch). Offline mode with `FETCHCONTENT_SOURCE_DIR_*` (§17). *Implemented and tested.* |
| R25 | §6 | `GTEST_SKIP()`, `INSTANTIATE_TEST_SUITE_P` | They exist since GTest 1.10; before that, `INSTANTIATE_TEST_CASE_P`. | The frontend recognises both names; the backend assumes GTest ≥ 1.10. |
| R26 | §4 | Stubs with `throw` give the *red* phase | *Verified* (GTest 1.16, Qt 6.4.2): GTest catches the exception **per test** and carries on (all red). QTest reports `FAIL! Caught unhandled exception` and **rethrows** → `std::terminate`: the following slots do not run. It is still red, but only the first failure is seen. | `throw` is kept (red guaranteed in both). Documented for QTest (one slot can be run on its own: `tst_foo slotName`). The `hook` policy (§8.3, plan 8.3) lets a handler fail the slot and return, so that QTest goes on. The QTest round trips check what really happens (§16). |
| R27 | §8 | — | *Verified*: with Clang, `-std=c++14 -Wpedantic -Werror`, GTest 1.16's own `TEST` macro fails (`[[maybe_unused]]` is a C++17 extension). Without `-Wpedantic`, or in C++17, it compiles. | In the round trip, C++14 test TUs are compiled with `-Wall -Wextra -Werror` (without `-Wpedantic`); the stubs (without GTest) do get `-Wpedantic`. |

---

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | **tree-sitter in both directions** (MVP). The frontend interface is prepared for an optional libclang frontend (`MT_WITH_LIBCLANG`, future). **[confirmed]** | A single parser dependency, pinned and needing no toolchain; it tolerates code that does not compile; the same type-extraction utilities in both directions. For *skeletons* the syntax is enough: the header explicitly declares what is needed. libclang would require Qt and project include paths just to read signatures. |
| D2 | Binary name `mirrortest`; C++ namespace `mt`; annotation prefix `// @mt:`; markers `// <mt:begin X>` / `// <mt:end X>`; TODOs `TODO(mt):`; diagnostic codes `mt-*`. **[confirmed]** | Consistent with the repo; `@gen:` is too generic and could collide. |
| D3 | Generator in C++17. Output: the **same** code for every `--std`, valid from C++11, except what is listed in §13.3. | Fewer variants = less surface for error. `--std` only changes what really depends on the standard. |
| D4 | Default stub policy `throw std::logic_error`; `--no-exceptions` ⇒ `std::abort()`. | A *red* phase guaranteed in GTest and QTest (both report the failure; QTest aborts after the first, R26), and without exceptions too. |
| D5 | `auto` over a result of the API under test ⇒ **error** by default (`--auto-policy=todo` downgrades it to a warning + placeholder type). | AT §11: "fail loudly". A clear error with `file:line` is better than an invented type. |
| D6 | Default placeholder in gen-tests: `FAIL()` / `QFAIL` (alternative `--placeholder=skip`). **[confirmed]** | Stricter and consistent with TDD; `skip` is available so that CI is not broken when generating in bulk over legacy code. |
| D7 | Default write mode `--overwrite=never`: if the file exists and differs, `<file>.new` is written and a warning is issued; if it is identical, it is not touched. | Non-destructive and idempotent (AT §4). |
| D8 | All or nothing: if there is any error diagnostic, no file is written. Atomic writing (temporary file + `rename`). | Never leave a half-generated tree. |
| D9 | Configuration: defaults < `--config file.json` < command line. No implicit discovery of configuration files. Unknown keys = error. | Predictable; typos in the config fail instead of being ignored. |
| D10 | `#ifndef` guards by default (`"guard": "pragma-once"` optional). | Portability and the most widespread convention. |
| D11 | All generated text (comments, `TODO(mt)`, stub messages and diagnostics) in English. All documentation — design, plan, README, user documentation — and code comments in English as well. | Consistent with the owner's other repositories; the generated code ends up in third-party projects. |

---

## 3. Architecture

```
                 ┌──────────────┐        ┌──────────────┐
  tests (.cpp) ─▶│ preprocess   │        │ preprocess   │◀─ headers (.h)
                 │ (blanking)   │        │ (blanking)   │
                 └──────┬───────┘        └──────┬───────┘
                        ▼                       ▼
                 ┌──────────────┐        ┌──────────────┐
                 │ syntax::Tree │        │ syntax::Tree │   tree-sitter-cpp
                 └──────┬───────┘        └──────┬───────┘
                        ▼                       ▼
            ┌───────────────────────┐   ┌───────────────┐
            │ TestFrontend          │   │ CodeFrontend  │──┐ also reads
            │  recognizer (gtest/qt)│   │ (headers→IR)  │  │ fixtures and
            │  → UsageFacts         │   └───────┬───────┘  │ existing
            │  → Inference          │◀──────────┼──────────┘ headers
            └──────────┬────────────┘           │
                       ▼                        ▼
                ┌──────────────────────────────────────┐
                │   ir::Model   (classes, functions)   │
                └──────┬────────────────────┬──────────┘
                       ▼                    ▼
              ┌────────────────┐   ┌─────────────────┐
              │ ImplBackend    │   │ TestPlanner     │ → ir::TestPlan
              │ (.h + .cpp)    │   └────────┬────────┘
              └───────┬────────┘   ┌────────┴────────┐
                      │            │ GTest / QTest   │
                      │            │ backends        │
                      │            └────────┬────────┘
                      ▼                     ▼
                ┌──────────────────────────────────────┐
                │ OutputPlan  (path, content, action)  │  ← pure, in memory
                └──────────────────┬───────────────────┘
                                   ▼
                ┌──────────────────────────────────────┐
                │ Writer: never/merge/force, .new,     │  ← the only layer with I/O
                │ dry-run (diff), check, atomic        │
                └──────────────────────────────────────┘
```

Principles:

- **Pure core, I/O at the edges.** Everything before the `Writer` is a function
  from data to data. Golden tests, `--dry-run`, `--check` and merge come for
  free from that.
- **Accumulated diagnostics**, not exceptions, for errors in the *input*
  (§12). Exceptions are left for internal or I/O failures.
- **The CLI is in the library too**: `mt::cli::run(args, env)` returns the exit
  code; `apps/cli/main.cpp` only calls it. The goldens run the real CLI in
  process. `env` carries the base directory for relative paths and the output
  and error `std::ostream`s: no `chdir` and no global state.

### 3.1 Modules and files

```
include/mirrortest/
  ir/          model.h  type.h  classify.h        IR, type spelling, classification (by-value, includes)
  syntax/      tree.h  preprocess.h  extract.h    tree-sitter RAII, blanking, node/type utilities
  core/        diagnostics.h  config.h  naming.h  output.h  writer.h  diff.h  merge.h  regions.h  templates.h  source.h
  frontend/    code_frontend.h  test_frontend.h  facts.h  literals.h  annotations.h  inference.h
  backend/     cxx_format.h  impl_backend.h  test_planner.h  gtest_backend.h  qtest_backend.h
  cli/         run.h
src/<same modules>.cpp
templates/     impl/{header,source}.inja   gtest/test_file.inja   qtest/test_file.inja
apps/cli/main.cpp
tests/unit/  tests/golden/  tests/roundtrip/
cmake/       MtDependencies.cmake  MtEmbedFiles.cmake
```

A single static library `mt_core`; the `mirrortest` executable; the test
executables `mt_unit_tests` and `mt_golden_tests`; the round trips are CTest
scripts.

---

## 4. IR

Source: [`include/mirrortest/ir/model.h`](../include/mirrortest/ir/model.h)
(already implemented; it is the contract between modules). Key points:

- `Type::spelling` is **authoritative**: it is what gets emitted. The
  decomposed fields (`base`, `is_const`, `ref`, `pointer_depth`) serve decisions
  (pass by value or `const&`, default value) and only describe the common
  shapes; anything exotic keeps its `spelling` and is treated conservatively.
- `normalize_spelling()` only touches whitespace and never reorders (`T const&`
  is respected). `make_type()` builds the canonical form. Both are implemented
  and tested (`src/ir/type.cpp`).
- `Origin` orders trust: `Defaulted < Inferred < Declared < Annotated`. On a
  conflict the higher one wins and a warning is issued.
- `Method::notes` → `// TODO(mt): …` comments above the declaration.
- `TestPlan`/`Stmt` is neutral: the *arrange/act* code is valid C++ in both
  frameworks and travels as text; what depends on the framework (placeholder,
  assertions) is its own `Stmt::Kind` that each backend renders.

---

## 5. Syntax layer

### 5.1 `syntax::Tree` / `syntax::Node`

Implemented (`include/mirrortest/syntax/tree.h`). `Tree` owns the tree and a
copy of the source (behind a `unique_ptr`, so moving the `Tree` does not
invalidate the `string_view`s of the `Node`s). `parse_cpp()` never fails on bad
input (it yields `ERROR` nodes); it only throws if the runtime rejects the
grammar (ABI). `tests/unit/test_syntax_tree.cpp` pins the node shapes the
frontends depend on (the result of the spike).

### 5.2 Preprocessing: *blanking* that preserves offsets

`syntax/preprocess.h`:

```cpp
struct MacroTable { std::vector<std::string> ignore;              // config.ignore_macros
                    bool expand_mock_methods = false; };          // gmock's macros: test files only (§7.13)
struct QtMarker { enum class Kind { QObject, Gadget, SlotsSection, SignalsSection,
                                    Invokable, DisableCopy };
                  Kind kind; unsigned offset; };
struct BlankedRange { unsigned begin, end; };                      // [begin, end)
struct Preprocessed { std::string text; std::vector<QtMarker> markers;
                      std::vector<BlankedRange> blanked; };
Preprocessed preprocess(std::string_view source, const MacroTable& table = {});
```

Implemented in `include/mirrortest/syntax/preprocess.h`.

Rules (all with unit tests):

1. **Same length** as the input; the `\n` (and `\r`) inside an erased span are
   kept → the lines and columns of the diagnostics remain those of the original
   file. The invariant is checked in every test: `out.text.size() == in.size()`
   and the same `\n` positions. The one exception is a gmock method macro that
   is expanded (below) over several lines: its line breaks are kept, in order,
   at the *end* of its span, so every line after it is where it was, and only a
   column on the macro's last line is off.
2. A leading UTF-8 BOM → 3 spaces.
3. A minimal lexical scanner that **does not touch**: `//` comments (with `\`
   continuation) and `/* */`; string and character literals with escapes; *raw
   strings* `R"delim(…)delim"` with the prefixes `u8R`, `uR`, `UR`, `LR`;
   preprocessor lines (`#` as the first non-blank character, with
   continuations).
4. Only whole identifiers are recognised (token boundaries).
5. Default macro table:

   | Macro | Treatment |
   |---|---|
   | `Q_OBJECT` | spaces; `QObject` marker |
   | `Q_GADGET` | spaces; `Gadget` marker (a gadget is **not** a `QObject`: no signals or slots) |
   | `Q_NAMESPACE`, `Q_REQUIRED_RESULT`, `Q_DECL_DEPRECATED` | spaces |
   | `Q_INVOKABLE` | spaces; `Invokable` marker |
   | `slots`, `Q_SLOTS` | only if followed by a single `:` (spaces/tabs in between) **and** the previous token is `public`/`protected`/`private` → spaces; `SlotsSection` marker at the offset of that specifier. In any other case (`int slots = 0;`, `c ? slots : x`) it is not touched. |
   | `signals`, `Q_SIGNALS` | only if followed by a single `:` and the previous token is an access specifier or a member boundary (`{` `}` `;` `:`). After a specifier (`public signals:`) → spaces, marker on the specifier. After a boundary → `public` + padding (the section must still see an access specifier), marker at the offset of `signals`. |
   | `Q_DECL_OVERRIDE`, `Q_DECL_FINAL`, `Q_DECL_NOEXCEPT`, `Q_DECL_CONSTEXPR`, `Q_NULLPTR` | `override`, `final`, `noexcept`, `constexpr`, `nullptr` + padding |
   | `Q_PROPERTY`, `Q_ENUM`, `Q_ENUMS`, `Q_FLAG`, `Q_FLAGS`, `Q_CLASSINFO`, `Q_INTERFACES`, `Q_DECLARE_METATYPE`, `Q_DECLARE_PRIVATE`, `Q_DECLARE_PUBLIC`, `Q_PRIVATE_SLOT`, `Q_DECL_DEPRECATED_X`, `Q_DISABLE_COPY`, `Q_DISABLE_COPY_MOVE` | function-like: identifier + balanced `( … )` (respecting strings, comments and raw strings inside) → spaces. Only **spaces or tabs** are allowed before the `(`, not line breaks: after `MY_ATTR⏎(void)f();` the `(` is a statement, not the arguments. `Q_DISABLE_COPY*` leaves a `DisableCopy` marker. If no `(` follows, or the parenthesis does not close, only the identifier is erased. |
   | `MOCK_METHOD`, `MOCK_METHODn`, `MOCK_CONST_METHODn` (n from 0 to 20) and their `_T`, `_WITH_CALLTYPE` and `_T_WITH_CALLTYPE` forms — **only when `MacroTable::expand_mock_methods` is on** (the test-file reader sets it; a header's reader does not) | replaced by the member each declares, padded with spaces: `MOCK_METHOD(int, read, (), (const, override));` → `int read() const override;`; `MOCK_METHOD1(write, void(int))` → `void write(int) override;` (the old macros do not say `override`, but a mock method is there to override one). Done by `syntax/mock_macros` (text in, text out; the argument lists have a regular shape): parentheses that protect a comma (`(std::map<int, int>)`) are dropped, the specifiers (`const`, `ref(&)`, `noexcept`, `override`, `final`; `Calltype(...)` is ignored) are written in the order the language wants, and white space is one space. A `;` is added after a macro that has none (the old macros need none); the declaration is shortened to fit (the space after a comma goes first) and, if it still does not fit, or the arguments hold a comment or a literal, or are not what the macro takes, the macro is **erased** like any other, so that it cannot spoil the declarations around it. A word that looks like one of them but has no `( … )` is left alone. |
   | `config.ignore_macros` | object-like or function-like depending on whether `(` follows (same rule) |
   | Export macro: an identifier `[A-Z][A-Z0-9_]*` between `class`/`struct` and another identifier that is not `final` (`class FOO_EXPORT Widget :`) | spaces. `class FOO final` and `class FOO {` keep `FOO`: it is the name. |

6. The markers are associated afterwards by range: `QObject`/`Gadget` → the
   `class_specifier` that contains it; `SlotsSection`/`SignalsSection` → the
   `access_specifier` at that offset; `Invokable` → the next member
   declaration; `DisableCopy` → the `class_specifier` that contains it.
7. A blanked macro is invisible to the scanner: it does not change which token
   was "the previous token" (so `Q_OBJECT` between `{` and `signals:` does not
   prevent it from being recognised).
8. Checked with ASan/UBSan on 21,000 truncated and corrupted inputs (length and
   line-break invariants; no out-of-range accesses). The unit test
   `PreprocessRobustness` repeats part of it with a fixed seed.

### 5.3 Extracting types from nodes (`syntax/extract.h`)

Implemented. A type is read as **text**, not rebuilt from the shape of the
declarator tree (which is inside out: in `const char* const names[3]` the name
sits in the middle of its own type):

    type   = qualifiers and type specifier, in source order
    suffix = the declarator's text without the name or the function part

so `const char* const names[3]` is `const char` + `* const[3]`. It works for any
shape —including those nobody foresaw— and keeps exactly what the user wrote.
The structured fields of `ir::Type` (`is_const`, `ref`, `pointer_depth`) are
only filled in if the suffix is of the simple kind (`*`, `&`, `const`); for
function pointers, arrays and pointers to members the `spelling` is exact and
nothing finer is claimed.

Node shapes of tree-sitter-cpp 0.23.4 it depends on (verified against the real
tree, pinned in `test_extract.cpp`):

- `virtual` is an **anonymous** token: it is searched among *all* the children
  (`has_token`). `...` in `f(int, ...)` is one too and used to get lost
  silently (`f(int, ...)` was read as `f(int)`); it is now a parameter of type
  `...`.
- `int (*cb)(int)` is a `function_declarator` whose declarator is a
  `parenthesized_declarator`: it **looks like a method and is a variable**
  (`is_function_pointer`, no `function`).
- A nested class/enum is a `field_declaration` with a `struct_specifier` /
  `enum_specifier` as its type and **no declarator**.
- `reference_declarator` does not label its inner declarator
  (`inner_declarator` uses the last named child); an unnamed parameter carries
  `abstract_*_declarator`.
- `int const x` puts the `type_qualifier` *after* the type; the order is kept.
- `constexpr` and `mutable` are `type_qualifier`s but not part of the type;
  `static`/`inline`/`extern` are `storage_class_specifier`s.
- A non-constant `noexcept(expr)` is not asserted: only `noexcept` and
  `noexcept(true)` count.
- An `init_declarator` (`int x = 3`) carries the value separately: it is not
  part of the type.
- Packs: `Args&&... args` → a nested `variadic_declarator`; the suffix keeps the
  `...` (`Args&&...`).

## 6. Code frontend (headers → `ir::Model`)

Implemented in `frontend/code_frontend.h` (`read_declarations`, `read_header`).
It is used in three places: the input of gen-tests; existing headers in gen-impl
(already-known API + merge); fixtures and test classes inside the tests. That is
why it reads **declarations**, not headers: any translation unit will do.

### 6.1 Traversal

- `translation_unit` → `namespace_definition` (including the
  `nested_namespace_specifier` `a::b`), `linkage_specification` (`extern "C"`,
  with or without braces), `preproc_if`/`preproc_ifdef`/`preproc_elif`/
  `preproc_else` (all branches, §6.5), `preproc_include` (path without
  delimiters + `is_system`). Anonymous namespaces: skipped in a header
  (internal linkage, not testable from outside); read, with the namespace around
  them, in a test file (`read_declarations(…, anonymous_namespaces = true)`),
  where the fakes, fixtures and helpers of the file are very often in one and
  are the file's own (§7.7).
- `class_specifier`/`struct_specifier` **with a body** → `Class`. Without a body
  (forward declaration, `class Foo* f();`) → ignored. `union` → ignored.
  `struct S { … } s;` defines `S` as well as declaring `s`. Inside a
  `template_declaration` → `template_params` (the text between `<>`, e.g.
  `typename T, int N = 4`).
- `enum_specifier` **with a name and a body** → `Enum` (`is_scoped` through the
  anonymous token `class`/`struct`; `underlying` through the `base` field;
  enumerators through their `name` field). Anonymous and forward-declared ones
  are ignored.
- `declaration`/`function_definition` at namespace scope with a
  `function_declarator` (not a function pointer) → `Function` (includes
  `static`, `inline`, operators and trailing return). These are **not**
  functions: variables, function pointers, `int Foo::bar() {}` (defines a
  member), function templates (`mt-unsupported` warning and they are skipped).
- A class read twice (`#if` branches, or two declarations) is **one** class: the
  members are merged and a signature already present (name + parameter types +
  `const` + ref-qualifier) is not repeated; neither are bases, fields or
  nested types.

### 6.2 Members (shapes verified against the real tree)

| tree-sitter shape | Result |
|---|---|
| `field_declaration` / `declaration` / `function_definition` with a `function_declarator` that is not a function pointer | method (one per declarator) |
| `declaration` whose declarator is the class name and with no type | `Constructor`; `explicit_function_specifier` → `is_explicit` |
| `destructor_name` (`~A`) | `Destructor` |
| `operator_cast` (`explicit operator bool() const`, `operator const char*() const`) | `Conversion`, name `operator <target type>`, no `ret` |
| a name that starts with `operator` followed by something that does not continue an identifier | `Operator`; the name is normalised (`operator ==` → `operator==`, `operator  new [ ]` → `operator new[]`); `operatorX` is an ordinary method |
| `delete_method_clause` / `default_method_clause` | `is_deleted` / `is_defaulted` |
| `default_value` = `0` | `is_pure` |
| `virtual` (anonymous token), `static`, `virtual_specifier` `override`/`final`, `const`, `noexcept`, ref-qualifier | flags |
| `template_declaration` inside the class | method with `is_template` |
| `field_declaration` with a non-function declarator (includes `int (*cb)(int)`, arrays, bitfields, `static constexpr`) | `Field` (one per declarator: `int a, *b;`) |
| `field_declaration` with no declarator whose type is a class/enum, `type_definition`, `alias_declaration` | name goes to `nested_types` (and, for a public enum, to `nested_enums` too: `Foo::Mode m{};` is all a test needs of it); a nested class or struct with a body is also read, like any other, into `Class::nested_classes` (with `nested_access`, and the outer class's `ns`): it is not a `Class` of the model, so nothing that iterates `Model::classes` sees it, but a planner can ask whether `Foo::Item` is default-constructible. The test planner makes the public ones classes of their own before it plans (§9.1, `flatten_nested_classes`). Unions and anonymous structs are not read |
| `friend_declaration`, `using_declaration`, `static_assert_declaration` | not members |

A method already present (same signature) is not added twice. The return type is
the declared one or that of `-> T`; `auto` without a trailing return stays as
`auto`.

Access: `private` by default in a `class`, `public` in a `struct`; every
`access_specifier` changes it. **Qt** (with the preprocessing markers, §5.2):
`SlotsSection`/`SignalsSection` at the offset of an `access_specifier` →
`QtRole::Slot`/`Signal` up to the next specifier; an `Invokable` between the end
of the previous member and the start of this one → `QtRole::Invokable`;
`QObject` → `is_qobject`, `Gadget` → `is_gadget`, `DisableCopy` →
`disable_copy`, always provided the marker is in the class and not inside a
nested class.

### 6.3 "Constructible" classes

A class is *default-constructible* if: it declares no constructors, or declares
one that is public, not deleted and has no mandatory parameters; and it is not
abstract (no `is_pure`). `Q_DISABLE_COPY` does not affect this (only copying).
The planner uses it (§9.2).

### 6.4 Not supported (an explicit diagnostic, never silence)

Each of these is an `mt-unsupported` warning with a position, and none is
silent: function templates (skipped), explicit and partial specialisations
(`template <> class Box<int>`; skipped, only the primary template is read),
constructor inheritance (`using Base::Base;`), `concept` definitions (skipped) and
`requires` clauses (ignored: the class or method is read as if unconstrained).
Members generated by unrecognised macros are the user's to list in
`ignore_macros`. Class templates are read and carry `template_params`; the
planner decides what to do with them.

### 6.5 Conditional compilation

All branches are visited. Declarations with the same signature are merged (the
first in source order wins). If two *alternative* branches of one conditional
(`#if` and its `#elif`/`#else`, at any depth) declare the same (name, arity) of
the same class with different signatures, both are kept; with the same signature
but another return type or flags, the first is kept; either way an
`mt-conditional` note says so, with the position of both: the user is the one
who knows which configuration applies. Overloads that are not alternatives (both
unconditional, in the same branch, one conditional and one not, or in different
conditionals) are ordinary overloads and say nothing.

### 6.6 Parse errors

The parser always gives a tree. Every `ERROR` node (the outermost; up to 10,
then a note "and N more parse errors") is an `mt-parse` warning with its
position, and what surrounds it is read: a header that almost parses is far more
useful than none.

---

## 7. Tests frontend (tests → `ir::Model`)

### 7.1 Pipeline

1. `preprocess` (§5.2) → `parse_cpp`.
2. Framework detection (`--framework auto`): GTest if it includes
   `gtest/gtest.h`/`gmock/gmock.h` or uses `TEST`/`TEST_F`/`TEST_P`; QTest if it
   includes `QtTest`/`QTest`/`QtTest/QtTest` or uses `QTEST_*MAIN` or has
   `slots` sections. Both or neither → `mt-framework` error (ask for
   `--framework`).
3. Collection of the **bodies to analyse**: every function body in the file
   (`TEST*`, fixture methods including `SetUp`/`TearDown`/the constructor,
   QTest *slots* inside and outside the class, helper functions), each with its
   scope (§7.4).
4. Statement traversal with a symbol table → **facts** (`facts.h`).
5. Name classification (§7.7) → which classes/functions have to be generated.
6. **Inference** (`inference.h`): facts → `ir::Model` with `Origin`, `reason`,
   `notes` and diagnostics.
7. **Rounds** (`pipeline.h`, `analyse()`): steps 5 and 6 repeat while inference
   learns the type behind a chained call (§7.9), at most `kMaxRounds` (4) times.

Separating facts from inference allows each half to be tested on its own:
*C++ fragment → expected facts* and *facts → expected model*.

### 7.2 Facts

Defined in `frontend/facts.h`. Facts refer to each other by **index** into the
vectors of `TestFileFacts` (`FactRef{kind, index}`), never by pointer, so the
set can be copied and moved without care. What the frontend records, and what
it does **not** decide:

| Fact | Content | Notes |
|---|---|---|
| `MethodCall` | receiver (`TypeName`), method, `args`, `on_const`, `via_arrow`, `use`, `where`, `ns` | `x.m()`, `x->m()`, `(*p).m()` |
| `ScopedCall` | `scope` as written, name, `args`, `use` | `X::m()`: whether `X` is a class or a namespace is decided by §7.7 (R13) |
| `FreeCall` | name, `args`, `use`, `owner` | function, macro, libc or functional *cast* `Foo(3)`: decided by §7.8. `this->m()` falls here too. `owner` is the class of the file whose method contains the call (`TEST_F` bodies count as methods of the fixture) |
| `FieldAccess` | receiver, field, `on_const`, `via_arrow`, `use` | `obj.x` not called; `obj.x = 5;` gives `Assigned` with the type of the right-hand side |
| `Construction` | type, `Form`, `args` | `Default`, `Direct`, `Brace`, `Copy` (`Foo f = 5;`), `Functional` (`Foo g = Foo(3);`), `New`, `MakeUnique` (also `make_shared`) |
| `TypeUse` | type, `from_throw_assertion` | types of declarations, parameters, return values, `new`, *casts*, `catch` and `EXPECT_THROW(…, E)` |
| `Unresolved` | `ChainedCall`, text, `where`, `root`, `call`, `resolved` | see §7.3: `call` is the call made on the unknown result (everything but the receiver) and `root` the call it hangs from |
| `Enumerator` | `scope` (`Color`, `acme::Color`), name, `where`, `ns` | `Color::Red` as a value, neither called nor a type: an enumerator if `Color` is an enum, which is as likely not to be (a static member `Limits::kMax`, a constant `acme::kMax`); decided by §7.7. `Box<int>::value` and `::kMax` are not recorded |
| `TestCaseInfo` | `TEST`/`TEST_F`/`TEST_P`, suite, name, `ns` | |

`ValueInfo` (an argument of a call or construction): `text`, `where`, `type`
(that of a literal, of a declared variable, of a *cast*; absent if nothing says),
`warnings` (from classifying the literal; emitted only if the type is used),
`is_string_literal` (its type is decided by the context), `is_null_pointer`,
`is_lvalue`, `is_moved`, `is_address_of` (and then `type` is the pointer's:
`&n` is `int*`), `var_name`, `from` (the call or field of the API it comes
from), `auto_origin` (§7.3) and `enumerator` (an index into `TestFileFacts::enumerators`
when the expression is `Color::Red`; its `type` is absent until classification, which
knows whether `Color` is an enum, gives it the enum's). `ResultUse::hint_enumerator` is
the same for a hint: `EXPECT_EQ(paint.color(), Color::Red)` hints that `color()` returns
`Color`, if that is a type.

`TestFileFacts` also keeps what the file itself declares: `local` (classes,
enums and functions; fixtures are never generated), `includes`,
`using_namespaces`, `using_declarations` (`using acme::Foo;`) and
`local_type_names` (aliases and `typedef`s).

`ResultUse{kind, hint}` with `UseKind { Unknown, Discarded, Compared,
Condition, Assigned, AutoBound, Argument, Receiver }`. **The first context to
claim a result keeps it**: `Compared` inside `EXPECT_EQ` does not become
`Argument`. `hint` is the type the context implies, as written
(`const std::string&`, `unsigned`): removing the top-level `const`/`&` is a job
for the inference (§7.9), not for the frontend.

Differences from the initial sketch of this section: `is_const` disappears from
`ValueInfo` (constness goes in `ir::Type`); `is_static` is not a field (it is a
`ScopedCall`); the annotations of a call live on its fact (`CallAnnotation`, §7.11); `CtorFact` becomes `Construction`
with explicit forms; `ResultUse.where` is redundant (the call carries it);
`Receiver` is added.

### 7.3 Symbol table

Scopes nested by `compound_statement`, `if`/`while`/`for`/`switch` (a variable
declared in the condition lives in its scope), `try`/`catch` and lambdas;
declaration happens in order. Sources: local declarations (`T v;`, `T v(…)`,
`T v{…}`, `T v = …`), parameters of functions, lambdas and `catch`,
`QFETCH(T, v)` (text: it is split at the **last top-level comma**, by
`syntax::parse_qfetch`; *verified*: the node varies depending on whether `T` is
primitive, §7.12), members of the
fixture/test class (including those of its local base classes), `for (T x : …)`.
Each symbol keeps the complete declared type (to know about `const`, reference,
pointer, smart pointer).

Receiver of a call `x.m()` / `x->m()`:
- `x` local/member of type `T`, `T&`, `const T&` → class `T`, `on_const` if
  `const`. `x->m()` on a value, or `x.m()` on a pointer, is not a recognisable
  call.
- `x->m()` with `T*`, `const T*`, `std::unique_ptr<T>`, `std::shared_ptr<T>`,
  `QScopedPointer<T>`, `QSharedPointer<T>`, `QPointer<T>` → `T`
  (`shared_ptr<const T>` → `on_const`). `p.reset()` with a dot is a call on the
  pointer, not on `T`.
- `(*p).m()` is the same as `p->m()`.
- Unknown receiver (an undeclared identifier, an `auto` with no deducible type,
  an `auto` parameter of a lambda) → `mt-unknown-receiver` note, ignored.
- Chained call `a.b().c()` → the type of `a.b()` is unknown: the call to `b` is
  left as `Receiver` and an `Unresolved` is recorded for `c`. **Every** link is
  recorded: in `a.b().c().d()`, `c` hangs from the call `b`, and `d` hangs from the
  link `c` (`Unresolved.root` of kind `Unresolved`). Only the first one shows as
  an error when nothing says what `b` returns (§7.9, `mt-chained-call`); the
  rest only matter once the first is resolved (§7.9, "Propagation and
  chains"). `Unresolved.root` keeps the call it hangs from: in `Foo(3).size()`
  the root is a `FreeCall` named `Foo`, and if §7.7 concludes that `Foo` is a
  class there is nothing left unresolved.

**`auto` and variables**: `auto x = Foo(1)` does not know whether `Foo` is a
type or a function (the syntax does not say), so `x` is left without a type and
`x.run()` is a chained call rooted in `FreeCall Foo`. If `auto x = call;`, the
symbol remembers the call (`auto_origin`): **what is later done with `x` is
evidence about the call**. `auto n = foo.count(); EXPECT_EQ(n, 5u);` leaves
`count()` as `AutoBound` with `hint = unsigned int` (reason "through variable
'n'"); the first `hint` wins and contexts without a hint (argument, discard,
receiver) add nothing. With `auto x = 3u;` or `std::make_unique<Foo>()` the type
is deduced (`auto` by value drops `const` and `&`; `auto&` keeps the constness
of what it binds to). *Reason for the change*: without it, the most common
pattern (`auto r = calc.add(2, 3); EXPECT_EQ(r, 5);`) forced `add` to be typed
by hand even though the next line says it.

**Members of the file's own classes**: a by-value member of class type
(`Foo foo_;`, `Baz c_{1, 2};`, `Qux d_ = Qux(2);`) is a `Construction` (a fixture
that has it requires `Foo` to be default-constructible), and so are the
constructor's initialisers (`F() : a_(3), b_{4} {}`, also when defined outside
the class). Pointers, references and `static` members construct nothing. A
member that a constructor defined *inside* the class initialises is not also
default-constructed; one initialised only by a constructor defined outside the
class still is (a spurious default constructor, harmless, in the generated class).

### 7.4 Scopes by kind of body

| Body | Symbols visible besides the locals |
|---|---|
| `TEST(S, N)` | the file's globals |
| `TEST_F(F, N)`, `TEST_P(F, N)` | members of the class `F` defined in the file (if it is not there: `mt-unknown-fixture`, warning, and the body is read anyway); `GetParam()` typed by `TestWithParam<T>` or `WithParamInterface<T>` (without a parameter type, `GetParam()` is just another free call) |
| fixture methods, inside or outside the class, constructors and destructors | members of the fixture and of its local bases; in a constructor, also its parameters and the initialisers |
| helper functions | their parameters |
| slots/methods of the QTest class | members of the test class (§7.12) |

`TYPED_TEST`/`TYPED_TEST_P` are not supported: `mt-unsupported` warning and the
body is skipped. Assertion macros are recognised in any position (also nested,
`ASSERT_NO_THROW(EXPECT_EQ(a, b))`) and are never a `FreeCall`;
`EXPECT_CALL`/`ON_CALL` discard the **whole** chain
(`.Times(2).WillOnce(Return(x))`): `Return`, `_` or `Invoke` are not code under
test.

### 7.5 Literal classifier (`literals.h`)

| Literal | Type |
|---|---|
| unsuffixed integer that fits in 32 signed bits | `int` |
| unsuffixed decimal integer outside `int` | `long long` + `mt-literal-range` warning (on a 32-bit and on a 64-bit data model the real type differs) |
| unsuffixed hex/oct/bin in `(INT_MAX, UINT_MAX]` | `unsigned int` (the same in ILP32 and LP64) |
| unsuffixed hex/oct/bin > `UINT_MAX` | `long long` if it fits, otherwise `unsigned long long`; + warning (the standard gives `long`/`unsigned long` in LP64 and `long long` in ILP32) |
| suffixes `u` · `l` · `ul`/`lu` · `ll` · `ull`/`llu` | `unsigned int` · `long` · `unsigned long` · `long long` · `unsigned long long`; `l`/`ul` with a value outside 32 bits → warning "does not fit a 32-bit long, which is what it is on 32-bit targets and on 64-bit Windows" |
| `z` · `uz`/`zu` (C++23) | `std::ptrdiff_t` (note: the signed type corresponding to `size_t`) · `std::size_t`; if `--std` < 23 → warning |
| floating-point unsuffixed · `f` · `l` | `double` · `float` · `long double` |
| `f16`, `bf16`, … (C++23) | not supported → `mt-literal` |
| `-`/`+` sign included in the token (R11) | keeps the type (`-1u` is still `unsigned int`) |
| digit separators `'` | ignored |
| `'a'` · `u8'a'` · `u'a'` · `U'a'` · `L'a'` · `'ab'` | `char` · `char` (≤C++17) / `char8_t` · `char16_t` · `char32_t` · `wchar_t` · `int` + warning |
| string `"…"`, `R"(…)"`, concatenated | "`char` string": the final type is decided by the context (§7.6) |
| `u8"…"` · `u"…"` · `U"…"` · `L"…"` | `const char*` (≤17) / `const char8_t*` · `const char16_t*` · `const char32_t*` · `const wchar_t*` |
| `true`/`false` · `nullptr` | `bool` · pointer of unknown type (`mt-unknown-param`) |
| `"…"s` · `"…"sv` | `std::string` · `std::string_view` |
| integers with `h`/`min`/`s`/`ms`/`us`/`ns` | `std::chrono::hours` … `nanoseconds` |
| other user-defined literals | `mt-literal` |
| `static_cast<T>(e)`, `(T)e`, `T(e)`, `T{e}` | `T` (`Declared`) |
| `sizeof(…)` | `std::size_t` |
| enumerator of a known enum `E::X` | `E` |

Implemented in `frontend/literals.h` (`classify_number`, `classify_character`,
`classify_string`, `classify_literal`). It works on the literal's **text**, so it
is tested without a parser; `classify_literal` dispatches by node type
(`number_literal`, `user_defined_literal`, `char_literal`, `string_literal`,
`raw_string_literal`, `concatenated_string`, `true`/`false`, `null`) and sets
the position of the type. Details settled while implementing it:

- Every inferred type carries `Origin::Inferred` and `reason = "literal <text>"`,
  which is what ends up in `// TODO(mt): … inferred from literal 3u`.
- tree-sitter lexes `-1`, `+2` and `-1u` as **one** `number_literal`, but `- 3`
  is a `unary_expression`: that case is resolved by the typing of expressions
  (2.4). `-1u` is still `unsigned int`; `-2147483648` is `long long`, because
  `2147483648` does not fit in `int` whatever comes in front of it.
- A `u` integer above `UINT_MAX` → `unsigned long long` + `mt-literal-range`
  (the standard would give `unsigned long` in LP64). `l` only gives `long`; if
  the value does not fit in 32 bits, a warning that a 32-bit `long` cannot hold it. `unsigned long long`
  overflow → that type + `mt-literal` "too large". An invalid or user-defined
  suffix (`3f`, `3_km`, `1.5s`, `1.5f16`) is `Unsupported` + `mt-literal`, with
  no type.
- `0x1e5` is the integer 485, not a float: in hexadecimal the exponent is `p`.
- Ordinary string (`"x"`, `R"(x)"`, `"a" "b"`): `Kind::String` **with no type**;
  the context decides (§7.6). With a prefix (`L u U u8`) it is a typed pointer;
  with the suffix `s`/`sv`, the `std::basic_string[_view]` of the corresponding
  character.
- Features of a standard newer than `--std` (separators `'` and `0b`: C++14;
  `"x"s` and chrono: C++14; `"x"sv` and `u8'a'`: C++17; suffix `z`: C++23) give
  `mt-std`, not a different type.
- `character_count` counts each escape (`\x41`, `\101`, `\u00e9`) and each UTF-8
  code point as one character: `'\x41'` is `char`, `'\x41\x42'` is `int`.
- Not this module's job: `static_cast<T>(e)`, `(T)e`, `T(e)`, `T{e}`,
  `sizeof(…)`, `E::X`. They are expressions with a type of their own; the tests
  frontend types them (2.4).

### 7.6 Usage context → type hint

| Context | Hint for the result / argument |
|---|---|
| `EXPECT_/ASSERT_EQ, NE, LT, LE, GT, GE (a, b)` | each side takes the type of the other; string → `std::string` |
| `EXPECT_/ASSERT_TRUE/FALSE(c)` | `bool` |
| `EXPECT_/ASSERT_STREQ, STRNE, STRCASEEQ, STRCASENE` | `const char*` (`const wchar_t*` if the literal is `L"…"`) |
| `EXPECT_/ASSERT_FLOAT_EQ` · `DOUBLE_EQ` · `NEAR(a,b,eps)` | `float` · `double` · `double` |
| `EXPECT_/ASSERT_THROW(stmt, E)`, `ANY_THROW`, `NO_THROW`, `EXPECT_DEATH(stmt, re)` | `stmt` is analysed (it can be a `{…}` block); its result is `Discarded` |
| `EXPECT_THAT(v, matcher)` | no hint (`mt-unknown-return`); matchers: later phase |
| `… << "msg"` | the left operand is unwrapped |
| `QCOMPARE(a, b)`, `QCOMPARE_EQ/NE/LT/LE/GT/GE`, `QTRY_COMPARE`, `QTRY_COMPARE_WITH_TIMEOUT` | each side the other's; string → `QString`. The timeout is code too |
| `QVERIFY(c)`, `QVERIFY2(c, m)`, `QTRY_VERIFY*` | `bool`; the message and the timeout are code too |
| `QVERIFY_EXCEPTION_THROWN(stmt, E)` · `QVERIFY_THROWS_EXCEPTION(E, stmt…)` · `QVERIFY_THROWS_NO_EXCEPTION(stmt…)` | `stmt` is analysed (beware: reversed order between the first two; the commas of `stmt` split it into several macro arguments and all of them are read) |
| `QTEST(actual, "column")` | `actual` is read, with no hint: its type is that of the column, which only the `_data` function knows |
| `if`/`while`/`for`/`do` (condition), `!x`, `x && y`, `x \|\| y`, `c ? … : …` | `bool`. The condition of a `switch` is an integer: no hint |
| `x == lit`, `!=`, `<`, `<=`, `>`, `>=` | type of the literal |
| `T v = call;`, and `T v(call);` / `T v{call};` when `T` is **not** a class | `T` without the top-level `const`/`&`; if `T` is a non-const `U&` → return `U&` (a temporary does not bind). The frontend keeps `T` as declared; stripping qualifiers is the inference's job. With a class `T`, `Bar b(call);` is a constructor argument (`Argument`, no hint) |
| `v = call;` with `v` declared `T` | `T` |
| `auto v = call;` (also `auto&`, `const auto&`) | `AutoBound` → `auto` policy (D5) |
| expression statement `call;` | `Discarded` → `void` + `mt-unused-result` warning |
| `GTEST_SKIP`, `FAIL`, `SUCCEED`, `ADD_FAILURE`, `QFAIL`, `QSKIP`, `QEXPECT_FAIL`, `QWARN` | ignored |

### 7.7 Name classification

Implemented in `frontend/classify.h` (the lists of names that belong to somebody
else are in `frontend/known_names.h`). A `TestFileFacts` goes in and a
`Classified` comes out: the facts plus whatever classification added, the
**classes** and the **functions** to generate —each with the indices of the
facts that mention it— in order of first use in the source, and diagnostics.
Steps, in this order:

0. enums from `Color::Red` (below), before the classes, because a name found to be
   an enum is not also a class;
1. classes from types, constructions and receivers;
2. `X::m()` calls (R13);
3. unqualified calls (§7.8);
4. calls pending on a functional *cast* (§7.3);
5. exception types.

**Class candidate**: it appears as the type of a declaration, member,
parameter, return value, `catch` or alias (`TypeUse`), as a base class of a class
the file defines (`class MockSensor : public Sensor`, §7.13), in `new N(…)`, as a
template argument of `make_unique`/`make_shared`/a smart pointer, as the
receiver of a call or of a field, in a construction, as `N(…)` when it is also a
class through one of those routes (a functional *cast*), or as the scope of an
`N::m()` call (R13, below). A type that appears only in `EXPECT_THROW(…, E)` is
**not** a candidate (see the end of this section).

**External** (never generated):
- in `std::`, `testing::`, `QTest::`, `Qt::` or `__gnu_cxx::`; or with
  `using std::x;`/`using testing::x;` (the name becomes theirs);
- the Qt pattern `^Q[A-Z]`; fundamental types and those of `<cstddef>`/
  `<cstdint>`/`<cstdio>` and Qt's *typedefs* (`qint64`, `uint`, …);
- with `using namespace std;`, the names of the standard library's types
  (`string`, `vector`, `unique_ptr`, …); without it, `string` is just another
  name;
- with `using namespace testing;`, the names of GoogleTest's and gmock's types
  (`Test`, `TestWithParam`, `NiceMock`, `StrictMock`, `Matcher`, … —
  `known_names.h`, `is_testing_type`); `testing::NiceMock<T>` needs no such line;
- declared in the file itself (fixtures, helper structs, enums, aliases and
  `typedef`s) or a template parameter;
- declared in an existing header that was analysed (§7.10): by simple name if it
  is used unqualified; if it is qualified (`other::Known`), the header's
  namespace has to end in the qualifier as written;
- `Outer::Inner` with a foreign `Outer`: somebody else's nested type;
- `config.external_types`: a simple entry (`Legacy`) covers any qualifier; a
  qualified one (`vendor::Driver`) also covers the short form.

**To generate** = candidates − externals. A type nested in a class *to be
generated* (`Outer::Inner`) is reported once (`mt-unsupported`) and not
generated.

**Namespace of a class** (decided per class, not per mention), in this order,
and the first rule that applies wins:
1. a qualified name (`ns::Foo`) is that class; `using acme::Foo;` or any
   qualified mention in the file says where `Foo` lives for the rest of the
   unqualified mentions. If the same name appears with two different
   qualifiers they are **two classes**, and the unqualified mentions go to the
   first one (`mt-ambiguous-scope`, warning);
2. the annotation `// @mt: class Foo namespace ns` (only for what is not
   qualified);
3. the `namespace ns { … }` the test is in;
4. exactly one `using namespace ns;` (not counting `std`/`testing`/`QTest`) →
   `ns`, with an `mt-ambiguous-scope` note;
5. several `using namespace` → `config.default_namespace` if it exists;
   otherwise global and an `mt-ambiguous-scope` warning;
6. `config.default_namespace`;
7. global.

**`X::m(args)` (R13)**, with `X` the last qualifier and the rest its namespace:
0. `std::`/`testing::`/`QTest::`, an external `X` (`QString::number`,
   `Fixture::Helper`, a class from a header) or a function already declared in
   the file or in a header (`helpers::twice`) → nothing to generate;
1. `X` is a class to be generated → `static` method of `X`;
2. the file shows `X` as a namespace —`namespace X {…}` (also where the tests
   are placed), `using namespace X;`, the qualifier of a type (`X::Foo`), or the
   namespace of something declared in the file or in a header— → free function
   of that namespace, with no warning;
3. if there is no way to know: `X` capitalised → class (it is created) with an
   `mt-ambiguous-scope` warning; lowercase → namespace function, with the same
   warning. The second call `X::other()` already finds the class and does not
   warn again.

### 7.8 Free functions

An unqualified call `f(args)` (also `::f(args)`) is, in this order:
1. a name in `config.ignore_macros` → nothing;
2. the name of a class to be generated → a **functional construction**
   (`Foo(3)`): a `Construction::Functional` is added and the class picks it up.
   The qualified spelling `acme::Foo(3)` is the same when `acme::Foo` is a class
   the file uses elsewhere (it is decided with the scoped calls, §7.7, before
   `acme` is taken for a namespace); with no such evidence it stays a function
   `Foo` of the namespace `acme`, as the unqualified spelling does;
3. ALL_CAPS (R21) → a macro, nothing. (A class called `ADC` beats the macro rule
   thanks to step 2);
4. the name of somebody else's type (`QString("x")`, `size_t(3)`, the fixture) →
   a *cast* to a foreign type, nothing;
5. a function of the C library (`strlen`, `memcpy`, `abs`, `printf`, `assert`,
   `offsetof`, …), a Qt function `^q[A-Z]` (`qDebug`), a member inherited from
   the GoogleTest fixture (`SetUp`, `GetParam`, `RecordProperty`, …) or a name
   brought in with `using std::x;` → nothing;
6. with `using namespace std;`, a standard function (`move`, `swap`, `max`, …)
   → nothing;
7. `config.external_functions` (a qualified entry `vendor::init` also covers
   `init()`), a function defined or declared in the file or in a header →
   nothing;
8. a call inside a fixture to a method (or field) of the fixture itself or of its
   bases, local or from headers → nothing (`FreeCall.owner`);
9. otherwise → a **free function to generate**. Its namespace is the one the file
   writes in another call `ns::f(…)` if there is one; if not, the same rules 3–7
   above as for classes.

A call to a local variable (lambda, function pointer, `std::function`) never gets
here: the frontend (§7.3) treats it as a call on the object, and it only
produces a fact (`operator()`) if the object is of a class.

**Pending calls** (§7.3): an `Unresolved` rooted in a `FreeCall` (or a `ScopedCall`:
`acme::Foo(3).size()`) whose name turns out to be a class
(`Foo(3).size()`, `auto f = Foo(1); f.run();`) becomes an
ordinary `MethodCall` of that class (`Unresolved::resolved`, `method_call`),
with the use it already had; if the root is a *cast* to a foreign type there is
nothing left to resolve; with any other root (`foo.make().run()`) they stay
unresolved and the inference applies the policy of §7.9 to them.

**Enums** (plan 7.4b): `Color::Red` as a value (`Enumerator` facts) names an enum when
the file or a header declares one (then it is not generated, but the value and the
side it is compared with get its type), and an enum **to generate** when nothing else
`Color` could be explains it. It is not generated when it is external (`std::`,
`testing::`, `Qt::` and the rest of §7.7's list, a class of the file or a header,
configuration), when its name does not begin with a capital (`acme::kMax`: a namespace;
the same guess as for `Foo::create()`), when it is used as a class (a method called on
it or a field read, a static call `Color::parse()`, a construction with arguments
that are not one of its own enumerators, `new Color`; the comparison operators that
a test's `==` produces do not count), or when its qualifier is a class of the test
(`Panel::Mode::Fast`: nested types are not generated). A declaration `Color c;`, a
parameter or a copy of an enumerator are what an enum has too: no evidence either way.
The enumerators are listed once each in order of first use. The namespace is the one in
the name (`acme::Color::Red`) or, as for a class, where the test is. A generated
enum is `known` to the classes pass, so no class `Color` is made of its type uses or
the comparisons of its values; the types of the facts that mention it
(`ValueInfo::type`, `ResultUse::hint`) are filled in at this point, with
`Origin::Inferred` and the reason `enumerator Color::Red`.

**Exception types** (`EXPECT_THROW(stmt, E)`): `E` is not a candidate on its own,
because generating an exception class requires a base that the test does not
state. It must be external (`std::runtime_error`), come from a header, or be a
class through some other route; otherwise `mt-unknown-type` (warning).
*Reason*: inventing `class BadInput {}` without `std::exception` would make
`EXPECT_THROW` fail to compile.

**Opt-in** (plan 7.4c): with `exception_base` in the configuration (`"std::runtime_error"`)
the base is said, and `E` is a class to generate after all (`ClassTarget::thrown`,
`thrown_at` the first such assertion; the namespace as for any class): the same
`E` that the test also builds or calls is the same class, which is also thrown. The
option does nothing under `no_exceptions`: there is nothing to throw, and the warning
stays. What is external, in a header or in the file is as before not generated.

### 7.9 Inference (facts → model)

- **Unification**: the uses of a method are grouped by (class, name, arity). A
  different arity = a different overload. The same arity and different argument
  types → **one** method; the one with the higher `Origin` wins and, on a tie,
  the first in source order; `mt-conflict` warning with the alternatives. For
  overloads by type: the `overload` annotation.
- **Parameters** from `ValueInfo`:
  - literal → its type (by value); string → `config.string_literal_param`
    (`const char*` by default).
  - variable of type `T`: if `T` is *by-value* (below) → `T`; otherwise
    `const T&`.
  - `std::move(x)` → `T` by value (note); `&x` → `T*`.
  - result of another API call → the type that call was inferred to return,
    by value or `const T&` like a variable of that type; a functional cast
    `Foo(3)` → the class it constructs (§7.9, "Propagation and chains").
  - a cast to a standard-library or Qt type (`std::string("x")`, `QString("x")`,
    `std::size_t(3)`, `string("x")` under `using namespace std`) has that type: it
    types the parameter it fills (`const std::string&`) and is evidence for the
    return type of a call compared with it.
  - unknown → policy (D5): `error` (`mt-unknown-param`) or `todo`
    (`config.placeholder_type`, `int` by default, with a TODO).
  - parameter name: the variable's if the argument is an identifier; otherwise
    `arg1…argN`; duplicates → numeric suffix.
- **Return**: the hint with the highest `Origin`; only `Discarded` → `void` +
  warning; `AutoBound`/`Unknown` without another hint → D5 policy. Conflict →
  `mt-conflict`.
- **`const`**: if **any** use is on a `const` access. Without evidence, not
  `const` (nothing is invented; on methods with 0 arguments whose result is
  compared the note `TODO(mt): consider making this const` is added).
- **`static`**: if any use is `X::m()` (§7.7).
- **Constructors**: one ctor per arity used; a default constructor only if it is
  used *and* another ctor is declared (otherwise the implicit one is enough). A
  single parameter → `explicit` unless copy-initialisation is observed
  (`Foo f = 5;`). Copy/move from another `Foo` → implicit, nothing to generate.
- **`using namespace std;`**: a test may write `string` or `vector<string>`, but
  a header must not carry the directive, so every type that reaches the model has
  the names of the standard library's types (`known_names.h`, `is_std_type`)
  prefixed: `std::string`, `std::vector<std::string>`. A name already qualified
  (`std::string`, `other::string`, `::string`) is left alone, and `type_map`
  values are written as the user gave them.
- **Fields**: `obj.x` not called → a public field, type from the context
  (`obj.x = 5;`, comparison). Brace-init with arguments is always treated as a
  constructor (not an aggregate) in the MVP.
- **By-value**: fundamental types (all combinations), `std::size_t`, `size_t`,
  `std::ptrdiff_t`, `std::nullptr_t`, `std::byte`, `(std::)(u)intN_t`,
  `(u)int_fastN_t`, `(u)int_leastN_t`, `(u)intptr_t`, `(u)intmax_t`, `qint*`,
  `quint*`, `qreal`, `qsizetype`, `uchar`, `ushort`, `uint`, `ulong`, pointers,
  known enums, `std::string_view`, `QStringView`, `std::chrono::*`, and
  `config.by_value_types`. Everything else → `const T&`.

Implemented in `frontend/inference.h` (`infer(classified, config, known,
diagnostics)`, a pure function). Details settled while implementing it:

- **Choosing among uses** (parameters, return types, fields, alike). The
  evidence with the highest `Origin` wins; among equals, the first in source
  order. If the others differ, `mt-conflict` lists them all and says which was
  used. "Differ" is judged on the *type*, not its spelling: `unsigned` and
  `unsigned int`, `long int` and `long`, `std::size_t` and `size_t` are one type
  and do not conflict. The winner's literal warnings (`mt-literal-range`, …) are
  reported then, and only then: a literal whose type nobody used says nothing.
- **Return type from a hint**: the type the context implied with the top-level
  `const` and reference removed (`const std::string& s = f();` → `std::string`),
  except a non-const lvalue reference, which stays (`std::string& s = f();` →
  `std::string&`, because a temporary would not bind). A `// @mt: returns T` on
  the very statement replaces that statement's own hint without a conflict
  report: the user has just said what they meant.
- **Nothing says** → by this precedence: a call made on the result
  (`mt-chained-call`), an `auto` variable with no later hint (`mt-auto`), anything
  else (`mt-unknown-return`). Parameters: `mt-unknown-param`. All are errors
  under `auto_policy: error`; under `todo` they are warnings, the type becomes
  `placeholder_type` with `Origin::Defaulted`, and a note says it is a guess.
  The result is `void` (`Origin::Defaulted`, `mt-unused-result`) only when
  **every** use discards it: one use that needs a value outweighs any number of
  discarded ones, and a result passed on as an argument is not "unused".
- **Parameters**: a literal gives its type; an ordinary string literal gives
  `config.string_literal_param`; `nullptr` gives nothing (a pointer of unknown
  type); a named variable gives its declared type without top-level `const` and
  reference — by value if it is a by-value type, else `const T&`; `&x` gives the
  pointer; `std::move(x)` gives `T` by value, with a note; a cast gives its
  target; an array variable gives nothing (it may be a pointer or a reference to
  the array and the text does not say); the result of another call gives its
  type only if that call was annotated (propagating inferred results is task
  2.8). A parameter takes its name from the first call that passes a variable,
  else `argN`; repeated names get a numeric suffix.
- **Constructors**: one per arity used, in order of first use. A one-argument
  construction whose argument is a `Foo` is a copy or move and generates
  nothing. `explicit` for one parameter unless some `Foo f = 5;` was seen. The
  override key for their parameters is `Foo::Foo` (`ns::Foo::Foo`).
- **Methods** are keyed by (name, arity); a call annotated `overload` is keyed by
  its argument types as well, so it can sit next to an unannotated one of the same
  arity. `ignore`d calls contribute nothing. `Operator` for names starting with
  `operator`. A non-const method with no parameters whose result is compared gets
  the note "consider making this const" unless `const` was stated.
- **Exception classes** (plan 7.4c): a `thrown` class has `config.exception_base` as its
  only base (`BaseSpec`, public), inherits its constructors (`Class::inherited_constructors`:
  `throw BadInput("why")` needs one that takes the text), and one note on the class
  (`Class::notes`) saying why it has the base it has and where the test expects it thrown.
  The rest is a class like another: members the test uses are inferred as usual.
- **Enums** (plan 7.4b): an `EnumTarget` becomes an `ir::Enum`: `enum class`, no
  underlying type, one enumerator per name, and for each its notes in
  `enumerator_notes` (the first also says why the name is taken for an enum). The
  name is a type that is by value (like any enum of the file or a header): `void set(Color
  arg1)`, not `const Color&`.
- **Operators** (plan 7.4): a comparison `a == b`, `!=`, `<`, `<=`, `>`, `>=` and an
  assertion that is one (`EXPECT_EQ`/`NE`/`LT`/`LE`/`GT`/`GE`, `ASSERT_*`,
  `QCOMPARE`, `QTRY_COMPARE`, `QCOMPARE_NE` …) is a `MethodCall` fact named
  `operator==` … on the class of `a` (`receiver_of(a)`: an object, or a reference
  to one, not a pointer) with `b` as its only argument and a `Condition` use with
  the hint `bool`. When `a` is a call whose type is not known and `b` is an
  object (`EXPECT_EQ(sensor.position(), expected)`), the operator is `b`'s
  class's: the comparison is what gives `position()` its type, and the same type
  has the operator. `a[i]` is an `operator[]` fact with the indices as its arguments
  (`subscript_expression`), whose result is used like any call's; assigning to it
  (`=`, `+=` …) sets `MethodCall::writes_result`, and `=` also gives it the type
  of the value as a hint. A comparison fact is its expression's `from`, so the
  expression is never claimed by another use. The operators of the standard
  library's types are facts too; classification (§7.7) ignores them as it does
  their methods.
  Inference: the comparison operators are `const` however the test reached them
  (`Config::overrides` can still say otherwise) and, with the hint, return `bool`;
  `operator[]` is `const`, unless some call writes its result, in which case it is
  not `const` and returns a reference to the type it would have (`int&`). The
  rest is a method like another: one per (name, arity), parameters from the
  arguments (`const Point&` for a variable, `int` for a literal).
- **Fields** are public, typed like a return value but always as a value; a class
  with fields and nothing else is emitted as a `struct`.
- **Overrides** (`Classified::overrides`, §7.11) win over inference for the
  attribute they state, including `const: false` from the configuration.
  Looked up by `Class::member`, then by the namespace-qualified key laid over it.
- **`type_map`** is applied last, to types of `Origin::Inferred`/`Defaulted`
  only; its keys are compared as types too (`unsigned` addresses `unsigned int`).
  Pointers, `const` and references around the mapped type are kept.
- **A class used with template arguments** (`Box<int>`) gets `mt-unsupported`
  and a plain class: class templates are not generated in the MVP (§19).
- **`--std`**: `std::optional`, `std::variant`, `std::string_view` and
  `std::byte` in a generated signature warn with `mt-std` when `--std` is below
  17 (§13.3).
- **Notes** (the `TODO(mt):` of §8.2) are written for every type that was
  inferred or defaulted — "return type inferred from literal 3u (t.cpp:12)",
  "parameter 1 of 'Foo::f' inferred from …" — and for none that the user
  declared or annotated.

**Propagation and chains** (task 2.8). Two things need a type that some other
call decided:

- *An argument that is the result of another call* — `bar.set(foo.size())` — takes
  the type `foo.size()` was inferred to return, as a variable of that type would
  (by value, or `const T&`), with the note "inferred from the result of
  'Foo::size'". A call whose result is a guess (a placeholder under
  `auto_policy: todo`, or `void` because it was never used) passes nothing on:
  its own parameter is then unknown, not silently typed. No iteration is needed:
  return types depend only on how results are used, never on parameter types, so
  `infer` works each result out the first time anything asks for it (once) and
  the order of the statements does not matter. A functional cast as an argument
  (`bar.use(Foo(3), QString("x"))`) is the class it constructs, `const Foo&`;
  `Classified::casts` says which free calls are casts.
- *A chained call whose receiver's type has become known* — `repo.find(1).name()`
  once `find` is annotated, compared or assigned to a typed variable — is a method
  of that type. This one does change the classes (`Item` may not be used anywhere
  else) and so what is inferred, hence a loop outside `infer`: `analyse()` runs
  classification and inference, looks at the chain links whose root now has a
  result type, adds a `MethodCall` for each (the receiver being that type, taken
  through `*`/`->`/`&`/smart pointers like any other, `const` included), and runs
  again from the facts the file gave plus those additions. One round resolves one
  link; at most four rounds run, and a chain still resolvable after the fourth
  gets a warning that the rest is not followed. A link whose root has a known type
  that has no members (`int`, `void`) is an `mt-chained-call` error. An external
  receiver (`std::vector<…>`) is resolved too, and generates nothing. Diagnostics
  of the rounds before the last are discarded: the next round may settle what they
  complained about.

### 7.10 Existing headers

Each `#include "…"` of the test is searched for in: the test's directory,
`-I…` (in order), `--out-include` (`core/include_search.h`). If it exists, it is
analysed with the code frontend (one level, no recursion in the MVP): its
classes/functions/enums become **known externals** (typed: they contribute
`Declared` signatures, and a generated header that names one of its types
includes it). If the header found is in `--out-include` — even when `-I` also
leads there, as it must for the test to build — it is an **existing target** →
merge/never (§10, §11), generated again like any other; otherwise a second run
would take its own output for the user's API and generate nothing.

Never looked for and never generated: the framework's and the standard
library's headers, written in quotes or not (`gtest/…`, `gmock/…`, `QtTest`,
`QtCore/…`, anything without an extension such as `vector` or `QString`). An
`#include <…>` is searched on `-I` only (not in the test's directory) and is
never a target: if nothing has it, it is somebody else's. Repeated includes
count once. Several test files in one run are processed one by one; two that
would write the same path with different content are an error (generating one
header from several tests is not supported yet).

### 7.11 Annotations (`// @mt:`)

```
annotation := "// @mt:" directive { ";" directive }
directive  := "returns" TYPE                         (short: applies to the next statement)
            | "overload" | "ignore"                  (short)
            | MEMBER "returns" TYPE                  (long: file scope)
            | MEMBER "param" N TYPE                  (N starts at 1)
            | MEMBER ("const" | "static" | "virtual" | "noexcept")
            | "class" NAME "namespace" NS
            | "class" NAME "header" "\"" PATH "\""
MEMBER     := [NS "::"] CLASS "::" METHOD
```

- Short form: it applies to the next statement (or to the one on the same line
  if the comment comes at the end). `mt-annotation` error if that statement
  contains zero or more than one call of the API.
- `TYPE` = the rest of the text up to `;` or the end, normalised.
- Annotations are `Origin::Annotated`. `config.overrides` accepts the long forms
  with the same meaning.

Implemented in `frontend/annotations.h`. Reading happens with the rest of the
test file (`read_test_file` fills `TestFileFacts::annotations`); applying the
short forms is the last step of `classify()`, because only then is it known
which calls are the code under test. Details settled while implementing it:

- **Which comments.** Line comments whose text starts with `@mt:` after the
  `//` and any spaces. `// @mt returns int` (no colon) and `/* @mt: … */` are
  `mt-annotation` errors rather than ordinary comments: an annotation that is
  silently ignored is worse than none. Anything else, including the text
  `// @mt:` inside a string, is not an annotation.
- **Which statement.** A comment that follows a statement on its own line
  applies to that statement; a comment on a line of its own, to the next
  statement (other comments in between are skipped); a comment in the middle of
  a statement that spans lines, to that statement. At file scope the "statement"
  is the whole next declaration, which will almost always hold more than one
  call and so be an error: use the long form. No statement after it (the end of
  the block) is an error.
- **Which call.** The API calls inside the statement's span are the method,
  static, free and namespaced calls and the field reads that classification
  attached to a class or function to generate (§7.7): `std::max(…)` or `strlen(…)`
  in the same statement do not count, and neither does a call classification
  found to belong to somebody else. A call found by classification itself
  (`Foo(3).size()`) does. A chain link resolved by `analyse()` (§7.9: `name` in
  `repo.find(1).name()` once `find` is known) is a consequence of the call it
  hangs from and does not count, which is what lets one `// @mt: returns Item`
  on that statement mean `find`. Exactly one is required; with several the
  message names them and suggests the long form.
- **What it does.** `returns T` goes to `CallAnnotation::returns`
  (`Origin::Annotated`, `reason = "annotation"`, `where` = the comment, which is
  what the TODO cites), `overload` and `ignore` to flags of the same struct, all
  on the call's fact. The fact's `ResultUse` is left as the code said it:
  inference (§7.9) reads both and the annotation wins. A call already carrying a
  `returns` rejects a second one. `ignore` means "not code under test": the
  inference generates nothing for that call.
- **Long forms** are collected in `Annotations::overrides`, with the shape and
  meaning of `config.overrides`; `classify()` lays them over the configuration's
  own, attribute by attribute (the annotation, being nearer the code, wins), into
  `Classified::overrides`. `param N` takes N from 1 to 9999 without a leading
  zero. Operator members (`Foo::operator==`) are allowed; conversion operators
  with a space (`operator bool`) are not, because the directive is split at
  spaces.
- **`class Foo namespace NS`** decides the namespace of an unqualified class
  (§7.7, rule 2); **`class Foo header "path"`** is kept in
  `ClassTarget::header` for the file assignment of §8.1. Naming a class nobody
  uses is harmless.
- **Repeats.** Each long directive may appear once per member and attribute
  (`Foo::f returns`, `Foo::f param 1`, `Foo::f const`, `class Foo namespace`,
  …); a second one is an error that cites the line of the first, and the first
  stands. A bad directive does not stop the good ones in the same comment.

### 7.12 Qt Test files

Everything of §7.1–7.11 applies; what is particular to Qt Test is this.

- **The tests are slots.** A slot of a local class (`private slots:`, or
  `Q_SLOTS`) whose body is in the file -- inside the class or as `T::a()` -- is
  recorded as `TestCaseInfo{kind: "SLOT", suite: the class, name: the slot}`,
  with the namespace. Qt Test's own `initTestCase`, `init`, `cleanup`,
  `cleanupTestCase` and the `_data` functions are slots too and are read like
  the rest. The class is a *local* class, as a GoogleTest fixture is: never
  generated, its members give the symbol table its types.
- **Evidence** (§7.1): the includes, a slot, or a `QTEST_MAIN`,
  `QTEST_APPLESS_MAIN`, `QTEST_GUILESS_MAIN` or `QTEST_DISABLED_MAIN` at the top
  of the file (`TestFileFacts::qtest_main`). `#include "tst_foo.moc"` is an
  include like any other and is never a header to generate: a name ending in
  `.moc` is foreign (`core::is_foreign_include`).
- **`QFETCH(T, v)`** declares `v` of type `T` in the scope where it stands,
  and `T` is a use of every type it names. It is **not** a construction: the
  macro copies a row of the test's data, so it asks nothing of a default
  constructor. The grammar reads the macro as an `ERROR` node when `T` is a
  builtin type (`QFETCH(int, v)`, `QFETCH(unsigned long long, v)`) and as a call
  otherwise, so both forms are read by text (`syntax::parse_qfetch`); the
  `ERROR` is not an `mt-parse` warning. `QFETCH_GLOBAL` is the same.
- **`QBENCHMARK`** and `QBENCHMARK_ONCE` are blanked by the preprocessing of
  §5.2: the braces that follow are an ordinary block.
- **Template arguments** of a scoped call are uses of types:
  `QTest::addColumn<acme::Foo>("foo")` asks for `acme::Foo` to exist.
- **Not seen**: the types of a `QTest::newRow(...) << a << b` row (the columns
  say them, but nothing connects the two); `&Class::signal` given to a
  `QSignalSpy` or `connect` (a pointer to a member is not a call, so the signal
  is not declared, and a class that has signals must be a `QObject`, which
  `gen-impl` does not generate yet); the `QTEST` column comparison (above).
  Each is a gap in what is generated, never a wrong guess.

### 7.13 Test doubles: fakes and mocks

A test of code that talks to an interface hands it a **double**: a class the file
defines that derives from the interface (`class MockSensor : public Sensor`).
The code under test is written against `Sensor`; a header cannot name
`MockSensor`. Without this section the tool would generate `Controller(MockSensor*)`
and an empty `sensor.h`.

- **A double** is a class of the file (anonymous namespaces included, §6.1) with
  a base class that is neither another class of the file nor part of the
  framework (`testing::Test`, `TestWithParam<T>`, `WithParamInterface<T>`, and
  those names unscoped under `using namespace testing;` or
  `using testing::Test;`). The base it **stands for** is the first such base, as
  the file writes it (`Sensor`, `acme::Sensor`, `std::runtime_error`,
  `QWidget`); a double of one of the file's doubles stands for what that one
  does. A base class is a use of its type (§7.7), so an interface nobody else
  provides is a class to generate. A double with several bases adds the note
  `mt-unsupported`: what it overrides is declared in the first.
  `Classified::doubles` maps every double's name to the base it stands for.
- **The interface**: the functions of a double marked `override` or `final`
  —written out, or by `MOCK_METHOD` and its relatives (§5.2)— are what the class
  it stands for must declare: pure virtual, as the double declares them
  (`ClassTarget::interface_methods`; return type, parameter types and names,
  `const`, `noexcept`; `Declared` origin, so nothing is guessed). One per
  signature, however many doubles say it (name, parameter types and `const`:
  `put(int)` and `put(double)` are two). Not constructors, destructors, statics
  or templates; a function without `override` is the double's own.
- **In the model** (`Inference::build_interface`): the interface's functions
  replace an inferred function of the same name and arity (a call made through
  the interface says less than the double), the others are added, and the class
  gets `virtual ~Foo() = default;` (note: "virtual because the test derives
  'MockSensor' from it"), which a class deleted through a pointer to it needs.
  A class the test also builds itself (`Foo foo;`, `new Foo`, next to a
  `class TestableFoo : public Foo`) cannot be abstract: its functions are
  virtual, not pure, and get stubs like any other.
  Types written with `using namespace std;` are qualified like any other.
  Nothing is generated for a base the file's includes or the configuration
  provide (§7.10), or that is somebody else's (`QWidget`, `std::runtime_error`).
- **Types**: an argument whose type is a double has the type it stands for
  (`Inference::through_double`): `&mock` → `Sensor*`, `fake` → `const Sensor&`,
  `std::move(owner)` → `std::unique_ptr<Sensor>` by value, a template argument
  too (`std::vector<FakeSensor*>` → `std::vector<Sensor*>`; a name is replaced
  whole, so `FakeSensorFactory` is not touched and `t::Fake` goes with its
  qualifier). gmock's wrappers are the mock they wrap: `NiceMock<M>`,
  `NaggyMock<M>` and `StrictMock<M>` (with or without `testing::`) of a double
  are that double; of anything else, they are left as written. Two doubles given
  to the same parameter are one type, not a conflict. The same goes for the type
  a call's result is compared with (`EXPECT_EQ(c.sensor(), &fake)` → `Sensor*`).
- **Where the type comes from**: a variable (`MockSensor m;`,
  `NiceMock<MockSensor> m;`, a fixture's member), `new MockSensor` (→ `Sensor*`)
  and `auto p = std::make_unique<MockSensor>();` (→ `std::unique_ptr<Sensor>`).
  `EXPECT_CALL(mock, f(...))` and `ON_CALL` are macros (R21): they are not read
  as calls of `f`, which the mock declares. What a double does with its
  arguments, and what the code under test does with the interface, are not
  checked: the round trips of §16.3 do, by compiling a real gmock mock against
  the generated interface.

---

## 8. Implementation backend (model → `.h` + `.cpp`)

### 8.1 File assignment

- Headers *to generate* = the test's `#include "…"` that do not exist (§7.10).
- Class ↔ header by normalised name (`match_key`: lowercase, without `_` or `-`;
  the header without directories or extension, through `file_stem`):
  `foo_bar.h` ↔ `FooBar`. `stem` = `snake_case(Class)`: `HTTPServer` →
  `http_server`, `Vec3D` → `vec3_d`, `UTF8String` → `utf8_string` (rules in
  `core/naming.h`).
- Order of preference for a class or function: its annotation
  (`// @mt: class Foo header "acme/foo.h"`), the header whose name matches, the
  first header to generate in source order; if there is none → `naming.header`
  (by default `{stem}.h` with `stem = snake_case(name)`) + an `mt-no-include`
  warning for each.
- **An enum is assigned like a class** (an annotation `// @mt: class Color header "x.h"`, the
  header whose name matches, the first one the test asks for, a file of its own). It is
  declared before the classes of its header, and its namespace's section before theirs, so
  that a class that takes it by value has it already. A header that uses it includes the
  header that holds it, whatever the use: no forward declaration stands in for an enum.
- **Every header the test asks for is generated**, even one nothing was assigned
  to (it holds only the guard and the `includes` region): the test includes it,
  so it has to exist for the test to compile.
- Paths: header at `<out-include>/<path as included>` (with subdirectories:
  `acme/foo.h`); source at `<out-src>/<same directory>/<naming.source>` with
  `stem = file_stem(header)`, so `acme/foo_bar.h` → `acme/foo_bar.cpp` however
  many classes it holds. No source file if nothing in the header is defined
  (a struct of fields, pure virtuals). `--out` sets both directories if they are
  not given separately. The include path is cleaned (`./`, `//`); one that
  leaves the output directory (`../x.h`, `/abs/x.h`) is generated by its file
  name instead, with an `mt-unsupported` warning.
- Namespaces: the contents of a header are grouped in one section per
  namespace, in order of first appearance; each section is nested `namespace`
  blocks (C++11-valid), not indented.

### 8.2 Header

```cpp
// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).
#ifndef ACME_FOO_H
#define ACME_FOO_H

// <mt:begin includes>
// <mt:end includes>

namespace acme {

class Foo {
public:
    // <mt:begin acme::Foo>
    // TODO(mt): parameter 1 of 'acme::Foo::Foo' inferred from literal 3 (tests/foo_test.cpp:11)
    explicit Foo(int arg1);
    // TODO(mt): return type inferred from literal 3u (tests/foo_test.cpp:12)
    unsigned int size() const;
    // TODO(mt): result never used; assumed void (tests/foo_test.cpp:17)
    void run();
    // <mt:end acme::Foo>
};

}  // namespace acme

#endif  // ACME_FOO_H
```

- `config.file_header` (e.g. a corporate header) is prepended; never the
  mirrortest licence.
- Guard (`include_guard`) from the included path: uppercase, anything
  non-alphanumeric → `_`, with runs collapsed and the ends trimmed (a leading
  `_` or a `__` would be reserved identifiers): `../a/foo.h` → `A_FOO_H`; if it
  starts with a digit, the prefix `H_`.
- Includes from the types used (`ir/classify.h`): `std::string`→`<string>`,
  `std::string_view`→`<string_view>`, `std::vector`→`<vector>`, `std::map`,
  `std::set`, `std::unordered_map`, `std::array`, `std::list`, `std::deque`,
  `std::optional`, `std::variant`, `std::tuple`, `std::function`→`<functional>`,
  smart pointers→`<memory>`, `std::pair`→`<utility>`,
  `std::size_t`/`ptrdiff_t`/`nullptr_t`→`<cstddef>`, `(std::)(u)intN_t`→`<cstdint>`,
  `std::chrono::*`→`<chrono>`, `Q[A-Z]\w*`→`<QName>`, `qint64` and the like →
  `<QtGlobal>`; classes generated in another header → `"other.h"`; classes and
  enums of existing headers (`ImplOptions::known`) → that header. Order: system
  ones alphabetically, then local ones alphabetically. The standard library is
  recognised only with its `std::` prefix.
- Member order: constructors (and the destructor), then methods by first
  appearance in the test, then fields. A class that holds a by-value field of
  another class of the same section is emitted after it; a class that names a
  later one in a signature gets it forward-declared (`class B;`) before the
  first class. A cycle of by-value fields is left in source order.
- Two generated headers that include each other cannot work (whichever is
  included first meets a name the other has not declared). The first include a
  cycle runs through, in header order, that only needs the *name* (a parameter,
  a return type, a pointer or a reference: legal with an incomplete type in a
  declaration) becomes a forward declaration (`class B;`, in B's namespace,
  before everything else in the header) and moves to the `.cpp`, which needs the
  definition; this repeats until no cycle is left. Classes held by value cannot
  be forward declared: if that is what keeps a cycle alive, `mt-unsupported`
  warning naming the headers (put the classes in one header). Includes of
  existing headers are never replaced.
- A class with bases has them in its head (`class ParseError : public std::runtime_error {`,
  each with its access and `virtual`), and what it inherits the constructors of as the first
  line of its region, `using std::runtime_error::runtime_error;` (the constructor is named
  by the last name of the base without its template arguments). The header includes the
  headers of the bases (the standard ones from the table above, the generated ones' own):
  a base is held by value. Notes of the class (`Class::notes`) go above its first member.
  A header of classes without definitions has no source.
- An enum (plan 7.4b) is `enum class Color {`, a region labelled by its qualified name
  (`Color`, `acme::Color`) with one enumerator a line (`Red,`, the trailing comma is
  C++11), each below its notes, and `};`. It has no definitions: a header of enums
  has no source.
- Free functions: a `// <mt:begin functions>` region in the namespace; in a
  named namespace the label is `<ns>::functions`, so that two namespaces' regions
  in one header are told apart. A global class called `functions` or `includes`
  would collide with these: `mt-unsupported`.
- A static field also gets its definition (`int Counter::count{};`) in the
  source.

### 8.3 Source (stubs)

```cpp
// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).
#include "acme/foo.h"

// <mt:begin includes>
#include <stdexcept>
// <mt:end includes>

namespace acme {

// <mt:begin acme::Foo>
Foo::Foo(int /*arg1*/) {}

unsigned int Foo::size() const {
    throw std::logic_error("mirrortest: not implemented: acme::Foo::size() const");
}

void Foo::run() {
    throw std::logic_error("mirrortest: not implemented: acme::Foo::run()");
}
// <mt:end acme::Foo>

}  // namespace acme
```

`--stub-policy` policy:

| Policy | Methods (incl. `void`) | Ctor/dtor | Include |
|---|---|---|---|
| `throw` (default) | `throw std::logic_error("mirrortest: not implemented: <signature>")` | empty | `<stdexcept>` |
| `abort` (`--no-exceptions`, `--no-stl`) | `std::abort();` | empty | `<cstdlib>` |
| `default` | `return {};` · `void` → empty · return by reference or a type that `{}` cannot build → `std::abort()` + `mt-unsupported` warning | empty | `<cstdlib>` if needed |
| `hook` | `mt_not_implemented("<signature>");` and then what `default` has: nothing for `void`, `return {};` for a value, `std::abort();` + `mt-unsupported` where `{}` cannot build it (the hook returned and there is nothing to return) | empty | `"mt_not_implemented.h"`, and `<cstdlib>` if needed |

With `default`, `mt-stub-may-pass` is emitted once. Parameter names are always
commented out (R22). Constructors and destructors are empty under every policy:
a constructor that threw would stop every test at its first line with the same
message and hide which methods are missing, and a destructor must not throw.

`return {};` is copy-list-initialisation, so a type qualifies only if `{}`
builds it that way (`ir::is_default_returnable`): fundamental types, enums,
pointers, the standard containers, strings and smart pointers, Qt's value
classes, `pair`/`tuple`/`array` of such types, the names in
`default_constructible_types`, and generated or existing classes with no
constructor declared or with a non-`explicit` default one. Anything else — an
unknown type included — gets the abort and the warning. `--no-rtti`: the generator never emits `typeid`/
`dynamic_cast`; the option remains as a documented guarantee and is passed to
the templates. The `includes` region is emitted even when empty, so that the
merge can add includes.

**The `hook` policy** (plan 8.3) answers R26. With `throw`, QTest reports the first
failure and ends the program, so the slots after it never show which stubs they
need. A `hook` stub calls `mt_not_implemented("<signature>")` and, if that returns,
gives an empty value like `default`. The function is declared in a header of its own,
`mt_not_implemented.h` (`backend/hook_header.h`), that `generate_impl` writes beside
the other headers (`--out-include`, else `--out`) whenever some stub calls it, and
every source includes. It is plain text built in C++, not a template, and nothing to
compile or link: every function is `inline` and the handler is a function-local static.

- `typedef void (*mt_not_implemented_handler)(const char* signature);`,
  `mt_set_not_implemented_handler(handler)` (returns the previous one; `nullptr` puts
  the default back) and `mt_not_implemented(signature)`.
- The default handler **throws** a `std::logic_error` with the same message as the
  `throw` policy, or — under `no_exceptions` or `no_stl`, which have nothing to throw —
  prints the signature with `<cstdio>` and aborts. The stubs are the same in both.
  That is why `effective_stub_policy` leaves `hook` alone.
- The test side installs a handler that makes the running test fail and *returns*
  (`QTest::qFail(signature, __FILE__, __LINE__)`, `ADD_FAILURE()`), before the tests
  run: a static object in a file of the test program is enough
  (`tests/roundtrip/hook/qt_handler.cpp`). Every slot then runs and each reports the
  signatures it is missing. A handler that returns without failing the test lets it
  pass without the code it tests, like `default` — which is why `mt-stub-may-pass`
  is not emitted: the policy does not return a value by itself, the handler decides.
- The header is written like any other file: `.new` beside it if it exists and
  differs (a run with `--no-exceptions` after one without), and left alone by
  `--overwrite merge`, which has no regions to merge in it.

---

## 9. Test planner and backends (model → tests)

### 9.1 Cases

- Per class (or instantiation of a class template, below) with anything public:
  suite `config.naming.suite` (`{Class}Test`) in the class's namespace.
- One case per public constructor (`Constructs`, and if there are several
  `Constructs_<types>`), per public method that is not deleted, and per
  destructor, and per Qt signal (QTest: a `QSignalSpy` skeleton). A deleted
  member (its refusal is asserted, below), a member template (only class templates
  are instantiated, so there are no arguments to call it with) and, under GoogleTest,
  a signal get no case.
- **A class template is tested through the instantiations the configuration
  lists** (`template_instantiations`, plan 7.1; `backend/template_expand`).
  Each entry is the text between the `<>` of one instantiation:
  `"acme::Box": ["int", "std::string, 8"]`. Before planning, every template the
  configuration names (by its qualified name) is replaced, where it stood, by one
  class per entry, called `Box<int>` and `Box<std::string, 8>` (the arguments
  as written, normalised), in which each template parameter is replaced by its
  argument in every method's return and parameter types, default arguments, fields
  and bases. A parameter without an argument takes its default, with the parameters
  before it replaced (`typename A = std::allocator<T>`). The injected class name
  (`Box clone()`) becomes the instantiation, and a name after `::` or `.` is left
  alone, as is a number (`3u`). The instantiation is an ordinary class from there on:
  `Box<int> sut;`, `Box<int>::capacity()`, `Box<int>::Item` for a nested type, and
  `is_default_constructible` finds it by that name.
  - One plan, one file and one suite for each instantiation, named from the
    template and its arguments (`{Class}` is `Box_int`, `{stem}` its
    `snake_case`, `box_int`): `box_int_test.cpp` and `Box_intTest`, `tst_box_int.cpp`
    and `TestBox_int` for Qt Test, so every file keeps one class, one entry point.
  - Reported as `mt-config` warnings on the run, each leaving that instantiation
    out: more arguments than parameters, fewer than the parameters without a
    default, a parameter pack (`typename... Ts`, which has no single argument; the
    template then stays a template). When none of a template's instantiations can
    be made it is skipped as if unlisted.
  - A template the configuration does not name is skipped with an
    `mt-unsupported` warning that says how to list it (`"Box": ["int"]`). A mock is
    not made of an instantiation (§9.4).
  - The test includes the header only; a template has no definitions to link, so
    its members must be in the header. Member templates are skipped as for any
    class.
- **A derived class gets an `Inherits` case** (plan 7.2), the first of its suite:
  one `static_assert(std::is_base_of<Base, Derived>::value, "…")` for each direct
  base, in the order of the base clause, with the base as written (`Counted<int>`,
  `::acme::Base`) and the class by its name (the test is in its namespace), and
  the message naming the class with its namespace. `std::is_base_of` ignores the
  access and virtual-ness of the base, so a `private` or `virtual` base is asserted
  like a public one: the case says *that* the class derives, which is what a
  refactoring of the hierarchy would change. After the assertions a comment says
  that the members the class inherits are tested with the class that declares them:
  the planner lists a class's own declarations only, and repeating a base's members
  in every derived suite would test the same code once for each of them. An
  overriding function is declared in the class and is tested there as any other.
  - It is all `static_assert`: nothing is left to write, so unlike every other case
    it has **no placeholder** and, run, it passes (the compiling is the test). The
    round-trip scripts expect exactly that of a case called `Inherits`.
  - `<type_traits>` is included only when a case needs it. In a fixture suite the
    case is a `TEST_F` like the rest (GoogleTest does not allow a suite to mix
    `TEST` and `TEST_F`), and under `gtest.parameterised` or `qtest.data_driven` it
    has no table. If a method is called `Inherits`, the method takes
    `Inherits_2`.
  - A class with bases and nothing public of its own (`struct Tag : Named {}`) is
    planned for this case alone; it is no longer a class with nothing to test.
  - A base that is a protected or private class nested in another (`Outer::Impl`) is
    no name for a test to write, so it is left out of the assertions (and of the
    comment); with none left there is no case.
  - The bases of an instantiation of a class template are the template's with the
    arguments substituted (`Holder<int>`).
- **A class that deletes its copies or moves has them asserted** (plan 7.5), in cases
  that follow `Inherits` and precede those of the members: `IsNotCopyable` for the copy
  constructor (`T(const T&) = delete`) and the copy assignment (`operator=(const T&) =
  delete`) — one `static_assert(!std::is_copy_constructible<T>::value, "… must not be copy
  constructible")` each, `is_copy_assignable` for the second — and `IsNotMovable` for
  `T(T&&) = delete` and `operator=(T&&) = delete` (`is_move_constructible`,
  `is_move_assignable`). A parameter counts as the class when, as written, it is the end of
  its qualified name from a `::` (`Foo`, `acme::Foo`, `::acme::Foo`; `Tree::Leaf` for a
  nested class, `Box<int>` for an instantiation), and with one parameter only; a deleted
  function that is not a special member (`Foo(int) = delete`, `bool operator==(const Foo&)
  = delete`) is nothing to assert. Access does not matter, for the traits do not see it, so
  a deleted private copy constructor counts. Q_DISABLE_COPY and Q_DISABLE_COPY_MOVE (the
  model does not tell them apart) are the copy constructor and assignment, deleted. A
  defaulted or implicit copy is not asserted: nothing says the author meant it. Like
  `Inherits` they are all `static_assert`: no placeholder, passing; they are planned for a
  class that has nothing else public, and the round-trip scripts expect the three names
  (`Inherits`, `IsNotCopyable`, `IsNotMovable`) to pass. `<type_traits>` is included for
  any of them.
- **A public class nested in another is tested as a class of its own** (plan 7.3;
  `backend/nested_expand`). Before planning, each public class nested in a class of
  the model (and in the public ones nested in those, depth first; also those of an
  instantiation of a template, §9.1 above) becomes a class of the model right after
  the class that holds it, called by its path, `Tree::Node`, with that path's
  holder in `Class::enclosing`. The test is written in the namespace of the outer
  class, where `Tree::Node sut;` is the one name that reaches it, so everything the
  planner names it by follows from the name: the suite `Tree_NodeTest` and
  `tree_node_test.cpp` (`{Class}` is `Tree_Node`, `{stem}` is `tree_node`), the
  `static_assert` of its bases, `Tree::Node::key()` in the target.
  - The types its members mention are written as in the class, where `Node` is the
    sibling and `Order` the enum of the outer class; out of it they are
    `Tree::Node` and `Tree::Order`. So the types the classes around it declare are
    qualified with their path, the nearest class first (it hides the same name
    further out, and a name already qualified, or after `::`, is left alone), in
    return and parameter types, fields and bases, except the names the nested class
    declares itself, which the planner qualifies with the nested class's own name
    (`Tree::Leaf::Item`), as for any class.
  - Not tested: a protected or private nested class (a test cannot name it) and
    everything inside it, and a nested class template (it needs arguments of its
    own; a nested template of an instantiated class is not expanded either). A
    nested class with nothing public to test, and no bases, is **not reported**:
    unlike the header's own classes, a struct of data nested in a class is the
    norm, and a note for each would drown the ones that matter. No mock is
    made of a nested interface (`MockOuter::Iface` is not a name), so an argument
    of that type is not built with one.
- A class with nothing public to test, and no bases, gets an `mt-unsupported` note
  and no plan. Those and the template warnings are the only diagnostics the planner emits: what
  a header cannot give the skeleton is said in the skeleton itself, as a
  `TODO(mt)`.
- **Members under `#if`** (§6.5) are all tested, as if each existed: the
  generated test compiles only in a configuration that declares the member it
  calls. Guarding the test with the same condition is a possible extension; the
  `ifdef_branches` golden documents the current behaviour and has no round
  trip for that reason.
- **Free functions get no tests** (gen-tests covers classes). `gen-tests` says so
  with one `mt-unsupported` note per header, located at the first function and
  counting them; they are an extension for a later phase, not an omission by
  accident.
- One `ir::TestPlan` per class: the file is `config.naming.gtest_file` expanded
  with the class (`{stem}` = its `snake_case`), the suite lives in the class's
  namespace.
- Names (`core/naming.h`): the method's; operators through a table
  (`operator==`→`OperatorEq`, `operator[]`→`OperatorIndex`,
  `operator()`→`OperatorCall`, `operator bool`→`ConversionToBool`,
  `operator const char*`→`ConversionToConstCharPtr`, …; the full table is in
  `test_base_name`). If several cases share a name, the type suffix tells them
  apart (`type_suffix`, without `const`/`&`, `*`→`ptr`): `set_int`,
  `set_std_string`; whatever still collides (`size()` and `size() const`) gets
  `_2`, `_3`… in order, also against names already taken by other methods
  (`unique_test_names`).

### 9.2 Body

```cpp
TEST(FooTest, size) {
    FAIL() << "TODO(mt): write the test for acme::Foo::size() const";

    // Arrange
    Foo sut;
    // Act
    const Foo& const_sut = sut;
    const unsigned int result = const_sut.size();
    (void)result;
    // Assert
    // TODO(mt): compare result with the expected value
}
```

- **Placeholder first** (R19): `FAIL()`/`GTEST_SKIP()`/`QFAIL`/`QSKIP` return;
  the rest compiles against the real API but does not run. GCC and Clang do not
  warn about unreachable code with `-Wall -Wextra`; MSVC does (C4702 at `/W4`):
  a documented limitation, MSVC is not a target.
- `sut`: `Foo sut;` if it is default-constructible by a public, non-deleted
  constructor (`ir::is_default_constructible`: `T name{};` is direct-list
  initialisation, so an `explicit` default constructor does); otherwise the
  public constructor whose parameters are all *constructible* and that has the
  fewest parameters (tie: declaration order), as `Foo sut(a, b);` with a local
  per argument; if none → a commented-out line + TODO. An abstract class has no
  `sut` either (TODO: derive a test double). `static` methods → no `sut`.
- The constructor cases are `Foo sut(args);` + `(void)sut;`; the destructor case
  builds a `sut` like any other and says that it is destroyed when the test ends.
- Arguments: one local per mandatory parameter `T name{};` with `T` = the
  parameter's type without the top-level `const`/`&`; name = the parameter's (or
  `argN`), without collisions. `T&&` → `std::move(name)` (+ `<utility>`).
  Parameters with a default value: omitted (with a comment saying so).
- *Constructible* type: fundamental, enum (also the public ones nested in a
  class: `Foo::Mode`), pointer, known `std::`/Qt value types,
  default-constructible classes of the same model (nested ones included, when
  public), `config.default_constructible_types`. A function pointer, an array or
  `auto` never is.
- If any argument is not constructible, **everything the call needs is emitted
  commented out** (`Stmt::commented_out`) after a `TODO(mt): an argument of
  type 'X' cannot be built here` that names the first such type (without `const`
  or `&`): the `sut`, the `const_sut`, the locals of *all* the arguments and the
  call, with every argument named. Nothing is left half-compiled: a local
  that only the commented-out call used would be an unused-variable warning in a
  file built with the project's own flags. The person writing the test fixes the
  one line that cannot be built and uncomments the block. A `std::move` in a
  commented-out call does not add `<utility>`.
- `const` methods → through `const Foo& const_sut = sut;`.
- `noexcept` → `static_assert(noexcept(<call>), "…")` under `// Assert`
  (commented out with the call if the call is).
- Operators are called by name: `sut.operator==(o)`, `sut.operator[](i)`,
  `const_sut.operator bool()`: it is the one spelling that is valid for all of
  them, and it keeps the case about the member, not about an expression.
- Every `TODO(mt)` about the setup (an argument that cannot be built, a
  default or variadic argument left out, an abstract class) sits under
  `// Arrange`, before the locals it is about, in constructor cases as in method
  cases.
- Result: `const R result = …;` if `R` is a value; `R result = …;` if `R` is a
  reference; `R const result = …;` if it is a pointer (a leading `const` would
  make the pointee const); `auto result = …;` for what cannot be spelled before
  the name (function pointers, arrays); `void` → just the call. Always
  `(void)result;`. A conversion operator's result is the type it converts to.
- Nested types of the class in signatures → qualified `Foo::Item` (the tests are
  already inside the class's namespace).

### 9.3 Includes of the test

The framework (`<gtest/gtest.h>` / `<QtTest>`), the header under test **as the
user would include it**, `<utility>` if `std::move` is used.

How the header is spelt (`cli/gen_tests.cpp`): `--include-as` if given; else the
path below the **first** `-I` directory that contains it (both resolved, so
`./include/`, `src/../include` and an absolute `-I` are the same directory); a
directory that does not contain it, or does not exist, is skipped; with none,
the file's name alone.

### 9.4 GoogleTest

`backend/gtest_backend`: `build_gtest_views` spells the plans and
`generate_gtest` renders `gtest/test_file.inja` (variables in `templates.md`).
Same split as `gen-impl`: C++ decides how every statement is written, the
template only places lines.

```cpp
// Generated by mirrortest from foo.h. Review every TODO(mt).

#include <gtest/gtest.h>
#include "foo.h"

namespace acme {

TEST(FooTest, size) {
    FAIL() << "TODO(mt): write the test for acme::Foo::size() const";
    ...
}

}  // namespace acme
```

- `Placeholder` is `FAIL() << "…";` or, with `placeholder: "skip"`,
  `GTEST_SKIP() << "…";`. A statement marked `commented_out` is `// text;`. The
  messages are escaped as string literals (`"`, `\`, newline, tab).
- Includes: system ones alphabetically, then the local ones, each once. One
  section per namespace, in order of first appearance; the global namespace has no
  braces. Nothing inside a namespace is indented, as in the `gen-impl` output.
- `TEST` by default. With `gtest.fixture: true`, one fixture per class and
  `TEST_F` for every case (GoogleTest does not let a suite mix the two):

  ```cpp
  class AccountTest : public ::testing::Test {
  protected:
      AccountTest() : sut(id) {}

      void SetUp() override {
          // TODO(mt): bring sut to the state every test starts from
      }

      int id{};
      Account sut;
  };

  TEST_F(AccountTest, deposit) {
      FAIL() << "TODO(mt): write the test for Account::deposit(long)";

      // Arrange
      long amount{};
      // Act
      sut.deposit(amount);
      ...
  }
  ```

  - The fixture owns `sut` (planned by `Planner::plan_fixture`, rendered by the
    GoogleTest backend). A class that is default-constructible gets `Foo sut;`;
    otherwise the arguments of the simplest constructor become members **declared
    before** `sut` and the fixture's constructor passes them (`sut(id)`; a member
    cannot be initialised with parentheses in place, and braces would pick an
    `initializer_list` constructor). The body of a case has no `sut` of its own.
  - Names: the members' names are taken out of the bodies' (a parameter called
    `id` becomes `id2` there, since a local of that name would shadow the member),
    and a member does not take the names the bodies introduce (`sut`, `const_sut`,
    `result`, `object`).
  - A constructor case builds **another** object, called `object`, since `sut`
    exists already. The destructor case has nothing to do: the fixture destroys
    `sut` when the test ends.
  - Where `sut` cannot be built (abstract class, no usable constructor) the
    members are comments with the reason, and so is every call on `sut`; the
    cases stay, red, for somebody to fill in.
  - `SetUp()` holds one `TODO(mt)`: it is where the state every test starts from
    goes. `TearDown()` is written only when something needs it, which nothing does
    yet.
  - **It is opt-in because it changes what a red test does.** The plain skeleton
    never touches the class: the placeholder returns before any line of it runs
    (R19). A fixture builds `sut` *before* the test body, so the constructor of the
    class runs (and its destructor) for every test, with default-valued arguments,
    while the test is still red. That is what a fixture is for, and why it is not
    the default for legacy code. Qt Test has no fixture yet; the option is ignored
    there.

  gmock: **mocks of interfaces** (`gtest.mocks`, opt-in; `plan_mocks` and
  `build_gtest_mock_views`, `ir::MockClass`). A class is an *interface* if it has a
  pure virtual function of its own or one that a base **in the model** leaves
  pure: the class's own methods are visited first, then each base's, depth first
  and in the order they are listed, and a signature (name, parameter types,
  `const`) that a closer class has already declared —pure or implemented— is not
  looked at again. A base that is not in the model, or is a class template, may
  hide pure functions the tool cannot see: the mock says so in a note
  (`mt-unsupported` is not raised; it is a `TODO(mt)` on the class). Constructors
  and destructors are not mocked; an operator, a conversion operator and a
  C-style variadic function are pure virtual but gmock cannot mock them
  (`gmock_operator==` is not an identifier), so each is a `TODO(mt)` on the
  mock and the mock stays abstract until somebody declares it. Access is
  ignored (a private pure function must be implemented all the same).
  - One header per interface, `naming.mock_file` (`mock_{stem}.h`), in the
    namespace of the interface, with `gmock/gmock.h` and the interface's header
    (spelled as the tests include it, §9.3), the guard of `guard`
    (`MOCK_SENSOR_H` from the path), and the class `naming.mock_class`
    (`Mock{Class}`) with one `MOCK_METHOD(Ret, name, (Args), (Specs))` for each
    function: `const`, `ref(&)`/`ref(&&)`, `noexcept`, then `override`. Nested
    types are named through the class that declares them (`Sensor::Reading`);
    default arguments are not part of a mock; a type with a comma anywhere in
    its spelling is wrapped in parentheses (`(std::map<int, int>)`), which
    gmock removes (`GMOCK_PP_REMOVE_PARENS`).
  - Not made: for a class template (a note), when the header declares a class
    with the name of the mock already (a note), when nothing is left to mock.
  - The same output name for two interfaces of two headers is the usual refusal
    of §10 (different content under one path).
  - **The mock is what an interface argument is built with** (plan 6.3b).
    Wherever the planner makes the arguments of a call —the constructor under
    `sut`, a constructor case, a method, the fixture's members— a parameter whose
    type is an interface of this header with a mock takes the mock instead of a
    local that cannot be built:
    - `Sensor&`, `const Sensor&` → `testing::NiceMock<MockSensor> sensor;` and
      `sensor`; `Sensor*`, `const Sensor*`, `Sensor* const` → the same and
      `&sensor`;
    - `std::unique_ptr<Sensor>` (by value) →
      `std::unique_ptr<Sensor>(new testing::NiceMock<MockSensor>)`;
      `std::shared_ptr<Sensor>` (by value or `const&`) →
      `std::make_shared<testing::NiceMock<MockSensor>>()`: a temporary, so no
      local;
    - `NiceMock` because an uninteresting call to it is not a warning;
      the mock's name carries the namespace when the interface's differs from the
      class's (`hw::MockSensor`);
    - by value, `&&`, `**`, a container of pointers: as before (a comment).
    The interface is found by the simple name of the type; two interfaces of that
    name in different namespaces: the one in the class's own, else none. A mock
    that has notes (an operator, a base it cannot see) is still abstract and is
    not used. The test file includes the mock headers it uses, by
    `naming.mock_file` as it is (the file sits beside the tests), after the
    header under test; the constructors the plan does not use add none. The
    tests of the interface itself are still comments (an abstract class cannot be
    built).
  - Round trip (§16.3): `tests/roundtrip/gen-tests/<case>.mocks.cpp` is one more
    translation unit that includes the generated mock headers and proves, at
    compile time, that each mock is not abstract and is its interface.
  - **Cases over a table of arguments** (`gtest.parameterised`, plan 6.4;
    GoogleTest 1.10 or later for `INSTANTIATE_TEST_SUITE_P`). A method that
    takes arguments is a `TEST_P`: the arguments are the columns of a
    `ir::DataTable`, the body has a `Stmt::Kind::Data` where it would declare
    them, and the backend writes, in this order and with a blank line between,
    ```cpp
    struct CalcTest_add_Params { int a; int b; };
    class CalcTest_add : public ::testing::TestWithParam<CalcTest_add_Params> {};
    TEST_P(CalcTest_add, add) { ...   // const CalcTest_add_Params& param = GetParam();
                                      // int a = param.a; int b = param.b; ...
    // TODO(mt): one CalcTest_add_Params{...} for each case to run
    INSTANTIATE_TEST_SUITE_P(Cases, CalcTest_add, ::testing::Values(CalcTest_add_Params{}));
    ```
    The names are the suite's and the case's (`{suite}_{case}`,
    `{suite}_{case}_Params`). The single row is value-initialised, so the test
    is as red as a plain one: its placeholder is still the first statement. With
    `gtest.fixture` the class derives from the fixture and from
    `WithParamInterface<Params>` instead of `TestWithParam`, and `sut` is the
    fixture's. The locals the body uses after `Data` are copies of the row's
    fields, named as the arguments, so the rest of the body is the same as the
    plain case's. A name `param` is kept for the row (`param` as an argument
    becomes `param2`) only when the option is on.
    - Which cases: methods (static ones too) with at least one argument
      passed, whose `sut` and arguments can be built. Not constructors, the
      destructor or signals; not a call with an argument that is a mock
      (6.3b), an rvalue reference or a type that cannot be copied (a
      `unique_ptr`, a lock, a class in the header that deletes its copy
      constructor): those keep their locals. (A by-value argument that cannot
      be copied is moved from its local, `std::move(p)`, with or without the
      option.) Default arguments are left out of the table as of the call.
  Entry point: `gtest_main`, no extra code.

### 9.5 QTest

```cpp
#include <QtTest>
#include "acme/foo.h"

namespace acme {

class TestFoo : public QObject {
    Q_OBJECT
private slots:
    void size();
};

void TestFoo::size() {
    QFAIL("TODO(mt): write the test for acme::Foo::size() const");
    ...
}

}  // namespace acme

QTEST_GUILESS_MAIN(acme::TestFoo)
#include "tst_foo.moc"
```

`backend/qtest_backend` (`build_qtest_views`, `generate_qtest`; the template
`qtest/test_file.inja`, variables in `templates.md`), with the statement
spelling it shares with the GoogleTest backend in `backend/test_render`.

- File `tst_{stem}.cpp` (`naming.qtest_file`); class `Test{Class}`
  (`naming.qtest_class`); one class per file. The planner spells both, so the
  GoogleTest patterns are not consulted.
- `#include "<stem>.moc"` at the end, which **must match the file name**
  (AUTOMOC), with a comment saying so in the output itself.
- Main: `QTEST_GUILESS_MAIN` by default (`--qt-main appless|guiless|gui` choose
  `QTEST_APPLESS_MAIN`, `QTEST_GUILESS_MAIN`, `QTEST_MAIN`), naming the class with
  its namespace, since the macro stands outside it. A plan with several classes
  gets one entry point, for the first, and an `mt-unsupported` warning.
- Placeholder: `QFAIL("…")`, or `QSKIP("…")` with `--placeholder skip`. Both
  return from the slot, so what follows is compiled and not run.
- **Signals** are tests here (GoogleTest has nothing to say about them): the
  body builds `sut`, listens with `QSignalSpy spy(&sut, &Foo::changed);
  (void)spy;` and leaves two `TODO(mt)`: make `sut` emit it, check `spy`. A
  signal that shares its name with another member -- an overload, or a slot with
  other parameters -- takes its address through
  `static_cast<void (Foo::*)(int)>(&Foo::changed)`, since `&Foo::changed` alone
  would be ambiguous.
- **Names Qt Test gives a meaning.** A slot called `init`, `cleanup`,
  `initTestCase` or `cleanupTestCase` runs around the tests, and `X_data`
  feeds the slot `X`. A method of the class under test with one of those names
  is tested by a slot with `_call` after it (`init_call`, `size_data_call`; a
  number if that is taken too): otherwise its placeholder would run before every
  other test.
- Includes: `<QtTest>` (which has `QSignalSpy`), the header, `<utility>` if
  needed.
- **Data-driven slots** (`qtest.data_driven`, plan 6.4): the same cases as for
  `gtest.parameterised` (the same `ir::DataTable`) are written as a
  `<name>_data()` slot, declared and defined before the slot, with a column for
  each argument (`QTest::addColumn<int>("a");`), the locals the row is made of
  and `QTest::newRow("default") << a << b;` after a `TODO(mt)`; the slot itself
  reads them with `QFETCH(int, a);` where the body has `Data`. Two types that
  Qt Test cannot take in a column keep the plain slot: one with a comma
  (`QFETCH(std::map<int, int>, m)` is three macro arguments) and `const char*`
  (a `static_assert` of `QTest::addColumn`; `QString` or `QByteArray` are the
  types for it). `QTest::addColumn<std::string>` needs the type to be known to
  `QMetaType`, which Qt 6 does for any complete type and Qt 5 does not.

### 9.6 Qt 5 / Qt 6 differences (`--qt 5|6`, default 6)

It only affects the exception macros (R6). A one-argument `QSKIP("…")` and
`QTEST_*MAIN` are the same in 5.15 and 6.x.

---

## 10. Writing files

Implemented in `core/output.h` (`OutputPlan`: the plan, as data) and
`core/writer.h` (`write_plan`: the only layer with I/O).

- `OutputPlan` = a vector of `{path, content}`. The path is the one the user
  will see (already including the output directory; relative or absolute); the
  writer resolves it against a base directory but **shows it as it is**.
- Action per file, computed against the disk: `Create` (does not exist),
  `Unchanged` (same bytes: **not touched**, not even the mtime), and if it
  differs, depending on `--overwrite`: `never` → `WriteNew` (`<file>.new` next
  to it, the original intact), `force` → `Overwrite`, `merge` → `Merge` through
  the `MergeFn` (phase 3). Without a `MergeFn`, or if it answers "cannot be
  merged", it degrades to `WriteNew` (with an `mt-unsupported` warning in the
  first case). A `MergeResult` identical to the file is `Unchanged`.
- An existing `.new` is overwritten (it is ours). An old `.new` next to a file
  that is now `Unchanged` is left as it is.
- **All or nothing** (D8): first *everything* is decided and validated (empty or
  duplicate paths after normalising, a destination that exists and is not a
  regular file, unreadable…) accumulating all the `mt-io` errors; with any of
  them nothing is written and the report is empty. Then **stage-then-commit**:
  all the `<destination>.mt-tmp` files are written (creating parent
  directories) and only if all of them are on disk is each one `rename`d; a
  failure before the renames deletes the temporaries. A failure *during* the
  renames (practically impossible after the temporaries were written) can leave
  some earlier files in place. When overwriting, the file's permissions are
  kept.
- `--dry-run`: does not write and fills `FileResult::diff` (a unified diff, 3
  lines of context, `--- a/…` / `+++ b/…`) for each changed file: for a
  `WriteNew` it is the diff against the file itself; for a `Merge`, against the
  merged result. Exit 0. `--check`: the same, but the caller does not print and
  exits with 4 if `WriteReport::has_changes()` (any action other than
  `Unchanged`). Combined: it prints and exits with 4. Own diff in
  `core/diff.h`: Myers O(ND) after trimming the common head and tail, with git's
  conventions (`--- /dev/null` for new files, ranges `-3` / `-2,0`,
  `\ No newline at end of file`; lines are compared together with their
  terminator, so adding or removing the final newline is a change). Beyond
  `kMaxEdits` (2000) edits the differing stretch is presented whole as "all the
  old out, all the new in": still a correct diff, not a minimal one.
  Deterministic; verified with a reference patch applier and an LCS over 600
  random pairs.
- Summary (`format_summary`, on stderr unless `--quiet`): `created X`,
  `unchanged X`, `overwrote X`, `wrote X.new (X exists and differs)`,
  `merged N members into X` (`1 member`); with `--dry-run` the changes read
  `would create X`, `would overwrite X`, …

---

## 11. Merge by regions (`--overwrite=merge`)

### 11.1 Regions

Exact lines (after trimming whitespace): `// <mt:begin LABEL>` …
`// <mt:end LABEL>`, with `LABEL` = `includes`, `functions` (`<ns>::functions` in a named namespace) or the
qualified name of the class (`core/regions.h` spells them, for the backends and
the merge alike). Nesting or duplicating regions = `mt-merge-region` error.

### 11.2 Member key

Two different notions, so as not to confuse them:

- **Full signature** = (name, normalised parameter types, `const`,
  ref-qualifier). It identifies a member for real; the code frontend uses it to
  deduplicate (§6.5). `at(int)` and `at(int) const` are two members.
- **Merge key** = (name, arity); for the members of one class (or namespace)
  that the run wants as several overloads *of the same arity* → (name,
  normalised parameter types). The generated text shows which those are, so the
  merge needs no annotation to know (this replaces "if a parameter type is
  `Annotated`": that information does not survive being written out). It
  decides whether an inferred member "already exists" in the user's file.
  Neither the return type (it does not distinguish overloads) nor `const`
  (adding `size() const` next to the user's `size()` would be surprising) is
  part of it (R16); differences in them, in a parameter type or in `static` →
  `mt-merge-mismatch`.

### 11.3 Algorithm (per existing target file)

1. Identical to the desired one → `Unchanged`.
2. Analyse the existing file with the code frontend: the members of each class
   (in the **whole** class, not just in the region) or the `Class::method(…)`
   definitions in the `.cpp`.
3. Missing = desired ones whose key does not exist. Insert their declarations
   (with their TODOs) right before the `<mt:end …>` line, with the marker's
   indentation (and the file's line endings), in the order of the desired model.
   In the `.cpp`, a definition is missing when the header — **as merged**, or as
   it is on disk if it was not — declares a member of that **full signature**
   (qualified name, parameter types, `const`: there both sides are declared
   code) and the `.cpp` defines none; it is inserted with a blank line before it,
   unless the region is empty. A definition whose declaration the user's header
   does not have is not added: it would not compile.
   An enum's members are its enumerators, matched by name (what follows `=` in the user's
   is theirs): the ones the run needs and the enum lacks are inserted with their notes
   before the end marker.
4. An existing member with types different from the inferred ones →
   `mt-merge-mismatch` warning, left untouched.
5. Missing includes (compared as `<x>` / `"x"`, whatever the spacing, and anywhere
   in the file) → the `includes` region; if it does not exist, only a warning.
6. A class (or the free functions of a namespace) without a region, or whose
   region is there but whose class the file does not declare → `WriteNew` +
   `mt-merge-no-region` warning (code outside regions is never edited). A file
   whose regions are malformed is an `mt-merge-region` error and nothing is
   written.
7. Final check: the result is re-parsed; if it has more error nodes than the
   original → `WriteNew` + `mt-merge-broke` warning (a warning and not an error:
   an error would prevent writing the `.new`, D8).
8. Nothing is ever deleted.

---

## 12. Diagnostics

GCC format (understood by IDEs and CI): `file:line:col: error|warning|note:
message [mt-code]`. Sorted by (file, line, column, code). `--strict` turns
warnings into errors.

| Code | Severity | When |
|---|---|---|
| `mt-io` | error | cannot read/write |
| `mt-config` | error · warning | invalid configuration or unknown key · a compilation database that is not one (error), an entry of it that cannot be read (warning) |
| `mt-framework` | error | framework not detectable or ambiguous |
| `mt-parse` | warning | the parser marked regions as `ERROR` (not counting known shapes handled by text, such as `QFETCH(int, v)`) |
| `mt-auto` | error (policy) | `auto` receives a result of the API |
| `mt-unknown-return`, `mt-unknown-param` | error (policy) | type cannot be inferred |
| `mt-chained-call` | error (policy) | call on the result of another call |
| `mt-unused-result` | warning | unused result → `void` |
| `mt-conflict` | warning | incompatible inferences |
| `mt-literal`, `mt-literal-range` | warning | unsupported / out-of-range literal |
| `mt-vexing-parse` | warning | `Foo f();` |
| `mt-ambiguous-scope` | warning / note | `X::m()` without knowing whether `X` is a class or a namespace; a name written with two namespaces; several `using namespace` without `default_namespace` (warning); a single `using namespace` that decides the namespace (note) |
| `mt-unknown-receiver`, `mt-unknown-fixture` | note / warning | receiver or fixture not located |
| `mt-unknown-type` | warning | a type used (e.g. in `EXPECT_THROW`) that is neither generated nor known |
| `mt-no-include` | warning | class or function that no `#include` of the test claims, and no annotation places |
| `mt-annotation` | error | malformed annotation, or one without a single target |
| `mt-unsupported` | warning | construct outside the supported subset; also, from the backend: a header path outside the output directory, headers that hold each other's classes by value, region labels that collide, a `default` stub that cannot return `{}`. An error: `gen-tests --framework qtest` (until task 5.2), or two test files that would write the same file with different content |
| `mt-conditional` | note | member under `#if` with different versions |
| `mt-stub-may-pass` | warning | `default` policy |
| `mt-merge-region` | error | malformed or duplicated regions |
| `mt-merge-no-region`, `mt-merge-mismatch`, `mt-merge-broke` | warning | see §11 |
| `mt-std` | warning | a type that requires a standard newer than `--std` |
| `mt-no-stl` | warning | `no_stl`, and a generated header that has to include a header of the standard library for a type of the tests |

---

## 13. CLI

### 13.1 Syntax

```
mirrortest gen-impl  [--tests] FILE... --out DIR [--out-include DIR] [--out-src DIR]
                     [--framework auto|gtest|qtest] [--stub-policy throw|abort|default|hook]
                     [--no-exceptions] [--no-rtti] [--no-stl] [--auto-policy error|todo] [COMMON]
mirrortest gen-tests [--header] FILE... --out DIR [--framework gtest|qtest]
                     [--placeholder fail|skip] [--include-as PATH] [--qt 5|6]
                     [--qt-main appless|guiless|gui] [COMMON]
COMMON:  -I DIR ... | --std c++11|c++14|c++17|c++20|c++23 (default c++17)
         --overwrite never|merge|force (default never) | --dry-run | --check
         --config FILE.json | --templates DIR | --strict | --quiet | --verbose
         --compile-commands FILE  (adds its -I, -isystem and -iquote directories)
```

Implemented in `cli/run.h` (`parse_options`, `finish_run`, `run`); `main.cpp`
only builds the `Env` and calls `run`.

- **Inputs**: positional or with `--tests`/`--header` (AT §10); the positional
  ones first and then those of the option, each group in the order written.
  Without inputs → usage error.
- **Output**: `gen-impl` needs `--out`, or `--out-include` and `--out-src`;
  `--out-include`/`--out-src` each fall back separately to `--out`
  (`Options::header_dir()` / `source_dir()`). `gen-tests` needs `--out`; it
  writes one file per class, named by `naming.gtest_file`. Two headers that
  would write the same test file with different content (two `Foo`s) are an
  `mt-unsupported` error and nothing is written; the same content twice is one
  file. `--overwrite merge` has no merge function for tests yet, so it degrades
  to a `.new` beside the file with the warning of §10.
- **Repeatable options** (`-I`, `--tests`, `--header`): one value per
  occurrence, all occurrences accumulated. CLI11 makes them greedy by default
  (`-I inc test.cpp` would swallow `test.cpp` as a second directory and leave
  the command without inputs); `expected(1)`, `allow_extra_args(false)` and
  `multi_option_policy(TakeAll)` are all needed at once (none is enough on its
  own, checked against this version). A glued `-Idir` works too.
- **`--compile-commands FILE`** (both commands; `core/compile_commands.h`): a
  compilation database, the `compile_commands.json` CMake writes with
  `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. Only the include directories are taken,
  because mirrortest compiles nothing: it needs to know where the headers a test
  includes are. They behave exactly as if they had been given with `-I`, and come
  **after** the ones given on the command line, so that those are searched first
  (and, for `gen-tests`, the first directory that contains the header decides how
  the test includes it). Parsing the options only stores the path; the file is
  read when the command runs (`with_compile_commands`), so that its problems are
  ordinary diagnostics and `--strict` applies to them.
  - Per entry: `arguments` if present, else `command`, split the way a POSIX shell
    would (`split_command_line`: quotes and backslashes, no expansion; Windows
    quoting is not understood). `-I dir`, `-Idir`, `-isystem dir` and `-iquote dir`
    are read, nothing else (a `-DX=-Iy` is a definition). A relative directory is
    joined to the entry's `directory` and normalised lexically (`..`, `.`, no
    trailing slash); an entry without an absolute `directory` leaves it relative to
    the working directory of the run, which is where `-I` is relative to as well.
  - **Every entry counts**, not only those whose `file` is one of the inputs: a
    header is never in the database, and the test being written is usually not
    either. The union is deduplicated and keeps the order of first appearance. If
    several targets disagree on a directory, give a database filtered to the one
    that matters, or the `-I` yourself.
  - A file that cannot be read is `mt-io` (exit 3); a text that is not JSON, or not
    an array, is an `mt-config` error (exit 1); an entry that cannot be read (not an
    object, no `arguments` or `command`, a `directory` or `command` that is not a
    string, an argument that is not a string) is an `mt-config` warning naming its
    position and is skipped. Nothing is written after an error. `--verbose` prints
    the directories taken (`mirrortest: -I from 'cc.json': a b`).
- **Validation**: values outside their set (`--std c++98`, `--qt 4`,
  `--framework auto` in `gen-tests`, …) are a usage error that says what is
  valid; the sets come from `core/config.h` (`std_names()`, …), a single source
  of truth for the CLI and the file. `--quiet` and `--verbose` are mutually
  exclusive; `--include-as` requires exactly one header. Options of one command
  that belong to the other (`--stub-policy` in `gen-tests`) are a usage error.
- **Precedence** (D9): defaults → `--config` → command line, and **only an
  option actually passed** overrides the file (its default value never does:
  the default `--std` is c++17 and a file saying c++11 must win if `--std` is
  absent). `--no-exceptions`/`--no-rtti`/`--no-stl` can only switch on. Relative paths
  (config, inputs, templates, output) are resolved against `Env::cwd`, never
  with `chdir`, and are **shown as the user wrote them**.
- **`--quiet`**: removes the notes and the summary; warnings and errors are
  always printed. **`--verbose`**: more information (e.g. the templates that were
  replaced).

### 13.2 Exit codes

`0` success (including "nothing to do", `--help` and `--version`) · `1` there are
diagnostic errors that are not I/O ones · `2` incorrect usage · `3` I/O (**all**
the recorded errors are `mt-io`; if they are mixed, `1`) · `4` `--check` with
differences (only if there are no errors).

`finish_run` is the common ending of every command: it applies `--strict`
(warnings → errors), prints the sorted diagnostics on stderr, and with a write
report: `--dry-run` prints each file's diff on stdout; the summary goes to
stderr unless `--quiet` or `--check` (which stays silent, apart from
diagnostics); an error suppresses diffs and summary.

### 13.3 The only things `--std` changes

| Aspect | Effect |
|---|---|
| `u8'a'`, `u8"…"` | `char`/`const char*` up to C++17; `char8_t` from C++20 |
| `std::optional`, `std::variant`, `std::string_view`, `std::byte` in signatures | `mt-std` warning if `--std` < 17 |
| suffixes `z`/`uz` | `mt-std` warning if `--std` < 23 |
| templates | the `std` variable is available (a number: 11, 14, …) |

---

## 14. Configuration (`--config file.json`)

```json
{
  "std": "c++17",
  "default_namespace": "",
  "naming": { "header": "{stem}.h", "source": "{stem}.cpp",
              "gtest_file": "{stem}_test.cpp", "qtest_file": "tst_{stem}.cpp",
              "suite": "{Class}Test", "qtest_class": "Test{Class}",
              "mock_file": "mock_{stem}.h", "mock_class": "Mock{Class}" },
  "gtest": { "fixture": false, "mocks": false, "parameterised": false },
  "qtest": { "data_driven": false },
  "guard": "ifndef",
  "file_header": "",
  "stub_policy": "throw",
  "no_exceptions": false,
  "no_stl": false,
  "no_rtti": false,
  "placeholder": "fail",
  "auto_policy": "error",
  "placeholder_type": "int",
  "string_literal_param": "const char*",
  "exception_base": "",
  "qt": 6,
  "type_map": { "unsigned int": "std::uint32_t" },
  "by_value_types": [],
  "default_constructible_types": [],
  "external_types": [],
  "external_functions": [],
  "ignore_macros": [],
  "overrides": { "acme::Foo::items": { "returns": "std::vector<int>" } },
  "template_instantiations": { "acme::Box": ["int", "std::string"] }
}
```

Implemented in `include/mirrortest/core/config.h` (`Config`,
`apply_config_json`, `load_config_file`). Rules:

- **Layers** (D9): `Config{}` (defaults) → `apply_config_json` (file) → the CLI
  sets the fields of the options it received. Each layer only changes what it
  says; **lists are replaced**, not merged.
- **Strict validation, everything in one pass**: an unknown key (with its path:
  `naming.colour`, `overrides.A::f.retuns`), a wrong type or a value outside its
  set are `mt-config` errors; all of them are reported and, if there is any, the
  `Config` stays **intact** (the work is done on a copy).
- The JSON allows `//` and `/* */` comments. Invalid JSON is an error with the
  position nlohmann/json gives.
- `gtest.fixture` (default `false`): gen-tests writes a fixture class and `TEST_F`
  tests instead of `TEST`s (§9.4). Only the GoogleTest side reads it.
- `gtest.mocks` (default `false`): gen-tests also writes a gmock mock for every
  interface of the header (§9.4). `naming.mock_file` (`{stem}`) and
  `naming.mock_class` (`{Class}`) name them. Only the GoogleTest side reads them.
- `gtest.parameterised` (default `false`): a method with arguments is a `TEST_P`
  over a struct of them (§9.4). `qtest.data_driven` (default `false`): the same
  as a `<name>_data()` slot and `QFETCH` (§9.5). Each framework reads its own.
- `naming.*`: each pattern accepts a single placeholder and must contain it
  (`{stem}` in the file ones, `{Class}` in the name ones); otherwise all the
  classes would go to the same file or suite.
- `exception_base` (default empty): the base of the classes gen-impl generates for the
  types that only appear in `EXPECT_THROW(stmt, E)` (§7.7); empty, they are reported
  (`mt-unknown-type`). A type spelling; stored normalised. Has no effect with `no_exceptions`.
- `no_exceptions` makes `stub_policy: throw` behave as `abort`
  (`effective_stub_policy`); an explicit `default` is respected.
- `no_stl` (`--no-stl`, gen-impl; plan 8.2): the project does not use the standard
  library. It does two things and only two, both about what the generator adds or
  includes by itself: (1) `throw` behaves as `abort` too, because the stub would
  throw a `std::logic_error` (`<stdexcept>`); the source then needs only `<cstdlib>`,
  the C library's, and an explicit `default` is respected; (2) every generated header
  that ends up including a header of the standard library proper — not `<cstddef>`,
  `<cstdint>`, `<cstdio>`, `<cstdlib>` or Qt's — is reported with an `mt-no-stl`
  warning naming the type that asked for it (`ir::is_standard_library_header`; once per
  header and library header). The types themselves are the test's: the header is
  written as the test has it and the warning says where the policy and the test
  disagree, so the fix is in the test (or in `type_map`). It does nothing in
  gen-tests: the tests are built with GoogleTest or Qt Test.
- `file_header` must be a C++ comment (`//` or `/*`), because it is emitted
  verbatim at the top of every file; line breaks are normalised and trailing ones
  are trimmed.
- Types (`type_map`, `by_value_types`, `overrides.*.returns`/`params`,
  `template_instantiations`, …) are stored **normalised** with
  `normalize_spelling`; two `type_map` keys that normalise to the same thing are
  an error.
- `overrides`: key `Class::member` or `ns::Class::member`; value with `returns`,
  `params` (an object `{"1": "int", "3": "double"}`, positions from 1 with no
  leading zeros), `const`, `static`, `virtual`, `noexcept`. Same meaning as the
  long form of the annotation (§7.11).
- `type_map` is applied last, to `Inferred`/`Defaulted` types (never to
  `Declared`/`Annotated`), comparing the normalised `base`.

---

## 15. Templates

Implemented in `core/templates.h` (`TemplateStore`) and
`cmake/MtEmbedFiles.cmake`. User documentation in `docs/templates.md`.

- **Embedded** by default: `templates/**/*.inja` → `mt_embedded_templates.cpp`,
  generated at *configure* time (with `CONFIGURE_DEPENDS`: editing, adding or
  removing a template reconfigures and recompiles by itself; the file is only
  rewritten if its content changes). Each one is a *raw string*
  `R"mt_tpl(…)mt_tpl"`; configure fails naming the file if a template contains
  `)mt_tpl"`. They are normalised to LF when embedded (a `core.autocrlf` on
  Windows must not change the output). Tested in isolation with a mini CMake
  project (`tests/cmake/embed`): content with `;`, `[ ]`, `"`, `\`, `${…}` and
  `@…@` intact, CRLF→LF, ordering by name and no rewriting without changes.
- `--templates DIR`: a file with the same relative path replaces the embedded
  one; the others stay embedded. A `.inja` in the directory that does not
  replace any is a **warning** and is ignored (it is almost always a typo, and
  an override that is silently never applied is worse than an error). A
  nonexistent directory is an `mt-io` error; so is an override that cannot be
  read. `overridden()` lists the ones that were replaced (for `--verbose`).
- inja with `trim_blocks` and `lstrip_blocks`; an undefined variable = an error
  (*verified* in `tests/unit/test_dependencies.cpp`). **Verified peculiarity**:
  with `trim_blocks` inja strips the spaces and tabs after any `%}` in addition
  to the line break (Jinja only the line break), so `{% if x %} of` loses its
  leading space; a space that has to survive goes inside an expression:
  `{% if x %}{{ " of " }}`. `include`s are resolved **only** against the store,
  never against files in the current directory.
- All the templates are parsed the first time anything is rendered (or with
  `validate()`), registered as includable, and **all** the ones that fail to
  parse are reported, each with its name and the line inja gives. While any of
  them is broken none is rendered, and the error is repeated on every call (not
  just the first): it is loud on purpose. Errors in an override point to its
  file; those in an embedded template, to `(built-in) <name>`.
- **Contract**: the template receives a JSON *view model* with everything
  already formatted in C++ (`declaration`, `definition_head`, `body`,
  `notes[]`, `includes[]`, `namespaces[]`…). The template only places things.
  The type is `nlohmann::json`, which is inja's and **sorts the keys of
  objects**; whatever has to come out in a specific order goes in arrays
  (this corrects the mention of `ordered_json` in the first version). The merge
  (§11) reuses those same strings, not the templates. The variables of each
  template are in `docs/templates.md`. The view models are built by
  `backend::build_impl_views` and are what the backend tests assert on; the
  rendering tests compare the text byte for byte.
- Shared template already existing: `common/file_header.inja` (`file_header`,
  `source`) → `[file_header\n]// Generated by mirrortest[ from <source>]. Review
  every TODO(mt).\n`.

---

## 16. Testing strategy for the generator

1. **Unit** (GoogleTest): every module; C++ fragments as strings. The tests
   frontend in two halves (*C++ → facts*, *facts → model*).
2. **Golden** (`tests/golden/`, full contract in `tests/golden/golden.h` and
   `tests/golden/README.md`): `tests/golden/<group>/<case>/` with
   - `args.txt` (one argument per line; `${IN}` and `${OUT}` → the **relative**
     paths `in` and `out`), `input/` (→ `<work>/in`), `existing/` (optional, →
     `<work>/out` before running) and `notes.md` (ignored);
   - the expectations: `expected/` (**only** the tree under `<work>/out`),
     `stdout.txt`, `stderr.txt` and `exit_code.txt` (absent = empty / 0). They
     are kept apart from the expected tree so as not to mix metadata with
     generated files (a change from the first version, which used
     `expected/diagnostics.txt`). The cases that pin the `.new` behaviour carry
     `*.new` files (unblocked in `.gitignore`).
   - The runner executes the real CLI **in process** (`mt::cli::run`) with a new
     temporary directory as `cwd`, so every printed path is the relative one
     from `args.txt` and the goldens do not depend on where the repo is cloned.
     Each case is run **twice** in different directories and the results (code,
     stdout, stderr, tree) must be identical.
   - Conventions checked on the real output: LF, final newline, no trailing
     spaces in files (streams are exempt: a unified diff prints empty context as
     `" "`), and no absolute path (neither the working directory's nor the
     case's).
   - A failure shows the case, the stream or file, and an expected/actual diff.
     A case directory with an unknown entry (almost always a typo: `expeced/`) is
     a layout error.
   - Explicit update: `MT_UPDATE_GOLDEN=1 ctest -R golden` or the
     `update-golden` target; never by hand. It **refuses to record** output that
     is non-deterministic or breaks a convention, and deletes what is no longer
     produced.
   - The runner itself is tested (`tests/golden/test_golden_runner.cpp`) with a
     fake CLI: discovery, layout, each kind of difference, update, determinism
     and conventions. The files under `tests/golden/` are marked `-text` in
     `.gitattributes` so that git does not rewrite their line endings.
3. **Round trip** (CTest + `cmake -P tests/roundtrip/run_case.cmake`):
   - gen-impl (`tests/roundtrip/run_case.cmake`, cases `basic_class`,
     `fixture_members`, `namespaces`, `multi_class_includes`, `no_exceptions`,
     `no_stl`, `no_stl_library_types`, `stub_hook`, `stub_hook_abort`, `mock_interface`, `mock_legacy_macros`, `mock_known_interface`,
     `fake_interface`, whose test and arguments are those of the golden case):
     generate → compile the stubs with `-std=c++11 -Wall -Wextra -Wpedantic
     -Werror` (plus `-fno-exceptions -fno-rtti` for `abort`; the case's
     `input/include` is on `-I`) and the test with
     `-std=c++14 -Wall -Wextra -Werror` (R27) → link against the main build's
     GoogleTest and gmock (compiler command lines, not a nested CMake project: no network
     and a second per case) → run with `--gtest_output=json` → require
     `tests > 0` and **`failures == tests`** (all red; with the `abort` policy
     the process dies, so each test is run alone and must die) → run again with
     `--check` and require exit 0 (idempotence: no file would change).
   - Stubs of **every** gen-impl golden (`tests/roundtrip/syntax_case.cmake`,
     CTest names `stubs.host.<case>`): `-fsyntax-only` with the flags above,
     each source including its own header first, so headers must be
     self-contained.
   - gen-tests (`tests/roundtrip/run_gen_tests_case.cmake`, CTest names
     `roundtrip.gen_tests.<case>`; a case is a `.cpp` in
     `tests/roundtrip/gen-tests/` named after a golden case of `gen-tests`, with an
     optional `<case>.prelude.h` forced into every translation unit for the types
     the header relies on and the tool cannot know): generate from the header of
     the golden case → compile the example implementation with
     `-std=c++11 -Wall -Wextra -Wpedantic -Werror` and each generated test file
     with `-std=c++14 -Wall -Wextra -Werror` (so an unused variable in a skeleton
     is a failure; this is how the destructor case learnt its `(void)sut;`) →
     link, run → `failures == tests` (`fail`) or, with `--placeholder skip`,
     `skipped == tests` and exit 0 → run again with `--check` and require exit 0.
     Not covered: `qt_header_gtest` (needs Qt), `ifdef_branches` (the tests call
     members of both branches, §9.1), `include_as` (names a header that is not
     there), and the cases that write nothing. The implementations matter even
     though a compiler drops the code after `FAIL()`'s `return`: they are what
     makes the declarations in the headers, and so the calls, real.
   - QTest (`tests/roundtrip/run_qt_case.cmake`, CTest names
     `roundtrip.qt.gen_tests.<case>` and `roundtrip.qt.gen_impl.<case>`; if
     `find_package(Qt6|Qt5 COMPONENTS Test)` finds nothing, one test
     `roundtrip.qt` is reported as *skipped* with `SKIP_REGULAR_EXPRESSION`; the CI
     jobs for Qt 6 and Qt 5 run the group with `--no-tests=error`, so a job that
     lost its Qt fails instead of checking nothing). Qt needs moc, which the script
     runs on the Q_OBJECT headers of the case and on the test file, writing the
     `.moc` where the test's own `#include` finds it. Each generated test file is
     its own program; everything is compiled as C++17 with `-fPIC` (Qt 6's
     requirement), the test files with `-Wall -Wextra -Werror`.
     - gen-tests → every slot (`-functions` lists them, but for the `…_data` ones,
       which are not tests) has a `FAIL!  : …::slot()` line —`slot(row)` for a
       data-driven one— and no `PASS   : …::slot()` one, and the program fails; with
       `--placeholder skip`, a `SKIP   :` line each and exit 0. (`Totals:` also
       counts `initTestCase`/`cleanupTestCase` as *passed*, so it cannot be used as
       it is.) The case `qt_special_names` is the proof that a method called
       `init` must not become a slot of that name: without the renaming the
       program's own `initTestCase` fails.
     - gen-impl → R26: after the first exception QTest ends the program, so each
       slot is **run alone** and must fail, with no `PASS` line for it, and the
       whole program must fail. `qt_fetch_types` has no round trip (a data
       function without rows runs no slot) and `qt_exceptions_new` needs Qt 6.3;
       the test of `qt_exceptions` uses a macro that Qt 6.3 deprecates, which is
       allowed to warn. `qt_hook` (`--stub-policy hook`) links a handler that calls
       `QTest::qFail` and returns (`MT_HANDLER`), so nothing ends the program: all the slots
       run, in one program, and every one must have its `FAIL!` line.
     - Not covered: `QTEST_MAIN` (it needs QtWidgets), hence no round trip for
       `qt_gui_main`.
   - **Cross**: if `MT_CROSS_CXX` is defined (CI installs
     `g++-arm-linux-gnueabihf`, a 32-bit compiler), the same syntax checks run with
     the cross compiler (`stubs.cross.<case>`): the generated stubs with
     `-std=c++11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only`, and with the
     `abort` policy also `-fno-exceptions -fno-rtti`.
4. **Corpus of edge cases**: templates, nested namespaces,
   `const`/`static`/`noexcept`, overloads, inheritance, operators,
   `enum class`, smart pointers, Qt macros, `#if`.
5. **Sanitizers** ASan/UBSan in CI on all the tests (already active).
6. CI: GCC and Clang × Debug/RelWithDebInfo with `-Werror`; a sanitizers job; a
   round-trip job with Qt 6 (and Qt 5 once the QTest backend exists); a
   cross-compiler job for a 32-bit target.

---

## 17. Dependencies

| Dependency | Version | Commit | Licence | Use |
|---|---|---|---|---|
| tree-sitter | v0.25.10 | `da6fe9be…` | MIT | parser runtime (ABI 13–15) |
| tree-sitter-cpp | v0.23.4 | `f41e1a04…` | MIT | C++ grammar (ABI 14) |
| nlohmann/json | v3.12.0 | `55f93686…` | MIT | configuration, view models |
| inja | v3.5.0 | `7d1b4600…` | MIT | templates |
| CLI11 | v2.5.0 | `4160d259…` | BSD-3 | CLI |
| GoogleTest | v1.16.0 | `6910c9d9…` | BSD-3 | tests (the last line with C++14, R7) |

Full commits and the mechanism in `cmake/MtDependencies.cmake`. Changing a
version is a reviewed change: tag and commit together, and re-running the
goldens and the round trip. Build requirements: CMake ≥ 3.20, a C++17 and C11
compiler, git.

---

## 18. Risks (updated)

| Risk | Mitigation |
|---|---|
| Wrong type inference | `Origin` + `TODO(mt)`, an error with `auto`, annotations, `type_map` |
| Overwriting user code | `never` by default, merge only inside regions, a re-parse check, all or nothing |
| Macros that break syntactic parsing | blanking with a table + `ignore_macros`; `mt-parse` warning if `ERROR` nodes remain |
| Contradictory `#if` branches | §6.5 |
| ILP32/LP64 differences | §7.5, never `long` by magnitude |
| Generated tests that pass without value | placeholder first; `throw` stub policy |
| Running legacy code with garbage values | placeholder first: the body does not run |
| Drift of GTest/Qt/tree-sitter | versions pinned by commit; `test_syntax_tree.cpp` pins node shapes; goldens |
| Corporate network without access to GitHub | `FETCHCONTENT_SOURCE_DIR_*` with commit verification |
| Unbounded scope | declared subset; the rest `mt-unsupported`, never silence |

## 19. Out of scope (explicit)

Inferring business logic; resolving overloads by semantics; running the real
preprocessor; metaprogramming; generating the user project's `CMakeLists.txt`;
formatting with clang-format (the output already follows a fixed style).
