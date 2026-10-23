// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Reading input files. The one place that turns a path into text.
//
// The path is kept exactly as the user gave it -- never made absolute -- so
// diagnostics and generated "from tests/foo_test.cpp" comments are identical
// whatever directory the tool runs from.
//
// The text is the file's bytes, untouched: CRLF stays CRLF, a UTF-8 BOM stays
// a BOM. Normalising here would shift every offset the parser reports, and
// the preprocessing pass (syntax/preprocess.h) is written to cope with both.

#ifndef MT_CORE_SOURCE_H
#define MT_CORE_SOURCE_H

#include <filesystem>
#include <optional>
#include <string>

#include "mirrortest/core/diagnostics.h"

namespace mt::core {

struct SourceFile {
    std::string path;   // as given on the command line
    std::string text;   // raw bytes
};

// Reads `path`. A relative path is resolved against `base_dir` when one is
// given (the CLI passes its working directory instead of changing the
// process's), but `SourceFile::path` and every diagnostic keep the path as the
// user wrote it.
//
// On failure records an `mt-io` error, located at the file, and returns
// nullopt. The message deliberately avoids strerror(): its wording differs
// between platforms and diagnostics are compared byte for byte.
std::optional<SourceFile> read_source(const std::string& path, Diagnostics& diagnostics,
                                      const std::filesystem::path& base_dir = {});

}  // namespace mt::core

#endif  // MT_CORE_SOURCE_H
