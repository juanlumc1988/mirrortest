// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>

#include "mirrortest/core/source.h"
#include "test_support.h"

using mt::core::Diagnostics;
using mt::core::read_source;
using mt::core::Severity;
using mt::test::TempDir;
using mt::test::write_file;
namespace code = mt::core::code;

TEST(ReadSource, ReadsTheFileAndKeepsThePathAsGiven) {
    TempDir dir;
    const std::string path = write_file(dir.path() / "foo_test.cpp", "TEST(A, B) {}\n");

    Diagnostics d;
    const auto file = read_source(path, d);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->path, path);
    EXPECT_EQ(file->text, "TEST(A, B) {}\n");
    EXPECT_TRUE(d.empty());
}

TEST(ReadSource, KeepsRelativePathsRelative) {
    // Diagnostics must read the same from any working directory.
    TempDir dir;
    write_file(dir.path() / "t.cpp", "x");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(dir.path());

    Diagnostics d;
    const auto file = read_source("t.cpp", d);
    std::filesystem::current_path(previous);

    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->path, "t.cpp");
}

TEST(ReadSource, DoesNotTouchLineEndingsOrBom) {
    TempDir dir;
    const std::string bytes = "\xEF\xBB\xBF" "int a;\r\nint b;\r\n";
    const std::string path = write_file(dir.path() / "crlf.h", bytes);

    Diagnostics d;
    const auto file = read_source(path, d);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->text, bytes);
}

TEST(ReadSource, KeepsEmbeddedNulBytes) {
    TempDir dir;
    const std::string bytes("a\0b", 3);
    const std::string path = write_file(dir.path() / "nul.cpp", bytes);

    Diagnostics d;
    const auto file = read_source(path, d);
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->text.size(), 3u);
    EXPECT_EQ(file->text, bytes);
}

TEST(ReadSource, AnEmptyFileIsNotAnError) {
    TempDir dir;
    const std::string path = write_file(dir.path() / "empty.cpp", "");

    Diagnostics d;
    const auto file = read_source(path, d);
    ASSERT_TRUE(file.has_value());
    EXPECT_TRUE(file->text.empty());
    EXPECT_TRUE(d.empty());
}

TEST(ReadSource, MissingFileIsAnIoErrorLocatedAtThePath) {
    TempDir dir;
    const std::string path = dir.file("nope.cpp");

    Diagnostics d;
    const auto file = read_source(path, d);
    EXPECT_FALSE(file.has_value());

    ASSERT_EQ(d.all().size(), 1u);
    const auto& e = d.all()[0];
    EXPECT_EQ(e.severity, Severity::Error);
    EXPECT_EQ(e.code, code::io);
    EXPECT_EQ(e.where.file, path);
    EXPECT_EQ(e.message, "cannot read file: no such file");
}

TEST(ReadSource, ADirectoryIsAnIoError) {
    TempDir dir;

    Diagnostics d;
    EXPECT_FALSE(read_source(dir.path().string(), d).has_value());
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].code, code::io);
    EXPECT_EQ(d.all()[0].message, "cannot read file: it is a directory");
}

TEST(ReadSource, ReportsEveryFailureNotJustTheFirst) {
    TempDir dir;
    Diagnostics d;
    EXPECT_FALSE(read_source(dir.file("a.cpp"), d).has_value());
    EXPECT_FALSE(read_source(dir.file("b.cpp"), d).has_value());
    EXPECT_EQ(d.count(Severity::Error), 2u);
}
