// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What a merge adds to files an earlier run wrote, and what it leaves alone.
// The "generated" side is real output of the backend; the "existing" side is an
// older output, edited the way a user edits it.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "mirrortest/backend/impl_backend.h"
#include "mirrortest/backend/impl_merge.h"
#include "mirrortest/frontend/pipeline.h"

using namespace mt;

namespace {

using Files = std::map<std::string, std::string>;

// gen-impl over one test file, into memory.
Files generate(const std::string& test, const std::vector<std::string>& headers, core::Config config = {}) {
    core::Diagnostics diagnostics;
    frontend::AnalysisOptions ao;
    ao.config = config;
    const frontend::Analysis analysis = frontend::analyse({"tests/t.cpp", "#include <gtest/gtest.h>\n" + test}, ao, diagnostics);
    backend::ImplOptions options;
    options.config = config;
    options.source = "tests/t.cpp";
    options.headers_to_generate = headers;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, diagnostics);
    Files out;
    for (const core::OutputFile& f : backend::generate_impl(analysis.model, options, store, diagnostics).files) out[f.path] = f.content;
    return out;
}

struct Merged {
    std::optional<core::MergeResult> result;
    core::Diagnostics                diagnostics;
    std::string text() const { return result ? result->content : std::string("<refused>"); }
};

Merged merge_one(backend::ImplMerger& merger, const std::string& path, const std::string& generated, const std::string& existing) {
    Merged m;
    m.result = merger.merge({path, generated}, existing, m.diagnostics);
    return m;
}

Merged merge_header(const std::string& generated, const std::string& existing, const std::string& path = "foo.h") {
    backend::ImplMerger merger("", nullptr);
    return merge_one(merger, path, generated, existing);
}

std::vector<std::string> codes(const Merged& m) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : m.diagnostics.sorted()) out.push_back(d.code);
    return out;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::size_t count(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++n;
    return n;
}

const char* kOneTest =
    "#include \"foo.h\"\n"
    "TEST(S, N) {\n"
    "    Foo foo(3);\n"
    "    EXPECT_EQ(foo.size(), 3u);\n"
    "}\n";

const char* kTwoTests =
    "#include \"foo.h\"\n"
    "TEST(S, N) {\n"
    "    Foo foo(3);\n"
    "    EXPECT_EQ(foo.size(), 3u);\n"
    "    EXPECT_TRUE(foo.empty());\n"
    "    EXPECT_EQ(foo.name(), std::string(\"x\"));\n"
    "}\n";

// ---- headers: adding what is missing --------------------------------------------------------------------------

TEST(ImplMergeHeader, TheMembersTheOldFileLacksAreAddedWithTheirNotes) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), old.at("foo.h"));
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 2u);
    EXPECT_TRUE(m.diagnostics.empty());
    EXPECT_TRUE(contains(m.text(), "    // TODO(mt): return type inferred from EXPECT_TRUE (tests/t.cpp:"));
    EXPECT_TRUE(contains(m.text(), "    bool empty();\n"));
    EXPECT_TRUE(contains(m.text(), "    std::string name();\n"));
    EXPECT_TRUE(contains(m.text(), "#include <string>\n")) << "the include a new member needs";
    // What was there stays, once.
    EXPECT_EQ(count(m.text(), "unsigned int size();"), 1u);
    // The result is what a run over both tests would write, up to where the new members sit.
    EXPECT_EQ(m.text(), now.at("foo.h"));
}

TEST(ImplMergeHeader, MergingTwiceChangesNothingTheSecondTime) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged first = merge_header(now.at("foo.h"), old.at("foo.h"));
    ASSERT_TRUE(first.result.has_value());
    const Merged second = merge_header(now.at("foo.h"), first.text());
    ASSERT_TRUE(second.result.has_value());
    EXPECT_EQ(second.text(), first.text());
    EXPECT_EQ(second.result->added_members, 0u);
}

TEST(ImplMergeHeader, NothingMissingMeansTheFileComesBackAsItWas) {
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), now.at("foo.h"));
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.text(), now.at("foo.h"));
    EXPECT_EQ(m.result->added_members, 0u);
}

