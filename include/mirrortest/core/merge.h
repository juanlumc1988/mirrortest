// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Regions: the parts of a generated file that mirrortest owns and may add to
// (docs/design.md, section 11). `--overwrite=merge` never rewrites a file; it
// only inserts, and only before the `// <mt:end LABEL>` line of a region the
// tool itself wrote. Everything outside the markers is the user's.
//
// This module is text and nothing else -- finding the markers, checking that
// they are sane, inserting lines at the right place with the right indentation
// and line endings. Deciding *what* to insert is backend/impl_merge.h.
//
// A marker is a whole line (leading and trailing whitespace ignored):
//
//     // <mt:begin acme::Foo>
//     // <mt:end acme::Foo>
//
// A `// <mt:begin x>` after code on the same line is a comment, not a marker.

#ifndef MT_CORE_MERGE_H
#define MT_CORE_MERGE_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/diagnostics.h"

namespace mt::core {

struct Region {
    std::string label;
    std::size_t begin = 0;   // index of the line with the begin marker
    std::size_t end = 0;     // index of the line with the end marker
    std::string indent;      // the end marker's leading whitespace: what inserted lines are indented with
};

// The lines of `text`, each with its terminator ("\n" or "\r\n"); the last one
// has none if the text does not end in a newline. join(split_lines(t)) == t.
std::vector<std::string> split_lines(const std::string& text);

// The line terminator `text` uses: that of its first line, "\n" if it has none.
std::string line_ending_of(const std::string& text);

// Every region of `text`, in order. Anything malformed is an `mt-merge-region`
// error at the line in question (all of them are reported, `file` as given by
// the user) and the result is nullopt: nested regions, an end with no begin or
// naming another label, a begin that is never closed, the same label twice.
std::optional<std::vector<Region>> find_regions(const std::string& text, const std::string& file,
                                                Diagnostics& diagnostics);

// The first region with this label, or null.
const Region* find_region(const std::vector<Region>& regions, const std::string& label);

// The lines strictly between the markers, without terminators.
std::vector<std::string> region_body(const std::string& text, const Region& region);

// `text` with `lines` (no terminators; an empty one stays empty) inserted just
// before the end marker of `region`, indented like the marker and ended like
// the rest of the text. Nothing else moves.
std::string insert_before_end(const std::string& text, const Region& region, const std::vector<std::string>& lines);

}  // namespace mt::core

#endif  // MT_CORE_MERGE_H
