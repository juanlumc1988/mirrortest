// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Which includes of a test are headers to generate and which already exist.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/include_search.h"
#include "test_support.h"

using namespace mt;
using core::IncludeKind;

namespace {

ir::Include quoted(const char* path, unsigned line = 1) {
    ir::Include i;
    i.path = path;
    i.where = {"t.cpp", line, 1};
    return i;
}

ir::Include angled(const char* path) {
    ir::Include i = quoted(path);
    i.is_system = true;
    return i;
}

// ---- is_foreign_include -----------------------------------------------------------------------------------

TEST(IncludeSearchForeign, FrameworksQtAndTheStandardLibrary) {
    for (const char* p : {"gtest/gtest.h", "gmock/gmock.h", "catch2/catch.hpp", "doctest/doctest.h", "QtTest", "QtCore/QString",
                          "QtCore/qglobal.h", "QtWidgets/qwidget.h", "QtTest/qtest.h",
                          "QString", "QObject", "vector", "string", "cstdint", "bits/stl_vector.h/../x"}) {
        EXPECT_TRUE(core::is_foreign_include(p)) << p;
    }
}

TEST(IncludeSearchForeign, TheHeaderOfTheHookPolicyIsWrittenByUsAsASupportFileNotAClassToGenerate) {
    EXPECT_TRUE(core::is_foreign_include("mt_not_implemented.h"));
    for (const char* p : {"acme/mt_not_implemented.h", "my_mt_not_implemented.h", "mt_not_implemented.hpp", "mt_not_implemented"}) {
        EXPECT_EQ(core::is_foreign_include(p), std::string_view(p) == "mt_not_implemented") << p << ": only that file, as the stubs spell it";
    }
}

TEST(IncludeSearchForeign, TheMocFileOfAQtTestIsWrittenByTheBuildNotByUs) {
    for (const char* p : {"tst_foo.moc", "acme/tst_foo.moc", "foo.moc"}) EXPECT_TRUE(core::is_foreign_include(p)) << p;
    for (const char* p : {"mocha.h", "moc_helpers.h", "foo.moc.h"}) EXPECT_FALSE(core::is_foreign_include(p)) << p;
}

TEST(IncludeSearchForeign, ProjectHeadersAreNot) {
    for (const char* p : {"foo.h", "acme/foo.h", "acme/foo.hpp", "a/b/c.hh", "../x.h", "my_gtest_helpers.h", "qt_helpers.h", "Quark.h",
                          "QtUtils/helper.h", "gtest_extras/x.h"}) {
        EXPECT_FALSE(core::is_foreign_include(p)) << p;
    }
}

// ---- resolve_includes --------------------------------------------------------------------------------------

core::SearchDir at(const std::filesystem::path& p) { return {p, p.string()}; }

struct Tree {
    test::TempDir dir;
    Tree() {
        test::write_file(dir.path() / "tests/helper.h", "class Helper {};\n");
        test::write_file(dir.path() / "inc/acme/lib.h", "class Lib {};\n");
        test::write_file(dir.path() / "src/acme/gen.h", "class Gen {};\n");
        test::write_file(dir.path() / "other/acme/lib.h", "class OtherLib {};\n");
    }
    core::IncludeSearch search() const {
        core::IncludeSearch s;
        s.test_dir = at(dir.path() / "tests");
        s.include_dirs = {at(dir.path() / "inc")};
        s.out_include = at(dir.path() / "src");
        return s;
    }
};

TEST(IncludeSearchResolve, SomethingThatDoesNotExistIsATarget) {
    const Tree tree;
    const auto r = core::resolve_includes({quoted("acme/foo.h")}, tree.search());
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].kind, IncludeKind::Target);
    EXPECT_TRUE(r[0].file.empty());
    EXPECT_EQ(r[0].path, "acme/foo.h");
}

TEST(IncludeSearchResolve, NextToTheTestOrOnAnIncludeDirectoryIsKnown) {
    const Tree tree;
    const auto r = core::resolve_includes({quoted("helper.h"), quoted("acme/lib.h")}, tree.search());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].kind, IncludeKind::Known);
    EXPECT_EQ(r[0].file, tree.dir.path() / "tests/helper.h");
    EXPECT_EQ(r[1].kind, IncludeKind::Known);
    EXPECT_EQ(r[1].file, tree.dir.path() / "inc/acme/lib.h");
}

