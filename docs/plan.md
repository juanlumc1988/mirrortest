# mirrortest — implementation plan

Order of work. The *what* and the *why* are in [`design.md`](design.md) (cited
as "§n"); this file only says *in what order* and *when a task is done*. Tick
each task (`- [x]`) when it is finished, in the same commit that implements it.

## Rules for every task

- One task = one commit (or a few), with its tests in the same commit.
- **Definition of done** (all tasks):
  1. `cmake --build build -j && ctest --test-dir build --output-on-failure`
     green with GCC **and** Clang, both with `-DMT_WERROR=ON`.
  2. Clean build with `-fsanitize=address,undefined` (see
     [`development.md`](development.md)).
  3. New unit tests covering the happy path **and** the edge cases listed in
     the task.
  4. If the generated output changes: goldens regenerated through the explicit
     mechanism (never by hand) and the diff reviewed.
  5. `design.md` updated if the implementation forces a decision to change
     (and the decision explained in the commit).
- No `unordered_*` on any path that affects the output. No absolute path or
  timestamp in the output.
- Problems in the input → `Diagnostics`, not exceptions.
- When `design.md` leaves an ambiguity unresolved: choose the most
  conservative option (an explicit error rather than a guess), document it and
  point it out in the summary.

---

## Phase 0 — Scaffolding (done)

- [x] Apache-2.0 licence, `.gitignore`, README, development notes (`docs/development.md`).
- [x] CMake ≥ 3.20, dependencies pinned by tag + verified commit
      (`cmake/MtDependencies.cmake`), offline through `FETCHCONTENT_SOURCE_DIR_*`.
- [x] IR (`ir/model.h`), type spelling (`ir/type.h`, with tests).
- [x] tree-sitter wrapper (`syntax/tree.h`) + tests that pin the node shapes
      found in the spike.
- [x] Minimal CLI (`--version`, subcommands registered) and CI (GCC/Clang ×
      Debug/RelWithDebInfo, ASan+UBSan).

---

## Phase 1 — Common core

- [x] **1.1 Diagnostics** — `core/diagnostics.h/.cpp`. `Severity`,
  `Diagnostic{severity, code, message, loc, notes}`, `Diagnostics` (accumulates,
  `has_errors()`, `promote_warnings()` for `--strict`, `sorted()`,
  `format(std::ostream&)` in the GCC style of §12). Tests: stable order, exact
  format, `--strict`.
- [x] **1.2 Reading sources** — `core/source.h/.cpp`. `SourceFile{path
  (as given), text}`; binary read; CRLF tolerated (the input is not
  normalised); `mt-io` error. Tests with temporary files.
- [x] **1.3 Preprocessing (blanking)** — `syntax/preprocess.h/.cpp`, §5.2
  complete. Tests (each one checking the length and `\n`-position invariant):
  every macro in the table; `slots` is **not** touched outside an access
  position (`int slots = 0;`); macros inside comments, strings, raw strings and
  `#define` lines left alone; multi-line `Q_PROPERTY` keeps its lines; nested
  parentheses and strings containing `)` inside the argument; export macro
  between `class` and the name; BOM; `ignore_macros` as object and function;
  markers with correct offsets. Integration test:
  `tests/unit/data/spike_header.h` parses after blanking with
  `has_error() == false`; `spike_qtest.cpp` leaves `QFETCH(int, value)` as its
  only `ERROR` (a known shape, §7.3: handled by text).
