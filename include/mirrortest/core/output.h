// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What a run wants on disk, as data. Everything before the writer is a pure
// function of its input and produces one of these; only core/writer.h touches
// the file system (design section 3, "Pure core, I/O at the edges"). That
// split is what makes --dry-run, --check and the golden tests fall out for
// free: they just look at the plan instead of applying it.

#ifndef MT_CORE_OUTPUT_H
#define MT_CORE_OUTPUT_H

#include <string>
#include <vector>

namespace mt::core {

struct OutputFile {
    // Where the file goes, exactly as it should be shown to the user: the
    // output directory already joined in, relative to the working directory
    // unless the user gave an absolute one. The writer resolves a relative
    // path against its base directory but always prints it as given.
    std::string path;
    std::string content;   // the bytes, line endings included (always LF when generated)
};

struct OutputPlan {
    std::vector<OutputFile> files;   // in the order they are reported
};

// Where a generated file lands: `dir` joined with the path below it, "" and "."
// meaning the working directory ("out" + "acme/foo.h" -> "out/acme/foo.h"). Text
// only: the path is shown to the user as built, never resolved.
inline std::string output_path(const std::string& dir, const std::string& relative) {
    if (dir.empty() || dir == ".") return relative;
    return dir.back() == '/' ? dir + relative : dir + "/" + relative;
}

}  // namespace mt::core

#endif  // MT_CORE_OUTPUT_H
