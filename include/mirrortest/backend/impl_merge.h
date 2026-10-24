// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// `--overwrite=merge` for gen-impl (docs/design.md, section 11): what a new run
// adds to the headers and sources an earlier run wrote, without touching a
// line of the user's.
//
// The unit of work is a file the writer found already there and different from
// what the run generates. The merge parses both with the code frontend, finds
// the members the run wants that the existing file lacks, and inserts them --
// declaration and TODO comments in the headers, definition in the sources --
// just before the end marker of the class's region (core/merge.h). Nothing is
// ever removed or edited. Anything the merge cannot do safely it refuses, and
// the writer falls back to a `.new` file beside the original:
//
//   * a class (or the free functions) whose region the existing file lacks;
//   * a result that parses worse than the original did (`mt-merge-broke`).
//
// Members are matched by (name, arity); if the run wants overloads of one
// arity they are told apart by their parameter types. A member that exists but
// with other types (`int size() const` where the tests say `unsigned int
// size()`) is left alone with an `mt-merge-mismatch` warning: the user decided.
// A definition is added to a source only if the header, as merged, declares it.

#ifndef MT_BACKEND_IMPL_MERGE_H
#define MT_BACKEND_IMPL_MERGE_H

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"
#include "mirrortest/core/writer.h"

namespace mt::backend {

class ImplMerger {
public:
    // Reads a file by the path it has in the plan; nullopt if there is none. Used
    // to find the header a source goes with when that header was not merged.
    using Reader = std::function<std::optional<std::string>(const std::string& path)>;

    // `out_include` is the directory the headers are written under, as in
    // ImplOptions: a source's `#include "acme/foo.h"` names <out_include>/acme/foo.h.
    ImplMerger(std::string out_include, Reader reader);

    // The merge function for core::WriterOptions. The merger must outlive the
    // write, and the plan must list a header before its source (gen-impl does).
    core::MergeFn function();

    // One file. Public so the tests can drive it directly.
    std::optional<core::MergeResult> merge(const core::OutputFile& generated, const std::string& existing,
                                           core::Diagnostics& diagnostics);

private:
    std::string                        out_include_;
    Reader                             reader_;
    std::map<std::string, std::string> merged_headers_;   // output path -> text after merging
};

}  // namespace mt::backend

#endif  // MT_BACKEND_IMPL_MERGE_H