TEST(ImplMergeHeader, TheUsersOwnCodeIsNeverTouched) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    std::string mine = old.at("foo.h");
    mine.insert(mine.find("class Foo"), "// my own comment\n#define MINE 1\n");
    mine.insert(mine.find("    // <mt:end Foo>"), "    int hand_written() const { return 1; }\n");
    mine.insert(mine.find("};"), "private:\n    int secret_;\n");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_TRUE(contains(m.text(), "// my own comment\n#define MINE 1\n"));
    EXPECT_TRUE(contains(m.text(), "    int hand_written() const { return 1; }\n"));
    EXPECT_TRUE(contains(m.text(), "private:\n    int secret_;\n"));
    EXPECT_TRUE(contains(m.text(), "    bool empty();\n"));
    // The additions come after the user's line inside the region, just before the marker.
    EXPECT_LT(m.text().find("hand_written"), m.text().find("bool empty();"));
    EXPECT_LT(m.text().find("bool empty();"), m.text().find("// <mt:end Foo>"));
}

TEST(ImplMergeHeader, AMemberTheUserMovedOutOfTheRegionIsStillThere) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    std::string mine = old.at("foo.h");
    const std::string decl = "    unsigned int size();\n";
    mine.erase(mine.find(decl), decl.size());
    mine.insert(mine.find("};"), "    unsigned int size();   // moved\n");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(count(m.text(), "size()"), 1u) << "found in the class, not only in the region";
}

TEST(ImplMergeHeader, ADifferentSignatureOfTheSameMemberIsKeptAndReported) {
    const Files now = generate(kTwoTests, {"foo.h"});
    std::string mine = now.at("foo.h");
    mine.replace(mine.find("unsigned int size();"), std::string("unsigned int size();").size(), "int size() const;");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.text(), mine) << "left exactly as the user has it";
    ASSERT_EQ(codes(m), std::vector<std::string>{"mt-merge-mismatch"});
    const core::Diagnostic d = m.diagnostics.sorted()[0];
    EXPECT_EQ(d.severity, core::Severity::Warning);
    EXPECT_TRUE(contains(d.message, "'int size() const' is declared here but the tests imply 'unsigned int size()'")) << d.message;
    EXPECT_EQ(d.where.file, "foo.h");
    EXPECT_EQ(d.where.line, 16u) << "the user's declaration";
}

TEST(ImplMergeHeader, ADifferentParameterOrStaticnessIsAMismatchToo) {
    const char* test =
        "#include \"foo.h\"\nTEST(S, N) { Foo foo; foo.put(5); EXPECT_EQ(foo.get(), 2); EXPECT_EQ(Foo::make(), 1); foo.put(5); }\n";
    const Files now = generate(test, {"foo.h"});
    std::string mine = now.at("foo.h");
    mine.replace(mine.find("put(int arg1)"), 13, "put(long arg1)");
    mine.replace(mine.find("static int make"), 15, "int make");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(codes(m), (std::vector<std::string>{"mt-merge-mismatch", "mt-merge-mismatch"}));
}

TEST(ImplMergeHeader, ConstructorsAreMatchedByArity) {
    const Files old = generate("#include \"foo.h\"\nTEST(S, N) { Foo a(1); }\n", {"foo.h"});
    const Files now = generate("#include \"foo.h\"\nTEST(S, N) { Foo a(1); Foo b(1, 2); Foo c(7); }\n", {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), old.at("foo.h"));
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 1u) << "Foo(int, int); Foo(int) was there";
    EXPECT_TRUE(contains(m.text(), "Foo(int arg1, int arg2);"));
    EXPECT_EQ(count(m.text(), "explicit Foo(int arg1);"), 1u);
}

TEST(ImplMergeHeader, OverloadsOfOneArityAreToldApartByTheirTypes) {
    const char* before = "#include \"foo.h\"\nTEST(S, N) { Foo foo; foo.set(1); }\n";
    const char* after =
        "#include \"foo.h\"\nTEST(S, N) { Foo foo; foo.set(1); foo.set(2.5); foo.set(std::string(\"a\")); }\n";
    const Files old = generate(before, {"foo.h"});
    const Files now = generate(after, {"foo.h"});
    // One method `set` with a conflict, in the version of the tests without annotations: the new
    // file has a single `set(int)`; give the merge two to tell apart by writing them by hand.
    std::string wanted = now.at("foo.h");
    wanted.insert(wanted.find("    // <mt:end Foo>"), "    void set(double arg1);\n    void set(const std::string& arg1);\n");
    const Merged m = merge_header(wanted, old.at("foo.h"));
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 2u);
    EXPECT_TRUE(contains(m.text(), "void set(double arg1);"));
    EXPECT_TRUE(contains(m.text(), "void set(const std::string& arg1);"));
    EXPECT_EQ(count(m.text(), "void set(int arg1);"), 1u);
}

