// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The libclang frontend (docs/design.md, section 6.7; plan 8.5): the same job as
// frontend/code_frontend.h -- C++ declarations in, ir::Model out -- done by asking a
// compiler front end instead of a syntactic parser.
//
// Why have both. tree-sitter (D1) needs no toolchain and tolerates code that does not
// compile, which is what a test written before its implementation is. A header, on the
// other hand, is code that compiles, and a compiler knows what a syntax tree cannot:
// which branch of an #if is the one that builds, what a macro expands to (an export
// macro needs no `ignore_macros`), what a `using` alias names. The price is a libclang
// that matches the compiler, the project's include directories and the standard the
// header is written in -- and a header that does not compile gets partial or wrong
// types, which is why it is reported (mt-parse) and why the tree-sitter reader stays the
// default (`--frontend libclang` chooses this one).
//
// What it reads is what the tree-sitter reader reads, into the same model: namespaces,
// classes and structs with their members, bases, nested types, class templates (as the
// generator expands them), enums, free functions and #include directives. Types are
// spelled by libclang, so they follow its printing policy (a type is as qualified as the
// libclang version prints it), then normalised like any other spelling.
//
// The Qt markers the preprocessing pass leaves (Q_OBJECT, signals and slots sections,
// Q_INVOKABLE) are applied by offset exactly as the other reader does, because libclang is
// given the same blanked text. Qt's own headers are only needed to resolve QObject and the
// value classes: without them those types are unknown and reported, like any other.

#ifndef MT_FRONTEND_CLANG_FRONTEND_H
#define MT_FRONTEND_CLANG_FRONTEND_H

#include <string>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/source.h"
#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/preprocess.h"

namespace mt::frontend {

// Whether this build has the libclang frontend (MT_WITH_LIBCLANG).
bool libclang_supported();

// The major version of the libclang this program runs with (18 for "clang version 18.1.3"), or 0 when the build has none
// or its version cannot be read. What libclang reads differs between versions, and a test of the behaviour of one of them
// needs to know which it is.
int libclang_major_version();

struct ClangOptions {
    // Passed to libclang as the compiler's command line, in this order, after `-x c++`:
    // -I and -D, -std=..., whatever `clang_args` in the configuration says. Paths in them
    // are absolute or relative to the process's working directory (libclang's), so the
    // caller makes them absolute.
    std::vector<std::string> arguments;

    // What libclang opens: `source.path` made absolute against the run's working directory,
    // so that an #include "next_to_it.h" is found whatever the process's directory is. Empty:
    // `source.path` as it is.
    std::string absolute_path;
};

// Preprocesses (Qt macros), asks libclang to parse `source` as C++, and reads the
// declarations that are in that file (not those of the headers it includes) into a model.
// The text is given to libclang from memory, so the file need not exist.
//
// A parse error libclang recovers from is an `mt-parse` warning at its location (at most ten,
// then a note with the count); a header libclang cannot open at all is an `mt-parse` error
// and an empty model. Without MT_WITH_LIBCLANG this records an error that says so.
ir::Model read_header_clang(const core::SourceFile& source, const std::string& include_path,
                            const syntax::MacroTable& macros, const ClangOptions& options,
                            core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_CLANG_FRONTEND_H