- [x] **1.4 Configuration** — `core/config.h/.cpp`. `Config` with the defaults of
  §14; JSON loading with strict validation (unknown key or wrong type →
  `mt-config` carrying the key's path); defaults < file < CLI merge. Tests:
  defaults, every key, errors.
- [x] **1.5 Naming** — `core/naming.h/.cpp`. `snake_case`, normalisation to
  pair class↔header (§8.1), guards (§8.2), expansion of the `{stem}` and
  `{Class}` patterns; sanitising test-case names (§9.1, operator table).
  Exhaustive tests (acronyms: `HTTPServer` → `http_server`, digits, leading
  `_`).
- [x] **1.6 Diff** — `core/diff.h/.cpp`. Deterministic unified diff, 3 lines of
  context, `--- a/` `+++ b/` headers, a new file against `/dev/null`, handling
  of "no newline at end". Tests with known cases.
- [x] **1.7 OutputPlan + Writer** — `core/output.h`, `core/writer.h/.cpp`, §10
  (no merge yet: `never`, `force`, `.new`, `Unchanged`, `--dry-run`, `--check`,
  atomic, create directories, all-or-nothing). Tests on a temporary directory,
  including "a previous `.new` is overwritten" and "identical → not touched
  (mtime intact)".
- [x] **1.8 Templates** — `cmake/MtEmbedFiles.cmake` (§15; the build fails if a
  template contains `)mt_tpl"`), `core/templates.h/.cpp` (embedded +
  `--templates DIR`, inja with `trim_blocks`/`lstrip_blocks`, clear error with
  the template name). `docs/templates.md` with the variables. Tests: partial
  override, missing template, undefined variable.
- [x] **1.9 CLI as a library** — `cli/run.h/.cpp`: `int run(const
  std::vector<std::string>& args, const Env& env)` with `Env{cwd, out, err}`;
  every option of §13 parsed and validated (CLI11), exit codes of §13.2;
  `--verbose` lists the templates that were replaced
  (`TemplateStore::overridden()`, promised in `docs/templates.md`);
  `apps/cli/main.cpp` only calls `run`. The subcommands still return "not
  implemented" but with the configuration already resolved. Tests for parsing
  and exit codes.
- [x] **1.10 Golden runner** — `tests/golden/runner.cpp` (§16.2): case
  discovery, `${IN}`/`${OUT}` substitution, double run, comparison of tree +
  diagnostics, `MT_UPDATE_GOLDEN=1`, `update-golden` target, global check (LF,
  final newline, no trailing spaces, no absolute paths). One trivial case that
  pins `--version`/wrong usage.

## Phase 2 — gen-impl with GoogleTest (MVP)

- [x] **2.1 Extraction utilities** — `syntax/extract.h/.cpp` (§5.3): type text
  of parameters and return values, decomposition, node search. Tests on the
  spike shapes.
- [x] **2.2 Code frontend, MVP subset** — `frontend/code_frontend` (§6):
  namespaces, classes with a body, methods, ctors/dtor, fields, access,
  `static/const/virtual/=0/override/noexcept/=delete/=default/explicit`, enums,
  free functions. (Needed already for fixtures and existing headers; Qt and
  `#if` in 4.1.) Tests: `tests/unit/data/spike_header.h` (after blanking) →
  expected model, field by field.
- [x] **2.3 Literal classifier** — `frontend/literals` (§7.5) complete. Table
  of tests: every row of §7.5, including `-1`, `-1u`, `0xFFFFFFFF`,
  `4294967296`, `1'000'000`, `5000000000l` (32-bit `long` warning), `1e-9`, `0x1p3`,
  `'ab'`, `u8"x"` with `--std` 17 and 20, `"x"s`, `10ms`, `1.5_km`.
- [x] **2.4 GTest recogniser + symbol table + facts** —
  `frontend/test_frontend`, `frontend/facts` (§7.1–7.4, §7.6). *C++ → facts*
  tests: every row of §7.6; receivers (§7.3) including `->` with smart
  pointers and `(*p).m()`; fixtures (`TEST_F` uses members, fixture methods
  are analysed); `TEST_P` + `GetParam()`; `QFETCH` does not apply yet;
  `ASSERT_NO_THROW({ … })`; `<< "msg"`; most vexing parse; `auto`.
- [x] **2.5 Name classification and namespaces** — (§7.7, §7.8). Tests:
  externals (std, Q*, fixtures, existing headers), `X::m()` in its three
  branches, `using namespace`, free functions vs. macros vs. libc.
- [x] **2.6 Annotations** — `frontend/annotations` (§7.11). Tests: every
  directive, short and long form, errors (`mt-annotation`: no target, more than
  one call, syntax), `config.overrides`.
- [x] **2.7 Inference** — `frontend/inference` (§7.9). *Facts → model* tests:
  unification by arity, `Origin` priorities, deterministic `mt-conflict`,
  `const`, `static`, ctors (default only when needed, `explicit`), fields,
  by-value vs `const&`, parameter names, `auto`/unknown policies (`error` and
  `todo`), `type_map`.
- [x] **2.8 Propagation between calls** — arguments that are the result of
  another call of the API take its inferred type, and chained calls are resolved
  a link per round (at most 4) once the type behind them is known (§7.9,
  `frontend/pipeline`). Tests.
- [x] **2.9 Implementation backend** — `backend/impl_backend` + templates
  `impl/header.inja`, `impl/source.inja` (§8): file assignment, includes by
  type (`ir/classify`), regions, TODOs, stub policies, commented parameter
  names, `file_header`, guards. Unit tests of the view model.
- [x] **2.10 The `gen-impl` subcommand** — full wiring frontend → inference →
  backend → writer (`never`/`force` modes). Goldens (`tests/golden/gen-impl/`):
  1. `basic_class` (ctor with literals, EQ/TRUE/FALSE/STREQ, `void`)
  2. `literals` (every form of 2.3 with its effect on the signature)
  3. `declared_vars` (typed variables as arguments and return values)
  4. `auto_error` (no files, `mt-auto` diagnostic, exit 1)
  5. `auto_todo` (`--auto-policy=todo`)
  6. `const_receiver` (local `const&` and `const` fixture member)
  7. `static_methods`
  8. `fixture_members` (`TEST_F`, `SetUp`)
  9. `namespaces` (`ns::Foo`, `using namespace`, test inside a namespace)
  10. `multi_class_includes` (two includes, a class without a pair, a cycle
      broken by a forward declaration)
  11. `free_functions` (+ macros and libc ignored)
  12. `annotations`
  13. `conflicts`
  14. `unused_result`
  15. `smart_pointers` (`unique_ptr`, `make_unique`, `new`, `->`)
  16. `vexing_parse`
  17. `subdir_include` (`acme/foo.h` → subdirectories in the output)
  18. `existing_never` (a different existing header → `.new`)
  19. `existing_identical` (re-run → `unchanged`)
  20. `existing_known_api` (an existing header on `-I` supplies types and the
      class is not regenerated)
  21. `no_exceptions` (`--no-exceptions` → `std::abort()`, `<cstdlib>`)
  22. `stub_default` (`mt-stub-may-pass` warning)
  Added along the way: `no_include` (nothing claims the classes: files named
  from them and `mt-no-include`), `using_namespace_std`, `fields`, `chains`,
  `qualified_cast`.
- [x] **2.11 gen-impl/GTest round trip** — `tests/roundtrip/run_case.cmake`
  (§16.3; compiler command lines rather than a generic subproject: no network,
  a second per case): cases `basic_class`, `fixture_members`, `namespaces`,
  `multi_class_includes`, `no_exceptions`. Require compilation with the flags of
  §16.3, `failures == tests`, and idempotence. Also `stubs.host.<case>`: the
  stubs of every gen-impl golden compile as strict C++11.
- [x] **2.12 CI: cross compiler and round trip** — a job that installs
  `g++-arm-linux-gnueabihf`, a 32-bit cross compiler, and defines `MT_CROSS_CXX` (stubs with `-std=c++11
  -Wall -Wextra -Wpedantic -Werror -fsyntax-only`; `no_exceptions` additionally
  with `-fno-exceptions -fno-rtti`): `stubs.cross.<case>` for every gen-impl
  golden. The round trips themselves run in the host and sanitizer jobs. (The
  cross job could not be run where this was written, which has no cross
  compiler; the same script runs with GCC and Clang.)
- [x] **2.13 User documentation** — `docs/conventions.md`: how to write tests
  the tool understands (the rules of §7 in user language, with examples),
  annotations, policies, diagnostic codes. README: a real *Usage* section.

## Phase 3 — Merge by regions (brought forward, R15)

- [x] **3.1 Regions** — `core/merge.h/.cpp`: locate, validate
  (`mt-merge-region`), insert before `<mt:end>` with the marker's indentation.
- [x] **3.2 Header and source merge** — §11.3 complete, with the key of §11.2,
  `mt-merge-mismatch`, `mt-merge-no-region`, re-parse check
  (`mt-merge-broke`). Unit tests per step.
- [x] **3.3 `--overwrite=merge`** in the writer + goldens: `merge_new_method`,
  `merge_new_ctor`, `merge_user_changed_type` (no duplicate), `merge_no_region`,
  `merge_includes`, `merge_idempotent` (and `merge_user_implemented`,
  `merge_new_ctor`). Round trip (`tests/roundtrip/run_merge.cmake`): generate →
  "implement" a method (patch in the case) → add a test → merge → it compiles,
  the implemented one passes and the new one fails.

## Phase 4 — gen-tests with GoogleTest

- [x] **4.1 Code frontend, what remains** — Qt roles, `is_qobject` and the
  merging of `#if` branches have been in since 2.2. Missing: `mt-conditional`
  when two branches declare the same (name, arity) with different signatures
  (§6.5), complete nested classes, and the `mt-unsupported` cases of §6.4
  (specialisations, `using Base::Base`, `concept`). Tests.
- [x] **4.2 Planner** — `backend/test_planner` (§9.1–9.3): cases, names, `sut`,
  locals, `const_sut`, `noexcept`, result, commented-out, includes. *Model →
  plan* tests. (Needed `ir::is_default_constructible` and `Class::nested_enums`:
  the planner has to know that `Foo::Mode m{};` and `Foo::Item i{};` are valid.)
- [x] **4.3 GTest backend** — `backend/gtest_backend` + `gtest/test_file.inja`
  (§9.4), `fail`/`skip` placeholders. (`output_path`, `escape_literal` and the
  namespace lines moved to `core/output.h` and `backend/cxx_format.h`, where both
  backends use them.)
- [x] **4.4 The `gen-tests` subcommand** + goldens (`tests/golden/gen-tests/`):
  `simple_class`, `const_noexcept`, `static`, `overloads`, `operators`,
  `non_default_ctor`, `abstract`, `nested_types`, `namespaces`,
  `unknown_arg_type` (commented-out call), `qt_header_gtest`, `ifdef_branches`,
  `template_skipped`, `placeholder_skip`; and, for what the wiring adds,
  `default_constructible`, `cannot_build`, `lifecycle`, `defaults_and_variadic`,
  `rvalue_arguments`, `include_path`, `include_as`, `free_functions`,
  `multiple_headers`, `same_class_twice`, `existing_file`, `nothing_public`.
  (`PlanCollector` now serves both commands for the files of several inputs.)
- [x] **4.5 gen-tests/GTest round trip** — example implementation in the case;
  require compilation and `failures == tests` / `skipped == tests`.

## Phase 5 — QTest in both directions

- [x] **5.1 QTest recogniser** — test class, slots inside/outside the class,
  `QFETCH`, `addColumn`, `QCOMPARE`/`QVERIFY*`/`QTRY_*`, both exception macros
  (reversed order), framework detection (§7.1). Tests. (`gen-impl` accepts Qt
  Test files from here on, since its backend does not depend on the framework;
  the goldens and the compile-and-run round trip are 5.3. The details are §7.12.)
- [x] **5.2 QTest backend** — `backend/qtest_backend` + `qtest/test_file.inja`
  (§9.5–9.6): `.moc`, main, `--qt 5|6`, `QSignalSpy` for signals.
- [x] **5.3 QTest goldens and round trip** — both directions; expectations of
  §16.3 (R26). CI: a job with `qt6-base-dev`; a second job with `qtbase5-dev`.
  (The goldens: 6 for gen-impl, 9 for gen-tests; 13 of them run, against Qt 6.4.2.
  The Qt 5 job is written but has not run: the authoring environment has Qt 6 only.)

## Phase 6 — Fixtures, parameterised tests, mocks

- [x] **6.1** gen-tests with a fixture (`TEST_F`, `sut` member, `SetUp`). Opt-in
  with `gtest.fixture`; GoogleTest only (design 9.4).
- [x] **6.2** gen-impl from `MOCK_METHOD` in the test: generates the interface
  with pure virtual methods (`Declared` signatures). Also hand-written fakes
  (`override`), a virtual destructor, the type a double stands for in the
  parameters that receive it (`NiceMock`/`StrictMock`/smart pointers), and the
  anonymous namespaces of a test file, which were not read (design 7.13, 5.2, 6.1).
- [x] **6.3** gen-tests: gmock mocks for interfaces (all pure virtual
  functions), including the parentheses around types with commas
  (`MOCK_METHOD((std::map<int, int>), …)`). Opt-in with `gtest.mocks`: a mock
  header per interface (design 9.4).
- [x] **6.3b** gen-tests: hand the mock to the class that takes the interface —
  `NiceMock<MockSensor>` for a `Sensor&`, `Sensor*` or smart-pointer argument of
  the constructor under `sut` and of a method call — so a class that needs a
  collaborator is built instead of commented out.
- [x] **6.4** `TEST_P` + `INSTANTIATE_TEST_SUITE_P` in gen-tests (opt-in) and
  QTest `_data`. `gtest.parameterised` and `qtest.data_driven`; a by-value
  argument that cannot be copied is moved (design 9.4, 9.5).

## Phase 7 — Templates, inheritance, nested namespaces, operators

- [x] **7.1** Class templates with `template_instantiations` (gen-tests; design 9.1).
- [x] **7.2** Inheritance: inherited methods are not tested in the derived
  class (a comment in its `Inherits` case); `static_assert(std::is_base_of…)` (design 9.1).
- [x] **7.3** Complete nested classes (gen-tests: the public ones are tested as classes of their own; design 9.1).
- [x] **7.4** gen-impl: operators used in tests (`a == b`, `a < b`, `a[i]`),
  enums inferred from `E::X`, exception classes from `EXPECT_THROW` (opt-in).
  - [x] **7.4a** operators (design 7.9; `operators` golden and round trip).
  - [x] **7.4b** enums from `E::X` (design 7.7, 7.9, 8.1; `enums` golden and round trip).
  - [x] **7.4c** exception classes from `EXPECT_THROW`, opt-in through `exception_base`
    (design 7.7, 7.9, 8.2, 14; `exceptions` golden and round trip).
- [x] **7.5** `= delete` on copy/move → `is_copy_constructible`
  `static_assert`s (gen-tests; design 9.1; `non_copyable` golden and round trip).

## Phase 8 — Advanced configuration

- [x] **8.1** `--compile-commands`: extract `-I` from the file (design 13.1; `core/compile_commands.h`;
  `compile_commands` golden cases for both commands).
- [x] **8.2** `--no-stl` (§8.3): stubs abort instead of throwing a `std::logic_error`, and a generated
  header that includes a library header for a type of the tests is reported (`mt-no-stl`;
  `no_stl` and `no_stl_library_types` golden cases and round trips).
- [x] **8.3** `hook` stub policy (R26): `--stub-policy hook` calls `mt_not_implemented()` from a generated
  header whose handler the test side can replace (design 8.3; `stub_hook`, `stub_hook_abort` and `qt_hook`
  golden cases and round trips).
- [x] **8.4** YAML loader (R18): `--config tool.yaml` is converted to JSON and read by the same reader
  (design 14.1; `MT_WITH_YAML`, yaml-cpp 0.9.0; `config_yaml` and two `cli` golden cases, `requires.txt` in the golden runner).
- [x] **8.5** libclang frontend for headers behind `MT_WITH_LIBCLANG` (D1; design 6.7): `--frontend libclang`, `--clang-arg`;
  the same model as tree-sitter makes (`test_clang_frontend.cpp`, and every `gen-tests`/`gen-impl` golden run again with it:
  `GoldenWithLibclang`), plus what only a compiler knows (`libclang_ifdef`, `libclang_macros`); a CI job.

---

## Phase 9 — Robustness and coverage (defined, not started)

What is left after phase 8 comes from three places: the places where the design says "a later phase"
(§7.6 matchers, §9.1 free functions, §6.5 members under `#if`), the limits that the phase 8 review listed,
and one thing nobody has done yet: run the tool on code that was not written for it. The tasks are ordered
by usefulness for their size and risk, so that the first ones also find the bugs that the later ones would
otherwise meet by accident. Size: **S** under a day of work, **M** a few days, **L** a week. If a task grows
past twice its size, stop at what is solid and say so instead of widening it.

- [x] **9.1** *(M, high)* **Run it on real headers.** `tools/corpus.sh` (a development script, not part of the build
  or of CI: the corpus is other people's code) runs `gen-tests` with both frontends over the public headers of the
  dependencies already fetched into the build tree (CLI11, GoogleTest and GoogleMock, inja, nlohmann/json, yaml-cpp,
  tree-sitter) and a sample of libstdc++'s, compares the models with `mt_dump_model` (`-DMT_BUILD_TOOLS=ON`), and runs
  `gen-impl` over the tests of GoogleTest and GoogleMock. 200 headers and 75 test files, with the address and
  undefined-behaviour sanitizers: no crash, hang or sanitizer report and no `error`. The defects of the two
  readers that it found were fixed, each with a unit test made from the smallest header that showed it; the limits that
  stay are in `design.md` 6.6, and every difference between the frontends is classified in `development.md` ("Running it on code
  that was not written for it").
- [x] **9.2** *(S, high)* **libclang across versions.** The CI job builds and tests against a matrix of the libclang
  versions that Ubuntu 24.04 and 26.04 offer, 14 to 22 (`cmake/MtLibclang.cmake` looks for the same; 21 and 22 are only in 26.04, and a
  `newest` job builds with the GCC 15 and Clang 22 of 26.04, where a missing `#include <algorithm>` showed up). Phase 8
  showed why: a later version than the one tried locally keeps `alignas` in the extent of a declaration. Run locally
  against each of them, it found `extern "C" { }` (an `UnexposedDecl` before 17) and the name of a typedef'd anonymous
  struct or enum (not in the cursor's spelling before 16) read wrong by 14 to 16, a parity snippet that was not valid
  C++ and was read only because 18 keeps an invalid default, and a C++23 test that needs 16: all fixed, and written in
  `design.md` 6.7 with what cannot be made up for.
- [x] **9.3** *(S, medium)* **Types written with parentheses.** `decltype(...)`, `typeof(...)` and `__typeof__(...)` are one
  word for `ir::type_from_spelling` (done while running the corpus, 9.1): `const decltype(x)&` has its base, its `const` and
  its reference; function-pointer, array and array-reference types stay whole. `test_type.cpp` has a row for each form
  (nested parentheses, `decltype(auto)`, the argument inside angle brackets, the GNU spellings) and the parity battery has
  a snippet of them, so both frontends make the same model. The one form the syntax-tree reader cannot read is the GNU
  `typeof`/`__typeof__` as a declaration's type: the grammar has no such type (`design.md` 6.6).
- [x] **9.4** *(M, medium)* **Matchers in `gen-impl`** (§7.6). `EXPECT_THAT(v, m)` gives `v` the type its matcher implies when
  the matcher says it: `Eq`, `Ne`, `Lt`, `Le`, `Gt`, `Ge`, `StrEq`, `StrNe`, `StrCaseEq`, `StrCaseNe`, `HasSubstr`, `StartsWith`,
  `EndsWith`, `IsTrue`, `IsFalse`, `IsNull`, `NotNull`, `DoubleEq`, `FloatEq`, `DoubleNear`, `FloatNear`, and `AllOf`/`AnyOf`/`Not`
  over those. Matchers over containers stay `mt-unknown-return`, now with the matcher named. Golden cases `matchers` (with a
  round trip that compiles the stubs and the test against gmock and requires every test red) and `matchers_unknown`; unit
  tests of the facts; the table is in §7.6.
- [ ] **9.5** *(L, high)* **Free functions in `gen-tests`** (§9.1: "an extension for a later phase"). Today a header's
  free functions get one `mt-unsupported` note. They get tests: one suite per header (the stem of the header, as
  `{stem}` in the naming rules) holding a test per function in its namespace, an overload set as one test per
  overload, `static` and `inline` as they are, `extern "C"` blocks read, templates and operators noted as
  they are for classes. Same planner and backends for GoogleTest and Qt Test, so the work is in the planner (a
  plan without a class) and the naming. Design first: the file name and suite name rules go into §9.1 and §10
  before the code. Done when: golden cases for both frameworks, a round trip that compiles and runs the generated
  tests against the header's implementation, and the `mt-unsupported` note is gone from the free-function path.
- [ ] **9.6** *(M, medium)* **Tests guarded like the members they test** (§6.5). Under the syntax-tree reader every
  member under `#if` is tested, so the generated test compiles only in a configuration that declares it. The model
  keeps the condition of each alternative (the text of the `#if`/`#ifdef` chain, outermost first), and the
  backend wraps the test in the same `#if … #endif`, in both frameworks. The libclang frontend is unaffected (it
  reads the branch that builds). Done when: the `ifdef_branches` golden shows the guards, and has a round trip
  that compiles the generated test in each of the configurations; a member under `#else`/`#elif` is guarded by
  the negation or the chain, never by a guess.
- [x] **9.7** *(S, low)* **YAML merge keys** (`<<: *base`). The converter merges a mapping, or a list of mappings, into the one that
  holds the key; the keys written in the mapping win over the merged ones, and earlier mappings in a list win over later
  ones, as in YAML 1.1. A merge key whose value is not a mapping is an `mt-config` error with line and column. The reader of
  the JSON file is not touched. Unit tests for each rule, and the `config_yaml` golden case uses one (`design.md` 14).
- [ ] **9.8** *(S, medium)* **Installing it.** `install()` rules for the binary, the licence and the user
  documentation, a `cmake --install` check in CI that runs the installed `mirrortest --version`, and a CPack
  tarball (`-G TGZ`) built by a CI job without publishing it. Done when: the install tree has no path from the
  build tree in it, and `development.md` says how to install it.

### Left out of phase 9, and why

- **Publishing releases or binaries.** It acts outside the repository (a release page, an artifact someone
  downloads); the owner decides if and where, and 9.8 only makes it possible.
- **`mt_not_implemented.h` under `--overwrite merge`.** A file the test side is meant to edit (it installs the handler
  there) is never overwritten: a changed one gets a `.new`. That is the safe behaviour, not a missing feature.
- **The two limits of the syntax-tree reader** that libclang does not have (a comment inside a fundamental type,
  an abstract declarator in a trailing return type). They are rare, `--frontend libclang` reads them right, and 9.1
  will say if the corpus shows them often enough to change that.
- **Windows and macOS builds.** Nothing in the project needs them yet, and each is a CI job to keep green.
- **Matchers over containers** (9.4) and **templates and operators as free functions** (9.5): they need
  decisions about which instantiation to test, which the design leaves to the configuration (`template_instantiations`, §9.1).