TEST(ImplMergeHeader, FieldsOfAStructAreMerged) {
    const Files old = generate("#include \"p.h\"\nTEST(S, N) { Point p; p.x = 5; }\n", {"p.h"});
    const Files now = generate("#include \"p.h\"\nTEST(S, N) { Point p; p.x = 5; p.y = 2.5; }\n", {"p.h"});
    const Merged m = merge_header(now.at("p.h"), old.at("p.h"), "p.h");
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 1u);
    EXPECT_TRUE(contains(m.text(), "    double y;\n"));
    EXPECT_EQ(count(m.text(), "int x;"), 1u);
}

TEST(ImplMergeHeader, ClassesInNamespacesAreFoundByTheirQualifiedName) {
    const char* before = "#include \"foo.h\"\nTEST(S, N) { acme::Foo foo; foo.a(); }\n";
    const char* after = "#include \"foo.h\"\nTEST(S, N) { acme::Foo foo; foo.a(); EXPECT_TRUE(foo.b()); }\n";
    const Files old = generate(before, {"foo.h"});
    const Files now = generate(after, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), old.at("foo.h"));
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 1u);
    EXPECT_TRUE(contains(m.text(), "    bool b();\n"));
}

TEST(ImplMergeHeader, TheFreeFunctionsOfANamespaceAreTheirOwnRegion) {
    const Files old = generate("#include \"u.h\"\nTEST(S, N) { EXPECT_EQ(acme::add(1, 2), 3); EXPECT_EQ(sub(3, 1), 2); }\n", {"u.h"});
    const Files now = generate(
        "#include \"u.h\"\nTEST(S, N) { EXPECT_EQ(acme::add(1, 2), 3); EXPECT_EQ(sub(3, 1), 2); EXPECT_EQ(acme::mul(2, 3), 6); EXPECT_EQ(quot(6, 3), 2); }\n",
        {"u.h"});
    const Merged m = merge_header(now.at("u.h"), old.at("u.h"), "u.h");
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 2u);
    EXPECT_EQ(m.text(), now.at("u.h")) << "each new function in the region of its own namespace";
}

TEST(ImplMergeHeader, FreeFunctionsHaveTheirOwnRegion) {
    const Files old = generate("#include \"u.h\"\nTEST(S, N) { EXPECT_EQ(add(1, 2), 3); }\n", {"u.h"});
    const Files now = generate("#include \"u.h\"\nTEST(S, N) { EXPECT_EQ(add(1, 2), 3); EXPECT_TRUE(is_even(4)); EXPECT_EQ(add(1, 2, 3), 6); }\n", {"u.h"});
    const Merged m = merge_header(now.at("u.h"), old.at("u.h"), "u.h");
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.result->added_members, 2u) << "is_even(int) and add with three arguments";
    EXPECT_TRUE(contains(m.text(), "bool is_even(int arg1);"));
    EXPECT_TRUE(contains(m.text(), "int add(int arg1, int arg2, int arg3);"));
    EXPECT_EQ(count(m.text(), "int add(int arg1, int arg2);"), 1u);
}

// ---- headers: when it will not do ------------------------------------------------------------------------------

TEST(ImplMergeHeader, AClassWithoutARegionSendsTheWholeFileToDotNew) {
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), "class Foo {\npublic:\n    unsigned int size();\n};\n");
    EXPECT_FALSE(m.result.has_value());
    ASSERT_EQ(codes(m), std::vector<std::string>{"mt-merge-no-region"});
    EXPECT_TRUE(contains(m.diagnostics.sorted()[0].message, "'Foo' has no region in foo.h")) << m.diagnostics.sorted()[0].message;
    EXPECT_EQ(m.diagnostics.sorted()[0].severity, core::Severity::Warning);
}