TEST(IncludeSearchResolve, TheFirstDirectoryInOrderWins) {
    Tree tree;
    core::IncludeSearch s = tree.search();
    s.include_dirs = {at(tree.dir.path() / "other"), at(tree.dir.path() / "inc")};
    const auto r = core::resolve_includes({quoted("acme/lib.h")}, s);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].file, tree.dir.path() / "other/acme/lib.h");

    core::IncludeSearch t = tree.search();
    test::write_file(tree.dir.path() / "tests/acme/lib.h", "// next to the test\n");
    const auto beside = core::resolve_includes({quoted("acme/lib.h")}, t);
    EXPECT_EQ(beside[0].file, tree.dir.path() / "tests/acme/lib.h") << "the test's own directory comes before -I";
}

TEST(IncludeSearchResolve, AFileInTheOutputDirectoryIsATargetEvenIfIncludeDirectoriesLeadThere) {
    Tree tree;
    core::IncludeSearch s = tree.search();
    s.include_dirs = {at(tree.dir.path() / "src")};   // as it has to be for the test to build
    const auto r = core::resolve_includes({quoted("acme/gen.h")}, s);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].kind, IncludeKind::Target);
    EXPECT_EQ(r[0].file, tree.dir.path() / "src/acme/gen.h");
}

TEST(IncludeSearchResolve, AFileInTheOutputDirectoryAloneIsATargetToo) {
    const Tree tree;
    const auto r = core::resolve_includes({quoted("acme/gen.h")}, tree.search());
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].kind, IncludeKind::Target);
}

TEST(IncludeSearchResolve, TheSameNameElsewhereBeforeTheOutputDirectoryIsKnown) {
    Tree tree;
    test::write_file(tree.dir.path() / "inc/acme/gen.h", "class Elsewhere {};\n");
    const auto r = core::resolve_includes({quoted("acme/gen.h")}, tree.search());
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].kind, IncludeKind::Known) << "found on -I before the output directory, and a different file";
}

TEST(IncludeSearchResolve, ForeignIncludesAreNeverLookedFor) {
    Tree tree;
    test::write_file(tree.dir.path() / "tests/vector", "// a file called vector\n");
    const auto r = core::resolve_includes({quoted("gtest/gtest.h"), quoted("vector"), quoted("QString")}, tree.search());
    ASSERT_EQ(r.size(), 3u);
    for (const auto& i : r) {
        EXPECT_EQ(i.kind, IncludeKind::Foreign) << i.path;
        EXPECT_TRUE(i.file.empty()) << i.path;
    }
}

TEST(IncludeSearchResolve, SystemIncludesAreKnownIfFoundOnIncludeDirsAndOtherwiseNobodysBusiness) {
    const Tree tree;
    const auto r = core::resolve_includes({angled("acme/lib.h"), angled("acme/missing.h"), angled("helper.h")}, tree.search());
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].kind, IncludeKind::Known);
    EXPECT_EQ(r[1].kind, IncludeKind::Foreign) << "never a target";
    EXPECT_EQ(r[2].kind, IncludeKind::Foreign) << "the test's own directory is not searched for <...>";
}

TEST(IncludeSearchResolve, RepeatsAreDroppedAndOrderAndLocationKept) {
    const Tree tree;
    const auto r = core::resolve_includes({quoted("b.h", 3), quoted("a.h", 4), quoted("b.h", 5)}, tree.search());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].path, "b.h");
    EXPECT_EQ(r[0].where.line, 3u);
    EXPECT_EQ(r[1].path, "a.h");
}

TEST(IncludeSearchResolve, NoSearchDirectoriesAtAllMeansEverythingQuotedIsATarget) {
    const auto r = core::resolve_includes({quoted("foo.h"), quoted("acme/bar.h"), quoted("gtest/gtest.h")}, {});
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].kind, IncludeKind::Target);
    EXPECT_EQ(r[1].kind, IncludeKind::Target);
    EXPECT_EQ(r[2].kind, IncludeKind::Foreign);
}

