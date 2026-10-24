// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Which of a test's `#include "..."` are code to generate, and which are code
// that already exists (docs/design.md, section 7.10).
//
// A test written first includes headers that do not exist yet: those are the
// ones gen-impl writes. The others declare the API the test uses and must not
// be generated again. The search order is the compiler's, then the output
// directory's: the test's own directory, the `-I` directories in order, and
// finally `--out-include`, where an earlier run left its headers.
//
// A header that is found in the output directory (even when `-I` points
// there too, as it must for the test to build) is an existing *target*, not
// an existing API: it is generated again and the writer decides what becomes
// of the differences. Otherwise a second run would find its own output, take
// it for the user's, and generate nothing.
//
// This is the one place besides the writer and the source reader that looks at
// the file system.

#ifndef MT_CORE_INCLUDE_SEARCH_H
#define MT_CORE_INCLUDE_SEARCH_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/ir/model.h"

namespace mt::core {

enum class IncludeKind {
    Foreign,   // a framework, standard-library or Qt header: neither generated nor read
    Known,     // exists next to the test or on -I: its declarations are the API in use
    Target,    // does not exist, or is the output directory's own: gen-impl writes it
};

struct ResolvedInclude {
    std::string           path;   // exactly as the test spells it: "acme/foo.h"
    IncludeKind           kind = IncludeKind::Target;
    std::filesystem::path file;   // where it was found; empty if it was not
    std::string           shown;  // the same place the way the user would write it: "include/acme/foo.h"
    ir::SourceLoc         where;
};

// A directory to look in, and how to call it in messages. `dir` is what is
// opened; `shown` is what the user wrote (a relative path stays relative, so
// diagnostics are the same wherever the tool runs from).
struct SearchDir {
    std::filesystem::path dir;
    std::string           shown;
};

struct IncludeSearch {
    SearchDir              test_dir;      // the directory of the test file
    std::vector<SearchDir> include_dirs;  // -I, in order
    SearchDir              out_include;   // --out-include, or empty
};

// `gtest/gtest.h`, `gmock/gmock.h`, `QtTest`, `QString`, `vector`: includes
// that belong to a framework, to Qt or to the standard library. A quoted
// include of one of them must not be mistaken for a header to generate.
bool is_foreign_include(std::string_view path);

// The path of `file` below `dir`, spelt with '/', if it is below it: the way a
// header is included from an `-I` directory. Purely lexical -- the caller makes
// both absolute (and resolves symlinks) first. Not below it, the same place as
// `dir`, or paths that cannot be compared (one absolute, one not) give nothing.
//
//   ("/p/include/acme/foo.h", "/p/include")  -> "acme/foo.h"
//   ("/p/include/acme/foo.h", "/p/other")    -> nothing
std::optional<std::string> path_below(const std::filesystem::path& file, const std::filesystem::path& dir);

// Sorts the includes of a test. System includes (`<...>`) are looked up like
// the others but are never targets: if nothing has them they are somebody
// else's. Repeats are dropped, order is kept.
std::vector<ResolvedInclude> resolve_includes(const std::vector<ir::Include>& includes, const IncludeSearch& search);

}  // namespace mt::core

#endif  // MT_CORE_INCLUDE_SEARCH_H
