// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Regions: finding the markers, refusing malformed ones, inserting before the
// end marker.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/core/merge.h"
#include "mirrortest/core/regions.h"

using namespace mt;

namespace {

using Lines = std::vector<std::string>;

struct Found {
    std::optional<std::vector<core::Region>> regions;
    core::Diagnostics                        diagnostics;
};

Found find(const std::string& text) {
    Found f;
    f.regions = core::find_regions(text, "t.h", f.diagnostics);
    return f;
}

std::vector<std::string> messages(const Found& f) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : f.diagnostics.sorted()) out.push_back(std::to_string(d.where.line) + ": " + d.message);
    return out;
}

// ---- lines ------------------------------------------------------------------------------------------------------

TEST(MergeLines, SplitKeepsTerminatorsAndJoinGivesTheTextBack) {
    for (const std::string& text : {std::string(""), std::string("a"), std::string("a\n"), std::string("a\nb"), std::string("a\r\nb\r\n"),
                                   std::string("\n\n"), std::string("a\n\nb\n")}) {
        std::string joined;
        for (const std::string& line : core::split_lines(text)) joined += line;
        EXPECT_EQ(joined, text);
    }
    EXPECT_EQ(core::split_lines("a\nb"), (Lines{"a\n", "b"}));
    EXPECT_EQ(core::split_lines("a\r\nb\r\n"), (Lines{"a\r\n", "b\r\n"}));
    EXPECT_TRUE(core::split_lines("").empty());
}

TEST(MergeLines, TheLineEndingIsThatOfTheFirstLine) {
    EXPECT_EQ(core::line_ending_of("a\nb\n"), "\n");
    EXPECT_EQ(core::line_ending_of("a\r\nb\r\n"), "\r\n");
    EXPECT_EQ(core::line_ending_of("a\r\nb\n"), "\r\n");
    EXPECT_EQ(core::line_ending_of("a\nb\r\n"), "\n");
    EXPECT_EQ(core::line_ending_of("no newline"), "\n");
    EXPECT_EQ(core::line_ending_of(""), "\n");
}

// ---- finding regions --------------------------------------------------------------------------------------------

TEST(MergeFind, RegionsInOrderWithTheirLinesAndTheIndentOfTheEndMarker) {
    const Found f = find(
        "// <mt:begin includes>\n"
        "// <mt:end includes>\n"
        "class Foo {\n"
        "public:\n"
        "    // <mt:begin acme::Foo>\n"
        "    int x();\n"
        "    // <mt:end acme::Foo>\n"
        "};\n");
    ASSERT_TRUE(f.regions.has_value());
    ASSERT_EQ(f.regions->size(), 2u);
    EXPECT_EQ((*f.regions)[0].label, "includes");
    EXPECT_EQ((*f.regions)[0].begin, 0u);
    EXPECT_EQ((*f.regions)[0].end, 1u);
    EXPECT_EQ((*f.regions)[0].indent, "");
    EXPECT_EQ((*f.regions)[1].label, "acme::Foo");
    EXPECT_EQ((*f.regions)[1].begin, 4u);
    EXPECT_EQ((*f.regions)[1].end, 6u);
    EXPECT_EQ((*f.regions)[1].indent, "    ");
    EXPECT_TRUE(f.diagnostics.empty());
}

TEST(MergeFind, NoRegionsIsFine) {
    const Found f = find("int main() {}\n");
    ASSERT_TRUE(f.regions.has_value());
    EXPECT_TRUE(f.regions->empty());
    EXPECT_TRUE(find("").regions->empty());
}

TEST(MergeFind, WhitespaceAroundAMarkerAndTabsAreFine) {
    const Found f = find("  \t// <mt:begin a>  \r\n\t\t// <mt:end a>\t\r\n");
    ASSERT_TRUE(f.regions.has_value());
    ASSERT_EQ(f.regions->size(), 1u);
    EXPECT_EQ((*f.regions)[0].indent, "\t\t");
}

