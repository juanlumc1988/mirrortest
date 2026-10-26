// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Helpers shared by the unit tests that touch the file system.

#ifndef MT_TESTS_TEST_SUPPORT_H
#define MT_TESTS_TEST_SUPPORT_H

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define MT_TEST_GETPID _getpid
#else
#include <unistd.h>
#define MT_TEST_GETPID getpid
#endif

namespace mt::test {

// A fresh empty directory, removed with everything in it on destruction.
// Unique per process and per instance, so tests can run in parallel.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const std::filesystem::path candidate =
                base / ("mirrortest-" + std::to_string(MT_TEST_GETPID()) + "-" +
                        std::to_string(counter.fetch_add(1)));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec) && !ec) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("TempDir: cannot create a temporary directory");
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }

    // Absolute path of `relative` inside the directory, as a string.
    std::string file(const std::string& relative) const {
        return (path_ / relative).string();
    }

private:
    std::filesystem::path path_;
};

// Writes `content` byte for byte (no newline translation), creating parent
// directories. Returns the path written.
inline std::string write_file(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path.string();
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace mt::test

#endif  // MT_TESTS_TEST_SUPPORT_H