TEST(IncludeSearchResolve, TheFoundFileIsShownTheWayTheUserNamedItsDirectory) {
    const Tree tree;
    core::IncludeSearch s = tree.search();
    s.test_dir.shown = "tests";
    s.include_dirs[0].shown = "inc/";
    const auto r = core::resolve_includes({quoted("helper.h"), quoted("acme/lib.h"), quoted("missing.h")}, s);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].shown, "tests/helper.h");
    EXPECT_EQ(r[1].shown, "inc/acme/lib.h") << "no doubled slash";
    EXPECT_EQ(r[2].shown, "") << "not found: nothing to show";

    s.test_dir.shown = "";
    s.include_dirs[0].shown = ".";
    const auto bare = core::resolve_includes({quoted("helper.h"), quoted("acme/lib.h")}, s);
    EXPECT_EQ(bare[0].shown, "helper.h");
    EXPECT_EQ(bare[1].shown, "acme/lib.h");
}

TEST(IncludeSearchResolve, ADirectoryOfThatNameIsNotAHeader) {
    Tree tree;
    std::filesystem::create_directories(tree.dir.path() / "tests/dir.h");
    const auto r = core::resolve_includes({quoted("dir.h")}, tree.search());
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].kind, IncludeKind::Target);
}

TEST(IncludeSearchResolve, AnEmptyPathIsSkipped) {
    const Tree tree;
    EXPECT_TRUE(core::resolve_includes({quoted("")}, tree.search()).empty());
}

// ---- path_below -----------------------------------------------------------------------------------------------

TEST(IncludeSearchPathBelow, AFileBelowTheDirectoryIsTheRestOfItsPath) {
    EXPECT_EQ(core::path_below("/p/include/acme/foo.h", "/p/include"), std::optional<std::string>("acme/foo.h"));
    EXPECT_EQ(core::path_below("/p/include/foo.h", "/p/include"), std::optional<std::string>("foo.h"));
    EXPECT_EQ(core::path_below("/p/include/a/b/c/foo.h", "/p/include/a"), std::optional<std::string>("b/c/foo.h"));
}

TEST(IncludeSearchPathBelow, TrailingSlashesAndDotsDoNotMatter) {
    EXPECT_EQ(core::path_below("/p/include/acme/foo.h", "/p/include/"), std::optional<std::string>("acme/foo.h"));
    EXPECT_EQ(core::path_below("/p/./include/acme/../acme/foo.h", "/p/src/../include"), std::optional<std::string>("acme/foo.h"));
}

TEST(IncludeSearchPathBelow, AFileOutsideTheDirectoryIsNotBelowIt) {
    EXPECT_EQ(core::path_below("/p/include/foo.h", "/p/other"), std::nullopt);
    EXPECT_EQ(core::path_below("/p/include/foo.h", "/p/include/acme"), std::nullopt) << "the directory is deeper than the file";
    EXPECT_EQ(core::path_below("/p/include/foo.h", "/q"), std::nullopt);
    EXPECT_EQ(core::path_below("/p/include-more/foo.h", "/p/include"), std::nullopt) << "a prefix of the name is not a parent";
}

TEST(IncludeSearchPathBelow, TheDirectoryItselfIsNotBelowItself) {
    EXPECT_EQ(core::path_below("/p/include", "/p/include"), std::nullopt);
    EXPECT_EQ(core::path_below("/p/include/", "/p/include"), std::nullopt);
}

TEST(IncludeSearchPathBelow, PathsThatCannotBeComparedGiveNothing) {
    EXPECT_EQ(core::path_below("include/foo.h", "/p/include"), std::nullopt) << "one relative, one absolute";
    EXPECT_EQ(core::path_below("/p/include/foo.h", "include"), std::nullopt);
    EXPECT_EQ(core::path_below("", "/p/include"), std::nullopt) << "what could not be resolved is empty";
    EXPECT_EQ(core::path_below("/p/include/foo.h", ""), std::nullopt);
}

TEST(IncludeSearchPathBelow, RelativePathsCompareLexically) {
    EXPECT_EQ(core::path_below("include/acme/foo.h", "include"), std::optional<std::string>("acme/foo.h"));
    EXPECT_EQ(core::path_below("include/foo.h", "other"), std::nullopt);
}

}  // namespace
