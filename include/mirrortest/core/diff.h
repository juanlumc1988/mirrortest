// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Unified diffs, for --dry-run and --check (design section 10).
//
// Written here rather than shelled out to diff(1) because the output is
// compared byte for byte in the golden tests and must be the same on every
// machine: no dependency on which diff is installed, on locale, or on the
// current directory. It follows git's conventions so people can read it
// without learning anything:
//
//   --- a/src/foo.h              or  --- /dev/null  for a file that is new
//   +++ b/src/foo.h
//   @@ -3,7 +3,8 @@               a range of one line is "-3", an empty one "-2,0"
//   \ No newline at end of file  after a last line that has no line break
//
// Lines are compared with their terminator, so adding or removing the final
// newline is a change, and CRLF files diff as CRLF.
//
// The edit script is Myers' O(ND) algorithm, which finds a minimal one, after
// trimming the common head and tail. Memory is O(D^2), so a pathological
// input (thousands of lines changed) is capped: past kMaxEdits the differing
// middle is reported as "all old lines removed, all new lines added". That is
// still a correct diff -- applying it gives the right file -- just not the
// smallest, and generated files essentially never get there.

#ifndef MT_CORE_DIFF_H
#define MT_CORE_DIFF_H

#include <optional>
#include <string>
#include <string_view>

namespace mt::core {

// How many edited lines Myers is allowed before the fallback applies.
inline constexpr unsigned kMaxEdits = 2000;

// The diff that turns `old_text` into `new_text` for the file `path`.
//
// `old_text` is nullopt when the file does not exist yet; the header then
// reads "--- /dev/null". Returns "" when there is nothing to show: the
// contents are identical. (A new, empty file has no hunks but still gets its
// two header lines, since it is a change.)
//
// `path` is printed as given; the caller decides whether it is relative to
// the output directory. `context` is the number of unchanged lines shown
// around each change.
std::string unified_diff(std::string_view path, const std::optional<std::string>& old_text,
                         std::string_view new_text, unsigned context = 3);

}  // namespace mt::core

#endif  // MT_CORE_DIFF_H