TEST(ImplMergeHeader, ARegionForAClassTheFileDoesNotDeclareIsRefusedToo) {
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), "// <mt:begin Foo>\n// <mt:end Foo>\n");
    EXPECT_FALSE(m.result.has_value());
    EXPECT_EQ(codes(m), std::vector<std::string>{"mt-merge-no-region"});
}

TEST(ImplMergeHeader, MalformedRegionsInTheExistingFileAreAnError) {
    const Files now = generate(kTwoTests, {"foo.h"});
    const Merged m = merge_header(now.at("foo.h"), "// <mt:begin Foo>\n// <mt:begin Foo>\n");
    EXPECT_FALSE(m.result.has_value());
    EXPECT_TRUE(m.diagnostics.has_errors());
    EXPECT_EQ(m.diagnostics.sorted()[0].code, "mt-merge-region");
    EXPECT_EQ(m.diagnostics.sorted()[0].where.file, "foo.h");
}

TEST(ImplMergeHeader, AResultThatParsesWorseIsRefused) {
    // The end marker has been left inside a function body, where `explicit` is not valid.
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate("#include \"foo.h\"\nTEST(S, N) { Foo foo(3); Foo other(1, 2); EXPECT_EQ(foo.size(), 3u); }\n", {"foo.h"});
    const std::string mine =
        "class Foo {\npublic:\n    // <mt:begin Foo>\n    void f() {\n    // <mt:end Foo>\n    }\n};\n";
    (void)old;
    const Merged m = merge_header(now.at("foo.h"), mine);
    EXPECT_FALSE(m.result.has_value());
    EXPECT_EQ(codes(m).back(), "mt-merge-broke");
}

// ---- includes ---------------------------------------------------------------------------------------------------

TEST(ImplMergeIncludes, AMissingIncludeGoesIntoTheIncludesRegion) {
    const Files now = generate("#include \"foo.h\"\nTEST(S, N) { Foo foo; std::vector<int> v = foo.items(); }\n", {"foo.h"});
    std::string mine = now.at("foo.h");
    mine.erase(mine.find("#include <vector>\n"), std::string("#include <vector>\n").size());
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(m.text(), now.at("foo.h"));
}

TEST(ImplMergeIncludes, AnIncludeTheUserHasElsewhereIsNotAddedAgain) {
    const Files now = generate("#include \"foo.h\"\nTEST(S, N) { Foo foo; std::vector<int> v = foo.items(); }\n", {"foo.h"});
    std::string mine = now.at("foo.h");
    mine.erase(mine.find("#include <vector>\n"), std::string("#include <vector>\n").size());
    mine.insert(0, "#include <vector>\n");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_EQ(count(m.text(), "#include <vector>"), 1u);
    mine.replace(mine.find("#include <vector>"), 17, "#  include   <vector>");
    EXPECT_EQ(count(merge_header(now.at("foo.h"), mine).text(), "vector>"), 1u) << "spacing does not matter";
}

TEST(ImplMergeIncludes, WithoutAnIncludesRegionItIsOnlyAWarning) {
    const Files now = generate("#include \"foo.h\"\nTEST(S, N) { Foo foo; std::vector<int> v = foo.items(); foo.run(); }\n", {"foo.h"});
    std::string mine = now.at("foo.h");
    mine.erase(mine.find("#include <vector>\n"), std::string("#include <vector>\n").size());
    mine.erase(mine.find("// <mt:begin includes>\n"), std::string("// <mt:begin includes>\n").size());
    mine.erase(mine.find("// <mt:end includes>\n"), std::string("// <mt:end includes>\n").size());
    mine.replace(mine.find("    void run();"), 15, "");   // so that something else is missing too
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value()) << "the members are still merged";
    EXPECT_TRUE(contains(m.text(), "void run();"));
    EXPECT_FALSE(contains(m.text(), "<vector>"));
    ASSERT_EQ(codes(m), std::vector<std::string>{"mt-merge-no-region"});
    EXPECT_TRUE(contains(m.diagnostics.sorted()[0].message, "no 'includes' region")) << m.diagnostics.sorted()[0].message;
}

// ---- line endings and shape of the file -------------------------------------------------------------------------