TEST(MergeFind, AMarkerAfterCodeOrInsideATextIsNotAMarker) {
    const Found f = find(
        "int x;  // <mt:begin a>\n"
        "const char* s = \"// <mt:begin b>\";\n"
        "/* // <mt:begin c> */\n"
        "//<mt:begin d>\n"
        "// <mt:begin >\n"
        "// <mt:begin e f>\n"
        "// <mt:begin\n"
        "// <mt:begin ab\n"
        "// <mt:end ab\n");
    ASSERT_TRUE(f.regions.has_value());
    EXPECT_TRUE(f.regions->empty());
    EXPECT_TRUE(f.diagnostics.empty());
}

TEST(MergeFind, LabelsMayBeQualifiedAndFunctionsRegionsLookLikeThat) {
    const Found f = find(
        "// <mt:begin a::b::functions>\n// <mt:end a::b::functions>\n"
        "// <mt:begin functions>\n// <mt:end functions>\n");
    ASSERT_TRUE(f.regions.has_value());
    EXPECT_EQ((*f.regions)[0].label, "a::b::functions");
    EXPECT_EQ((*f.regions)[1].label, "functions");
}

TEST(MergeFind, TheMarkersAreTheOnesTheBackendWrites) {
    const std::string text = core::region_begin(core::class_label({"acme"}, "Foo")) + "\n" + core::region_end(core::class_label({"acme"}, "Foo")) + "\n";
    const Found f = find(text);
    ASSERT_TRUE(f.regions.has_value());
    ASSERT_EQ(f.regions->size(), 1u);
    EXPECT_EQ((*f.regions)[0].label, "acme::Foo");
}

TEST(MergeFind, NestedRegionsAreAnError) {
    const Found f = find("// <mt:begin a>\n// <mt:begin b>\n// <mt:end b>\n// <mt:end a>\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f), (Lines{"2: region 'b' begins inside region 'a' (line 1); regions do not nest"}));
    EXPECT_EQ(f.diagnostics.sorted()[0].code, "mt-merge-region");
    EXPECT_EQ(f.diagnostics.sorted()[0].severity, core::Severity::Error);
    EXPECT_EQ(f.diagnostics.sorted()[0].where.file, "t.h");
}

TEST(MergeFind, AnEndWithoutABeginIsAnError) {
    const Found f = find("int x;\n// <mt:end a>\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f), (Lines{"2: region 'a' ends but never began"}));
}

TEST(MergeFind, AnEndNamingAnotherLabelIsAnError) {
    const Found f = find("// <mt:begin a>\n// <mt:end b>\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f), (Lines{"2: region 'a' (line 1) is closed by the end of 'b'"}));
}

TEST(MergeFind, ABeginThatIsNeverClosedIsAnError) {
    const Found f = find("x;\n// <mt:begin a>\nint y;\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f), (Lines{"2: region 'a' is never closed"}));
}

TEST(MergeFind, TheSameLabelTwiceIsAnError) {
    const Found f = find("// <mt:begin a>\n// <mt:end a>\n// <mt:begin a>\n// <mt:end a>\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f), (Lines{"3: region 'a' appears twice (first at line 1)"}));
}

TEST(MergeFind, EveryProblemIsReportedNotJustTheFirst) {
    const Found f = find("// <mt:end x>\n// <mt:begin a>\n// <mt:begin a>\n// <mt:end a>\n// <mt:begin z>\n");
    EXPECT_FALSE(f.regions.has_value());
    EXPECT_EQ(messages(f).size(), 3u);
}

TEST(MergeFind, EveryPrefixOfADocumentIsHandled) {
    const std::string text =
        "// <mt:begin includes>\n// <mt:end includes>\nclass A {\n    // <mt:begin A>\n    // <mt:end A>\n};\n"
        "// <mt:begin functions>\nint f();\n// <mt:end functions>\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Found f = find(text.substr(0, n));
        if (f.regions) {
            for (const core::Region& r : *f.regions) EXPECT_LT(r.begin, r.end) << "cut at " << n;
        } else {
            EXPECT_TRUE(f.diagnostics.has_errors()) << "cut at " << n;
        }
    }
}

TEST(MergeFind, FindRegionByLabel) {
    const Found f = find("// <mt:begin a>\n// <mt:end a>\n// <mt:begin b>\n// <mt:end b>\n");
    ASSERT_TRUE(f.regions.has_value());
    ASSERT_NE(core::find_region(*f.regions, "b"), nullptr);
    EXPECT_EQ(core::find_region(*f.regions, "b")->begin, 2u);
    EXPECT_EQ(core::find_region(*f.regions, "c"), nullptr);
}

// ---- the body ---------------------------------------------------------------------------------------------------

TEST(MergeBody, TheLinesBetweenTheMarkersWithoutTerminators) {
    const std::string text = "x\r\n// <mt:begin a>\r\n  one\r\n\r\n  two\r\n// <mt:end a>\r\ny\r\n";
    const Found f = find(text);
    ASSERT_TRUE(f.regions.has_value());
    EXPECT_EQ(core::region_body(text, (*f.regions)[0]), (Lines{"  one", "", "  two"}));
}

TEST(MergeBody, AnEmptyRegionHasAnEmptyBody) {
    const std::string text = "// <mt:begin a>\n// <mt:end a>\n";
    const Found f = find(text);
    EXPECT_TRUE(core::region_body(text, (*f.regions)[0]).empty());
}

// ---- inserting --------------------------------------------------------------------------------------------------

TEST(MergeInsert, GoesJustBeforeTheEndMarkerWithItsIndentation) {
    const std::string text = "class A {\n    // <mt:begin A>\n    int a();\n    // <mt:end A>\n};\n";
    const Found f = find(text);
    ASSERT_TRUE(f.regions.has_value());
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"// TODO(mt): new", "int b();"}),
              "class A {\n    // <mt:begin A>\n    int a();\n    // TODO(mt): new\n    int b();\n    // <mt:end A>\n};\n");
}

TEST(MergeInsert, IntoAnEmptyRegion) {
    const std::string text = "// <mt:begin includes>\n// <mt:end includes>\n";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"#include <string>"}),
              "// <mt:begin includes>\n#include <string>\n// <mt:end includes>\n");
}

