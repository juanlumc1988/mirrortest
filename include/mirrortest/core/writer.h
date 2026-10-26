// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The only layer that writes files. docs/design.md, section 10 and D7/D8.
//
// What happens to each file in the plan depends on what is already there:
//
//   not there                      Create
//   there, same bytes              Unchanged   (not touched: the mtime stays, so
//                                               build systems do not rebuild)
//   there, different bytes         by --overwrite:
//       never   (the default)        WriteNew    <file>.new next to it, original untouched
//       force                        Overwrite
//       merge                        Merge       through the MergeFn (phase 3), else WriteNew
//
// The run is all or nothing. Every decision is made, and every error found,
// before the first byte is written. Then every file is staged as
// `<target>.mt-tmp` and only when all of them are on disk are they renamed
// into place, so an I/O failure cannot leave a half-generated tree behind.
// (A failure during the final renames can still leave earlier files in place;
// renames of files that were just written essentially do not fail.)
//
// A `<file>.new` is the tool's own output and is simply replaced the next
// time. A stale `.new` next to a file that is now Unchanged is left alone.

#ifndef MT_CORE_WRITER_H
#define MT_CORE_WRITER_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"

namespace mt::core {

enum class OverwriteMode { Never, Merge, Force };
const char* to_string(OverwriteMode mode);                       // "never", "merge", "force"
std::optional<OverwriteMode> parse_overwrite_mode(std::string_view text);

enum class WriteAction { Create, Unchanged, Overwrite, WriteNew, Merge };

// The outcome of merging generated code into an existing file (phase 3).
struct MergeResult {
    std::string content;          // the merged file
    std::size_t added_members = 0;
};

// Merges `generated` into `existing`. nullopt means "cannot be merged safely"
// (no marked region, a region that does not parse, ...): the file then gets a
// `.new` instead. The function reports why through the Diagnostics.
using MergeFn = std::function<std::optional<MergeResult>(const OutputFile& generated,
                                                         const std::string& existing,
                                                         Diagnostics& diagnostics)>;

struct WriterOptions {
    OverwriteMode mode = OverwriteMode::Never;
    bool dry_run = false;   // compute and show diffs, write nothing
    bool check = false;     // write nothing; the caller exits non-zero if anything would change
    MergeFn merge;          // required for mode == Merge; without it Merge degrades to WriteNew
};

struct FileResult {
    std::string path;             // as in the plan
    WriteAction action = WriteAction::Create;
    std::string target;           // what was (or would be) written: `path`, or `path`.new for WriteNew
    std::size_t added_members = 0;   // Merge only
    std::string diff;             // unified diff against what is on disk; only set for dry_run / check
};

struct WriteReport {
    std::vector<FileResult> files;   // in plan order

    // Anything that is not Unchanged: what --check turns into exit code 4.
    bool has_changes() const;
};

// Applies `plan`, resolving relative paths against `base_dir`.
//
// Returns false if any error was recorded, in which case nothing was written
// and `report` is empty. With dry_run or check set nothing is ever written,
// and `report` holds what would have happened.
bool write_plan(const OutputPlan& plan, const std::filesystem::path& base_dir,
                const WriterOptions& options, WriteReport& report, Diagnostics& diagnostics);

// One line per file, ending in '\n':
//   created X / unchanged X / overwrote X / wrote X.new (X exists and differs)
//   / merged N members into X
// With `dry_run`, changes read "would create X", "would overwrite X", ...
std::string format_summary(const WriteReport& report, bool dry_run);

}  // namespace mt::core

#endif  // MT_CORE_WRITER_H