TEST(ImplMergeHeader, AFileWithCrlfLineEndingsGetsCrlfInsertions) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    std::string crlf;
    for (char c : old.at("foo.h")) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const Merged m = merge_header(now.at("foo.h"), crlf);
    ASSERT_TRUE(m.result.has_value());
    std::string expected;
    for (char c : now.at("foo.h")) {
        if (c == '\n') expected += '\r';
        expected += c;
    }
    EXPECT_EQ(m.text(), expected);
}

TEST(ImplMergeHeader, TheRegionIndentIsTheUsers) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    std::string mine = old.at("foo.h");
    for (std::size_t at = mine.find("\n    "); at != std::string::npos; at = mine.find("\n    ", at + 1)) mine.replace(at, 5, "\n\t");
    const Merged m = merge_header(now.at("foo.h"), mine);
    ASSERT_TRUE(m.result.has_value());
    EXPECT_TRUE(contains(m.text(), "\n\tbool empty();\n"));
    EXPECT_FALSE(contains(m.text(), "\n    bool empty();"));
}

TEST(ImplMergeHeader, OtherFileTypesAreNotMerged) {
    backend::ImplMerger merger("", nullptr);
    core::Diagnostics d;
    EXPECT_FALSE(merger.merge({"notes.txt", "x\n"}, "y\n", d).has_value());
}

TEST(ImplMergeHeader, EveryPrefixOfAnExistingFileIsHandled) {
    const Files old = generate(kOneTest, {"foo.h"});
    const Files now = generate(kTwoTests, {"foo.h"});
    const std::string text = old.at("foo.h");
    for (std::size_t n = 0; n <= text.size(); n += 7) {
        const Merged m = merge_header(now.at("foo.h"), text.substr(0, n));
        if (m.result) {
            EXPECT_GE(m.result->content.size(), n) << "cut at " << n;
        }
    }
}

// ---- sources ---------------------------------------------------------------------------------------------------

struct Pair {
    Files old, now;
};

Pair two_versions(const char* before, const char* after, std::vector<std::string> headers = {"foo.h"}) {
    return {generate(before, headers), generate(after, headers)};
}

TEST(ImplMergeSource, TheDefinitionsOfTheNewMembersAreAdded) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.old.at("foo.h"); });
    const Merged header = merge_one(merger, "foo.h", p.now.at("foo.h"), p.old.at("foo.h"));
    ASSERT_TRUE(header.result.has_value());
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), p.old.at("foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_EQ(source.result->added_members, 2u);
    EXPECT_EQ(source.text(), p.now.at("foo.cpp"));
}

TEST(ImplMergeSource, AMethodTheUserImplementedKeepsItsBody) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    std::string mine = p.old.at("foo.cpp");
    const std::string stub = "    throw std::logic_error(\"mirrortest: not implemented: Foo::size()\");";
    ASSERT_TRUE(contains(mine, stub));
    mine.replace(mine.find(stub), stub.size(), "    return 3;   // implemented");
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.old.at("foo.h"); });
    (void)merge_one(merger, "foo.h", p.now.at("foo.h"), p.old.at("foo.h"));
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), mine);
    ASSERT_TRUE(source.result.has_value());
    EXPECT_TRUE(contains(source.text(), "return 3;   // implemented"));
    EXPECT_EQ(count(source.text(), "Foo::size()"), 1u);
    EXPECT_TRUE(contains(source.text(), "bool Foo::empty()"));
}

TEST(ImplMergeSource, ADefinitionWhoseDeclarationIsNotThereIsNotAdded) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    // The header has `int size() const` where the tests say `unsigned int size()`, and is not merged
    // with the new members: no declaration, no definition.
    std::string header = p.old.at("foo.h");
    header.replace(header.find("unsigned int size();"), 20, "int size() const;");
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return header; });
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), p.old.at("foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_FALSE(contains(source.text(), "bool Foo::empty()")) << "not declared by the header the user has";
    EXPECT_EQ(source.result->added_members, 0u);
    EXPECT_EQ(source.text(), p.old.at("foo.cpp"));
}

