// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "mirrortest/core/diagnostics.h"

using mt::core::Diagnostic;
using mt::core::Diagnostics;
using mt::core::Severity;
using mt::ir::SourceLoc;
namespace code = mt::core::code;

namespace {

SourceLoc at(const char* file, unsigned line, unsigned column) {
    SourceLoc loc;
    loc.file = file;
    loc.line = line;
    loc.column = column;
    return loc;
}

std::string render(const Diagnostics& d) {
    std::ostringstream out;
    d.format(out);
    return out.str();
}

}  // namespace

TEST(Diagnostics, FormatsLikeGcc) {
    Diagnostics d;
    d.warning(code::unused_result, "result of Foo::run() is never used; assumed void",
              at("tests/foo_test.cpp", 17, 5));
    EXPECT_EQ(render(d),
              "tests/foo_test.cpp:17:5: warning: result of Foo::run() is never used; "
              "assumed void [mt-unused-result]\n");
}

TEST(Diagnostics, FormatDegradesWithTheLocation) {
    Diagnostics d;
    d.error(code::config, "unknown key 'colour'");
    d.error(code::io, "cannot read file", at("missing.cpp", 0, 0));
    d.error(code::parse, "unexpected token", at("a.cpp", 4, 0));
    EXPECT_EQ(render(d),
              "mirrortest: error: unknown key 'colour' [mt-config]\n"
              "a.cpp:4: error: unexpected token [mt-parse]\n"
              "missing.cpp: error: cannot read file [mt-io]\n");
}

TEST(Diagnostics, NotesFollowTheirDiagnostic) {
    Diagnostic conflict;
    conflict.severity = Severity::Warning;
    conflict.code = code::conflict;
    conflict.message = "Foo::size() is used with two return types";
    conflict.where = at("t.cpp", 12, 5);
    conflict.notes.push_back({at("t.cpp", 9, 5), "first inferred as 'int' here"});
    conflict.notes.push_back({at("t.cpp", 12, 5), "then as 'unsigned int' here"});

    Diagnostics d;
    d.add(conflict);
    EXPECT_EQ(render(d),
              "t.cpp:12:5: warning: Foo::size() is used with two return types [mt-conflict]\n"
              "t.cpp:9:5: note: first inferred as 'int' here\n"
              "t.cpp:12:5: note: then as 'unsigned int' here\n");
}

TEST(Diagnostics, OmitsTheBracketsWhenThereIsNoCode) {
    Diagnostic bare;
    bare.severity = Severity::Note;
    bare.message = "see above";
    bare.where = at("x.h", 1, 1);
    EXPECT_EQ(mt::core::format_diagnostic(bare), "x.h:1:1: note: see above\n");
}

TEST(Diagnostics, SortedByFileThenLineThenColumn) {
    Diagnostics d;
    d.warning(code::literal, "b", at("b.cpp", 1, 1));
    d.warning(code::literal, "a-late", at("a.cpp", 20, 1));
    d.warning(code::literal, "a-col2", at("a.cpp", 3, 2));
    d.warning(code::literal, "a-col1", at("a.cpp", 3, 1));
    d.warning(code::literal, "a-early", at("a.cpp", 2, 9));

    std::string order;
    for (const Diagnostic& x : d.sorted()) {
        order += x.message + " ";
    }
    EXPECT_EQ(order, "a-early a-col1 a-col2 a-late b ");
}

TEST(Diagnostics, NoFileSortsFirstAndCodeBreaksTies) {
    Diagnostics d;
    d.warning(code::unsupported, "w", at("a.cpp", 1, 1));
    d.warning(code::conflict, "w", at("a.cpp", 1, 1));
    d.error(code::config, "global");

    const std::vector<Diagnostic> sorted = d.sorted();
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].code, code::config);
    EXPECT_EQ(sorted[1].code, code::conflict);     // "mt-conflict" < "mt-unsupported"
    EXPECT_EQ(sorted[2].code, code::unsupported);
}

