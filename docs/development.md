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