TEST(ImplMergeSource, AnEmptyRegionIsNotOpenedWithABlankLine) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    std::string mine = p.old.at("foo.cpp");
    const std::size_t from = mine.find("// <mt:begin Foo>\n") + std::string("// <mt:begin Foo>\n").size();
    mine.erase(from, mine.find("// <mt:end Foo>") - from);   // the user deleted every stub
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.now.at("foo.h"); });
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), mine);
    ASSERT_TRUE(source.result.has_value());
    EXPECT_TRUE(contains(source.text(), "// <mt:begin Foo>\nFoo::Foo(int /*arg1*/) {}\n\n")) << source.text();
    EXPECT_FALSE(contains(source.text(), "// <mt:begin Foo>\n\n"));
}

TEST(ImplMergeSource, WithTheHeaderUnreadableNothingIsAdded) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    backend::ImplMerger merger("", [](const std::string&) -> std::optional<std::string> { return std::nullopt; });
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), p.old.at("foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_EQ(source.text(), p.old.at("foo.cpp"));
}

TEST(ImplMergeSource, TheHeaderIsFoundUnderTheIncludeDirectory) {
    const Pair p = two_versions("#include \"acme/foo.h\"\nTEST(S, N) { acme::Foo f; f.a(); }\n",
                                "#include \"acme/foo.h\"\nTEST(S, N) { acme::Foo f; f.a(); EXPECT_TRUE(f.b()); }\n", {"acme/foo.h"});
    std::string asked;
    backend::ImplMerger merger("include", [&](const std::string& path) -> std::optional<std::string> {
        asked = path;
        return p.now.at("acme/foo.h");
    });
    const Merged source = merge_one(merger, "src/acme/foo.cpp", p.now.at("acme/foo.cpp"), p.old.at("acme/foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_EQ(asked, "include/acme/foo.h");
    EXPECT_TRUE(contains(source.text(), "bool Foo::b()"));
}

TEST(ImplMergeSource, ConstructorsFunctionsAndNamespaces) {
    const Pair p = two_versions(
        "#include \"foo.h\"\nTEST(S, N) { acme::Foo f; f.a(); EXPECT_EQ(add(1, 2), 3); }\n",
        "#include \"foo.h\"\nTEST(S, N) { acme::Foo f; f.a(); acme::Foo g(2); EXPECT_EQ(add(1, 2), 3); EXPECT_TRUE(is_even(4)); }\n");
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.old.at("foo.h"); });
    (void)merge_one(merger, "foo.h", p.now.at("foo.h"), p.old.at("foo.h"));
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), p.old.at("foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_TRUE(contains(source.text(), "Foo::Foo(int /*arg1*/) {}")) << source.text();
    EXPECT_TRUE(contains(source.text(), "Foo::Foo() {}")) << "a default constructor is wanted once another exists";
    EXPECT_TRUE(contains(source.text(), "bool is_even(int /*arg1*/)"));
    EXPECT_EQ(source.result->added_members, 3u);
}

TEST(ImplMergeSource, AClassWithoutARegionInTheSourceIsRefused) {
    const Pair p = two_versions(kOneTest, kTwoTests);
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.now.at("foo.h"); });
    const Merged source = merge_one(merger, "foo.cpp", p.now.at("foo.cpp"), "#include \"foo.h\"\nunsigned int Foo::size() { return 3; }\n");
    EXPECT_FALSE(source.result.has_value());
    EXPECT_EQ(codes(source), std::vector<std::string>{"mt-merge-no-region"});
}

TEST(ImplMergeSource, IncludesOfTheSourceAreMergedToo) {
    const Pair p = two_versions(kOneTest, "#include \"foo.h\"\nTEST(S, N) { Foo foo(3); EXPECT_EQ(foo.size(), 3u); }\n");
    core::Config abort_policy;
    abort_policy.stub_policy = core::StubPolicy::Abort;
    const Files now = generate(kOneTest, {"foo.h"}, abort_policy);
    backend::ImplMerger merger("", [&](const std::string&) -> std::optional<std::string> { return p.now.at("foo.h"); });
    const Merged source = merge_one(merger, "foo.cpp", now.at("foo.cpp"), p.old.at("foo.cpp"));
    ASSERT_TRUE(source.result.has_value());
    EXPECT_TRUE(contains(source.text(), "#include <cstdlib>\n"));
    EXPECT_TRUE(contains(source.text(), "#include <stdexcept>\n")) << "the user's stays";
}

}  // namespace