TEST(Diagnostics, SortedIsIndependentOfReportingOrder) {
    const SourceLoc l1 = at("a.cpp", 1, 1);
    const SourceLoc l2 = at("a.cpp", 2, 1);

    Diagnostics forward;
    forward.warning(code::literal, "one", l1);
    forward.warning(code::literal, "two", l2);

    Diagnostics backward;
    backward.warning(code::literal, "two", l2);
    backward.warning(code::literal, "one", l1);

    EXPECT_EQ(render(forward), render(backward));
}

TEST(Diagnostics, ExactDuplicatesArePrintedOnce) {
    // The same construct reached through both branches of an #if.
    Diagnostics d;
    d.warning(code::vexing_parse, "'Foo f();' declares a function", at("t.cpp", 5, 5));
    d.warning(code::vexing_parse, "'Foo f();' declares a function", at("t.cpp", 5, 5));
    EXPECT_EQ(d.sorted().size(), 1u);
    EXPECT_EQ(d.all().size(), 2u) << "all() keeps what was reported";
}

TEST(Diagnostics, DuplicatesAreFoundEvenWithAnotherTiedDiagnosticBetween) {
    // Same key, different notes: A, B, A must still collapse to A, B.
    Diagnostic a;
    a.severity = Severity::Warning;
    a.code = code::conflict;
    a.message = "m";
    a.where = at("t.cpp", 1, 1);
    a.notes.push_back({at("t.cpp", 2, 1), "n1"});

    Diagnostic b = a;
    b.notes[0].message = "n2";

    Diagnostics d;
    d.add(a);
    d.add(b);
    d.add(a);
    ASSERT_EQ(d.sorted().size(), 2u);
    EXPECT_EQ(d.sorted()[0], a);
    EXPECT_EQ(d.sorted()[1], b);
}

TEST(Diagnostics, DifferentMessagesAreNotDuplicates) {
    Diagnostics d;
    d.warning(code::literal, "x", at("t.cpp", 1, 1));
    d.warning(code::literal, "y", at("t.cpp", 1, 1));
    EXPECT_EQ(d.sorted().size(), 2u);
}

TEST(Diagnostics, CountsAndHasErrors) {
    Diagnostics d;
    EXPECT_TRUE(d.empty());
    EXPECT_FALSE(d.has_errors());

    d.note(code::conditional, "n");
    d.warning(code::literal, "w");
    EXPECT_FALSE(d.has_errors());
    EXPECT_EQ(d.count(Severity::Note), 1u);
    EXPECT_EQ(d.count(Severity::Warning), 1u);

    d.error(code::io, "e");
    EXPECT_TRUE(d.has_errors());
    EXPECT_EQ(d.count(Severity::Error), 1u);
    EXPECT_FALSE(d.empty());
}

TEST(Diagnostics, StrictPromotesWarningsButNotNotes) {
    Diagnostics d;
    d.note(code::conditional, "n");
    d.warning(code::literal, "w");
    d.error(code::io, "e");

    d.promote_warnings();

    EXPECT_EQ(d.count(Severity::Note), 1u);
    EXPECT_EQ(d.count(Severity::Warning), 0u);
    EXPECT_EQ(d.count(Severity::Error), 2u);
}

TEST(Diagnostics, StrictMakesAWarningOnlyRunFail) {
    Diagnostics d;
    d.warning(code::unused_result, "w");
    ASSERT_FALSE(d.has_errors());
    d.promote_warnings();
    EXPECT_TRUE(d.has_errors());
}

TEST(Diagnostics, CodeConstantsMatchTheCatalogue) {
    // Spot-check the spelling of codes that goldens and users depend on.
    EXPECT_STREQ(code::auto_deduced, "mt-auto");
    EXPECT_STREQ(code::stub_may_pass, "mt-stub-may-pass");
    EXPECT_STREQ(code::merge_no_region, "mt-merge-no-region");
    EXPECT_STREQ(code::std_version, "mt-std");
}
