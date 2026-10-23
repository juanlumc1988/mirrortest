// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/source.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mt::core {

namespace {

void fail(const std::string& path, const char* why, Diagnostics& diagnostics) {
    ir::SourceLoc where;
    where.file = path;
    diagnostics.error(code::io, std::string("cannot read file: ") + why, where);
}

}  // namespace

std::optional<SourceFile> read_source(const std::string& path, Diagnostics& diagnostics) {
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::file_status status = fs::status(fs::path(path), ec);
    if (ec || !fs::exists(status)) {
        fail(path, "no such file", diagnostics);
        return std::nullopt;
    }
    if (fs::is_directory(status)) {
        fail(path, "it is a directory", diagnostics);
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(path, "permission denied or unreadable", diagnostics);
        return std::nullopt;
    }

    SourceFile file;
    file.path = path;
    file.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    // istreambuf_iterator stops silently on a read error; badbit tells a
    // failed read apart from a short file.
    if (in.bad()) {
        fail(path, "read error", diagnostics);
        return std::nullopt;
    }
    return file;
}

}  // namespace mt::core