TEST(MergeInsert, BlankLinesStayBlankWithoutTrailingSpaces) {
    const std::string text = "  // <mt:begin a>\n  // <mt:end a>\n";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"x", "", "y"}), "  // <mt:begin a>\n  x\n\n  y\n  // <mt:end a>\n");
}

TEST(MergeInsert, TabsAreKept) {
    const std::string text = "\t// <mt:begin a>\n\t// <mt:end a>\n";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"x"}), "\t// <mt:begin a>\n\tx\n\t// <mt:end a>\n");
}

TEST(MergeInsert, CrlfTextGetsCrlfLines) {
    const std::string text = "// <mt:begin a>\r\n// <mt:end a>\r\n";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"x", "y"}), "// <mt:begin a>\r\nx\r\ny\r\n// <mt:end a>\r\n");
}

TEST(MergeInsert, TheEndMarkerMayBeTheLastLineWithoutANewline) {
    const std::string text = "// <mt:begin a>\n// <mt:end a>";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {"x"}), "// <mt:begin a>\nx\n// <mt:end a>");
}

TEST(MergeInsert, NothingToInsertChangesNothing) {
    const std::string text = "// <mt:begin a>\n// <mt:end a>\n";
    const Found f = find(text);
    EXPECT_EQ(core::insert_before_end(text, (*f.regions)[0], {}), text);
}

TEST(MergeInsert, OnlyTheTargetRegionChangesAndAnotherOneIsStillFound) {
    const std::string text = "// <mt:begin a>\n// <mt:end a>\nmid\n// <mt:begin b>\n// <mt:end b>\n";
    const Found f = find(text);
    const std::string merged = core::insert_before_end(text, *core::find_region(*f.regions, "a"), {"x"});
    EXPECT_EQ(merged, "// <mt:begin a>\nx\n// <mt:end a>\nmid\n// <mt:begin b>\n// <mt:end b>\n");
    const Found again = find(merged);
    ASSERT_TRUE(again.regions.has_value());
    EXPECT_EQ(core::region_body(merged, *core::find_region(*again.regions, "a")), (Lines{"x"}));
}

TEST(MergeInsert, ARegionOfAnotherTextChangesNothing) {
    core::Region foreign;
    foreign.begin = 40;
    foreign.end = 41;
    EXPECT_EQ(core::insert_before_end("short\n", foreign, {"x"}), "short\n");
}

}  // namespace
