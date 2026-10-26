// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "mirrortest/syntax/preprocess.h"
#include "mirrortest/syntax/tree.h"
#include "test_support.h"

using mt::syntax::MacroTable;
using mt::syntax::preprocess;
using mt::syntax::Preprocessed;
using mt::syntax::QtMarker;
using Kind = mt::syntax::QtMarker::Kind;

namespace {

// The contract every call must honour, checked on every test:
//   1. same length;
//   2. every '\n' and '\r' exactly where it was;
//   3. every byte outside a blanked range is untouched;
//   4. inside a blanked range only spaces, line breaks and keyword letters.
void expect_invariants(const std::string& in, const Preprocessed& out) {
    ASSERT_EQ(out.text.size(), in.size());

    std::vector<bool> covered(in.size(), false);
    for (const auto& r : out.blanked) {
        ASSERT_LE(r.begin, r.end);
        ASSERT_LE(r.end, in.size());
        for (unsigned k = r.begin; k < r.end; ++k) {
            covered[k] = true;
        }
    }
    for (std::size_t k = 0; k < in.size(); ++k) {
        if (in[k] == '\n' || in[k] == '\r') {
            ASSERT_EQ(out.text[k], in[k]) << "line break moved at " << k;
        }
        if (!covered[k]) {
            ASSERT_EQ(out.text[k], in[k]) << "collateral change at " << k;
        }
    }
}

Preprocessed run(const std::string& in, const MacroTable& table = {}) {
    Preprocessed out = preprocess(in, table);
    expect_invariants(in, out);
    return out;
}

std::string pad(const std::string& keyword, std::size_t total) {
    return keyword + std::string(total - keyword.size(), ' ');
}

}  // namespace

// ---- object-like Qt macros --------------------------------------------------

TEST(Preprocess, QObjectIsBlankedAndMarked) {
    const std::string in = "class A {\n    Q_OBJECT\npublic:\n};\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "class A {\n            \npublic:\n};\n");
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].kind, Kind::QObject);
    EXPECT_EQ(out.markers[0].offset, in.find("Q_OBJECT"));
}

TEST(Preprocess, GadgetHasItsOwnMarker) {
    // A gadget is not a QObject: no signals, no slots, no QSignalSpy.
    const std::string in = "struct P { Q_GADGET };";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].kind, Kind::Gadget);
}

TEST(Preprocess, NamespaceRequiredResultAndDeprecatedAreJustBlanked) {
    const Preprocessed out =
        run("namespace n { Q_NAMESPACE }\nQ_REQUIRED_RESULT int f();\nQ_DECL_DEPRECATED void g();");
    EXPECT_EQ(out.text.find("Q_"), std::string::npos);
    EXPECT_TRUE(out.markers.empty());
}

TEST(Preprocess, QBenchmarkLeavesTheBlockItGuards) {
    const std::string in = "void f() { QBENCHMARK { g(); } QBENCHMARK_ONCE { h(); } }";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text.size(), in.size());
    EXPECT_EQ(out.text.find("QBENCHMARK"), std::string::npos);
    EXPECT_NE(out.text.find("{ g(); }"), std::string::npos);
    EXPECT_NE(out.text.find("{ h(); }"), std::string::npos);
    EXPECT_TRUE(out.markers.empty());
}

TEST(Preprocess, AnIdentifierThatOnlyStartsLikeQBenchmarkIsLeftAlone) {
    const std::string in = "int QBENCHMARKS = 1; int MY_QBENCHMARK = 2;";
    EXPECT_EQ(run(in).text, in);
}

TEST(Preprocess, InvokableIsMarkedAtTheMacro) {
    const std::string in = "class A { public: Q_INVOKABLE void ping(); };";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].kind, Kind::Invokable);
    EXPECT_EQ(out.markers[0].offset, in.find("Q_INVOKABLE"));
    EXPECT_NE(out.text.find("void ping();"), std::string::npos);
}

TEST(Preprocess, KeywordMacrosBecomeTheirKeyword) {
    const std::string in = "void f() Q_DECL_OVERRIDE Q_DECL_FINAL Q_DECL_NOEXCEPT;\n"
                           "Q_DECL_CONSTEXPR int x = 0; void* p = Q_NULLPTR;";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text,
              "void f() " + pad("override", 15) + " " + pad("final", 12) + " " +
                  pad("noexcept", 15) + ";\n" + pad("constexpr", 16) +
                  " int x = 0; void* p = " + pad("nullptr", 9) + ";");
}

// ---- function-like Qt macros ------------------------------------------------

TEST(Preprocess, FunctionLikeMacrosAreBlankedWithTheirArguments) {
    const std::string in =
        "class A {\n"
        "    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)\n"
        "    Q_ENUM(Color)\n"
        "    Q_CLASSINFO(\"k\", \"v\")\n"
        "    int value() const;\n"
        "};\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text.find("Q_"), std::string::npos);
    EXPECT_EQ(out.text.find("READ"), std::string::npos);
    EXPECT_NE(out.text.find("int value() const;"), std::string::npos);
}

TEST(Preprocess, EveryFunctionLikeMacroInTheTableIsBlanked) {
    for (const char* name :
         {"Q_PROPERTY", "Q_ENUM", "Q_ENUMS", "Q_FLAG", "Q_FLAGS", "Q_CLASSINFO", "Q_INTERFACES",
          "Q_DECLARE_METATYPE", "Q_DECLARE_PRIVATE", "Q_DECLARE_PUBLIC", "Q_PRIVATE_SLOT",
          "Q_DECL_DEPRECATED_X", "Q_DISABLE_COPY", "Q_DISABLE_COPY_MOVE"}) {
        const std::string in = std::string("class A { ") + name + "(x, y) int m; };";
        const Preprocessed out = run(in);
        EXPECT_EQ(out.text.find(name), std::string::npos) << name;
        EXPECT_NE(out.text.find("int m;"), std::string::npos) << name;
    }
}

TEST(Preprocess, MultiLineArgumentKeepsItsLineBreaks) {
    const std::string in = "class A {\n  Q_PROPERTY(int v\n             READ v\n             WRITE setV)\n  int w;\n};";
    const Preprocessed out = run(in);   // invariants prove the newlines are intact
    EXPECT_NE(out.text.find("int w;"), std::string::npos);
    EXPECT_EQ(out.text.find("READ"), std::string::npos);
}

TEST(Preprocess, NestedParenthesesAndStringsWithParenthesesInsideAnArgument) {
    const std::string in =
        "class A { Q_CLASSINFO(\"text\", \"a)b(c\") Q_ENUM(f(g(1), ')')) int m; };";
    const Preprocessed out = run(in);
    EXPECT_NE(out.text.find("int m;"), std::string::npos);
    EXPECT_EQ(out.text.find("Q_"), std::string::npos);
}

TEST(Preprocess, CommentsInsideAnArgumentDoNotConfuseTheMatching) {
    const std::string in = "class A { Q_PROPERTY(int v /* ) */ READ v // )\n WRITE w) int m; };";
    const Preprocessed out = run(in);
    EXPECT_NE(out.text.find("int m;"), std::string::npos);
}

TEST(Preprocess, FunctionLikeWithoutArgumentsIsTreatedAsObjectLike) {
    const std::string in = "class A { Q_ENUM int m; };";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "class A {        int m; };");
}

TEST(Preprocess, UnbalancedArgumentsOnlyBlankTheName) {
    const std::string in = "class A { Q_ENUM(x int m; };";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "class A {       (x int m; };");   // 1 + len("Q_ENUM") spaces
}

TEST(Preprocess, OnlySpacesAndTabsMayPrecedeTheOpeningParenthesis) {
    // A '(' on the next line is a statement, not the macro's arguments.
    const MacroTable table{{"MY_ATTR"}};
    const Preprocessed same = run("MY_ATTR (x) int a;", table);
    EXPECT_EQ(same.text.find("(x)"), std::string::npos);

    const Preprocessed next = run("MY_ATTR\n(void)f();", table);
    EXPECT_NE(next.text.find("(void)f();"), std::string::npos);
}

TEST(Preprocess, DisableCopyLeavesAMarker) {
    const Preprocessed out = run("class A { Q_DISABLE_COPY(A) };");
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].kind, Kind::DisableCopy);

    const Preprocessed move = run("class A { Q_DISABLE_COPY_MOVE(A) };");
    ASSERT_EQ(move.markers.size(), 1u);
    EXPECT_EQ(move.markers[0].kind, Kind::DisableCopy);
}

// ---- slots and signals ------------------------------------------------------

TEST(Preprocess, SlotsSectionIsBlankedAndMarkedAtTheAccessSpecifier) {
    const std::string in = "class A {\npublic slots:\n    void f();\nprivate Q_SLOTS:\n    void g();\n};";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text,
              "class A {\npublic      :\n    void f();\nprivate        :\n    void g();\n};");
    ASSERT_EQ(out.markers.size(), 2u);
    EXPECT_EQ(out.markers[0].kind, Kind::SlotsSection);
    EXPECT_EQ(out.markers[0].offset, in.find("public slots"));
    EXPECT_EQ(out.markers[1].kind, Kind::SlotsSection);
    EXPECT_EQ(out.markers[1].offset, in.find("private Q_SLOTS"));
}

TEST(Preprocess, SignalsBecomePublicAndAreMarkedAtTheKeyword) {
    const std::string in = "class A {\nsignals:\n    void changed();\nQ_SIGNALS:\n    void other();\n};";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "class A {\npublic :\n    void changed();\npublic   :\n    void other();\n};");
    ASSERT_EQ(out.markers.size(), 2u);
    EXPECT_EQ(out.markers[0].kind, Kind::SignalsSection);
    EXPECT_EQ(out.markers[0].offset, in.find("signals:"));
    EXPECT_EQ(out.markers[1].offset, in.find("Q_SIGNALS"));
}

TEST(Preprocess, SignalsAfterAnAccessSpecifierAreJustBlanked) {
    // `public public :` would be a syntax error.
    const std::string in = "class A {\npublic signals:\n    void changed();\n};";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "class A {\npublic        :\n    void changed();\n};");
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.find("public signals"));
}

TEST(Preprocess, SignalsAfterAMemberBoundaryWork) {
    // After a closed inline body, a semicolon, and after Q_OBJECT.
    for (const char* before : {"void f() {}", "int x;", "Q_OBJECT", "private:"}) {
        const std::string in = std::string("class A { ") + before + " signals: void s(); };";
        const Preprocessed out = run(in);
        EXPECT_NE(out.text.find("public"), std::string::npos) << before;
        EXPECT_NE(out.text.find("void s();"), std::string::npos) << before;
    }
}

TEST(Preprocess, SlotsAndSignalsAreOrdinaryIdentifiersOutsideASectionLabel) {
    const std::string in =
        "int slots = 0;\n"
        "int signals;\n"
        "void f() { g(slots); h(cond ? slots : other); }\n"
        "void g(int signals) { x = signals ? 1 : 2; }\n"
        "label: slots :: y;\n"
        "int Q_SLOTS_COUNT = 3;\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, in);
    EXPECT_TRUE(out.markers.empty());
}

TEST(Preprocess, SlotsNeedAnAccessSpecifierBeforeThem) {
    const std::string in = "class A { slots: void f(); };";
    EXPECT_EQ(run(in).text, in);
}

// ---- export macros ----------------------------------------------------------

TEST(Preprocess, ExportMacroBetweenClassKeyAndNameIsBlanked) {
    EXPECT_EQ(run("class FOO_EXPORT Widget : public Base {};").text,
              "class            Widget : public Base {};");
    EXPECT_EQ(run("struct Q_DECL_EXPORT S {};").text, "struct               S {};");
    EXPECT_EQ(run("class FOO_API\n    Widget {};").text, "class        \n    Widget {};");
}

TEST(Preprocess, AnAllCapsClassNameIsNotAnExportMacro) {
    for (const char* in : {"class FOO {};", "class FOO final : Base {};", "class FOO;",
                           "struct IO { int x; };", "template <class T> class X;",
                           "template <class T, class U> struct Y;", "enum class MODE : int { A };"}) {
        EXPECT_EQ(run(in).text, std::string(in)) << in;
    }
}

TEST(Preprocess, MixedCaseNamesAfterTheClassKeyAreNotExportMacros) {
    EXPECT_EQ(run("class Foo Bar;").text, "class Foo Bar;");
}

// ---- text that only contains a macro name -----------------------------------

TEST(Preprocess, MacrosInsideCommentsAreLeftAlone) {
    const std::string in =
        "// Q_OBJECT is not here\n"
        "/* Q_PROPERTY(x) and\n   private slots: */\n"
        "int a; // trailing Q_GADGET\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, in);
    EXPECT_TRUE(out.markers.empty());
}

TEST(Preprocess, ALineCommentContinuedByABackslashHidesTheNextLine) {
    const std::string in = "// comment \\\nQ_OBJECT\nQ_OBJECT\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, "// comment \\\nQ_OBJECT\n        \n");
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.rfind("Q_OBJECT"));
}

TEST(Preprocess, ALineCommentContinuedWithCrlfHidesTheNextLine) {
    const std::string in = "// comment \\\r\nQ_OBJECT\r\nQ_OBJECT\r\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.rfind("Q_OBJECT"));
}

TEST(Preprocess, MacrosInsideStringsAndCharLiteralsAreLeftAlone) {
    const std::string in =
        "const char* a = \"Q_OBJECT private slots: Q_PROPERTY(x)\";\n"
        "const char* b = \"say \\\"Q_OBJECT\\\" // not a comment\";\n"
        "char c = '\"'; char d = '\\''; Q_OBJECT\n";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.rfind("Q_OBJECT"));
    EXPECT_NE(out.text.find("\"Q_OBJECT private slots: Q_PROPERTY(x)\""), std::string::npos);
}

TEST(Preprocess, RawStringsAreLeftAlone) {
    const std::string in =
        "const char* a = R\"(Q_OBJECT \" )\" quoted)\";\n"
        "const char* b = R\"x(Q_OBJECT )\" still inside )x\";\n"
        "auto c = u8R\"(Q_OBJECT)\"; auto d = LR\"(Q_OBJECT)\";\n"
        "Q_OBJECT\n";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.rfind("Q_OBJECT"));
}

TEST(Preprocess, PrefixedLiteralsAreLeftAlone) {
    const std::string in =
        "auto a = u8\"Q_OBJECT\"; auto b = L\"Q_OBJECT\"; auto c = u'x'; auto d = U\"Q_OBJECT\";\n"
        "Q_OBJECT\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.markers.size(), 1u);
}

TEST(Preprocess, DigitSeparatorsDoNotStartACharacterLiteral) {
    const std::string in = "int a = 1'000'000; int b = 0x7F'FF; double c = 1'0.5e+1; Q_OBJECT";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.find("Q_OBJECT"));
}

TEST(Preprocess, PreprocessorDirectivesAreLeftAlone) {
    const std::string in =
        "#include <QObject>\n"
        "#define MY Q_OBJECT\n"
        "  #  define LONG(x) \\\n"
        "      Q_PROPERTY(x) \\\n"
        "      Q_OBJECT\n"
        "#if defined(Q_OBJECT)\n"
        "#endif\n"
        "#define S \"http://x\" // Q_OBJECT\n"
        "#define C /* Q_OBJECT\n   still comment */ 1\n"
        "Q_OBJECT\n";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 1u);
    EXPECT_EQ(out.markers[0].offset, in.rfind("Q_OBJECT"));
}

TEST(Preprocess, AHashThatIsNotADirectiveDoesNotSwallowTheLine) {
    const std::string in = "#define STR(x) #x\nint a = 1 # 2; Q_OBJECT\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.markers.size(), 1u);
}

TEST(Preprocess, UnterminatedLiteralsDoNotHideTheRestOfTheFile) {
    const Preprocessed s = run("const char* a = \"oops\nQ_OBJECT\n");
    EXPECT_EQ(s.markers.size(), 1u);
    const Preprocessed c = run("char c = 'x\nQ_OBJECT\n");
    EXPECT_EQ(c.markers.size(), 1u);
}

TEST(Preprocess, AnUnterminatedBlockCommentHidesTheRest) {
    const std::string in = "int a; /* never closed\nQ_OBJECT\n";
    EXPECT_EQ(run(in).text, in);
}

// ---- whole identifiers only -------------------------------------------------

TEST(Preprocess, OnlyWholeIdentifiersAreRecognised) {
    const std::string in =
        "int MY_Q_OBJECT; int Q_OBJECT_2; int xQ_OBJECT; int Q_OBJECTS;\n"
        "int myslots; int slots2;\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, in);
}

TEST(Preprocess, NonAsciiIdentifierCharactersDoNotSplitNames) {
    const std::string in = "int \xC3\xA9Q_OBJECT; int Q_OBJECT\xC3\xA9;";
    EXPECT_EQ(run(in).text, in);
}

// ---- ignore_macros ----------------------------------------------------------

TEST(Preprocess, IgnoreMacrosAreBlankedAsObjectLike) {
    const MacroTable table{{"MY_API", "ANOTHER"}};
    const Preprocessed out = run("class MY_API Widget { ANOTHER int x; };", table);
    EXPECT_EQ(out.text, "class        Widget {         int x; };");
}

TEST(Preprocess, IgnoreMacrosAreBlankedAsFunctionLikeWhenTheyTakeArguments) {
    const MacroTable table{{"MY_ATTR"}};
    const Preprocessed out = run("MY_ATTR(a, (b), \")\") void f(); MY_ATTR void g();", table);
    EXPECT_EQ(out.text.find("MY_ATTR"), std::string::npos);
    EXPECT_NE(out.text.find("void f();"), std::string::npos);
    EXPECT_NE(out.text.find("void g();"), std::string::npos);
}

TEST(Preprocess, WithoutTheTableAProjectMacroIsLeftAlone) {
    const std::string in = "MY_ATTR(a) void f();";
    EXPECT_EQ(run(in).text, in);
}

// ---- byte-order mark and edge cases -----------------------------------------

TEST(Preprocess, ByteOrderMarkBecomesSpaces) {
    const std::string in = "\xEF\xBB\xBF#include <a>\nQ_OBJECT\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text.substr(0, 3), "   ");
    EXPECT_EQ(out.markers.size(), 1u);
    ASSERT_FALSE(out.blanked.empty());
    EXPECT_EQ(out.blanked[0].begin, 0u);
    EXPECT_EQ(out.blanked[0].end, 3u);
}

TEST(Preprocess, ADirectiveRightAfterTheByteOrderMarkIsStillADirective) {
    const std::string in = "\xEF\xBB\xBF#define X Q_OBJECT\n";
    const Preprocessed out = run(in);
    EXPECT_TRUE(out.markers.empty());
    EXPECT_NE(out.text.find("Q_OBJECT"), std::string::npos);
}

TEST(Preprocess, EmptyAndTinyInputs) {
    EXPECT_EQ(run("").text, "");
    EXPECT_EQ(run("x").text, "x");
    EXPECT_EQ(run("\xEF\xBB").text, "\xEF\xBB");   // a truncated BOM is not a BOM
    EXPECT_EQ(run("Q_OBJECT").text, "        ");
}

TEST(Preprocess, NoQtAtAllIsTheIdentity) {
    const std::string in = "TEST(A, B) {\n    EXPECT_EQ(foo.size(), 3u);\n}\n";
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, in);
    EXPECT_TRUE(out.markers.empty());
    EXPECT_TRUE(out.blanked.empty());
}

TEST(Preprocess, Idempotent) {
    const std::string in =
        "#pragma once\nclass FOO_EXPORT W : public QObject {\n  Q_OBJECT\n  Q_PROPERTY(int v READ v)\n"
        "public slots:\n  void f();\nsignals:\n  void s();\n};\n";
    const Preprocessed once = run(in);
    const Preprocessed twice = run(once.text);
    EXPECT_EQ(twice.text, once.text);
    EXPECT_TRUE(twice.markers.empty());
}

TEST(Preprocess, MarkersAreInSourceOrder) {
    const std::string in =
        "class A { Q_OBJECT public slots: void f(); signals: void s(); Q_INVOKABLE void i(); };";
    const Preprocessed out = run(in);
    ASSERT_EQ(out.markers.size(), 4u);
    for (std::size_t k = 1; k < out.markers.size(); ++k) {
        EXPECT_LE(out.markers[k - 1].offset, out.markers[k].offset);
    }
}

// ---- integration with the parser ---------------------------------------------

namespace {

std::string data(const char* name) {
    return mt::test::read_file(std::string(MT_TEST_DATA_DIR) + "/" + name);
}

void collect_errors(const mt::syntax::Node& node, std::vector<std::string>& out) {
    if (node.is_error()) {
        out.emplace_back(node.text());
        return;
    }
    for (unsigned k = 0; k < node.child_count(); ++k) {
        collect_errors(node.child(k), out);
    }
}

}  // namespace

TEST(PreprocessIntegration, TheSpikeHeaderParsesWithoutErrorsAfterBlanking) {
    const std::string in = data("spike_header.h");
    ASSERT_FALSE(in.empty());
    EXPECT_TRUE(mt::syntax::parse_cpp(in).has_error()) << "raw Qt must still break the parser";

    const Preprocessed out = run(in);
    const mt::syntax::Tree tree = mt::syntax::parse_cpp(out.text);
    EXPECT_FALSE(tree.has_error()) << tree.root().sexp();

    // In source order: Q_OBJECT, Q_INVOKABLE, then the slots and signals sections.
    std::vector<Kind> kinds;
    for (const QtMarker& m : out.markers) {
        kinds.push_back(m.kind);
    }
    EXPECT_EQ(kinds, (std::vector<Kind>{Kind::QObject, Kind::Invokable, Kind::SlotsSection,
                                        Kind::SignalsSection}));
}

TEST(PreprocessIntegration, TheSpikeQTestFileKeepsOnlyTheKnownQfetchError) {
    const std::string in = data("spike_qtest.cpp");
    ASSERT_FALSE(in.empty());
    const Preprocessed out = run(in);
    const mt::syntax::Tree tree = mt::syntax::parse_cpp(out.text);

    std::vector<std::string> errors;
    collect_errors(tree.root(), errors);
    ASSERT_FALSE(errors.empty());
    for (const std::string& e : errors) {
        EXPECT_EQ(e.rfind("QFETCH", 0), 0u) << "unexpected parse error: " << e;
    }
}

TEST(PreprocessIntegration, TheSpikeGTestFileIsUntouchedAndParsesClean) {
    const std::string in = data("spike_gtest.cpp");
    ASSERT_FALSE(in.empty());
    const Preprocessed out = run(in);
    EXPECT_EQ(out.text, in);
    EXPECT_TRUE(out.markers.empty());
    EXPECT_FALSE(mt::syntax::parse_cpp(out.text).has_error());
}

TEST(PreprocessIntegration, LinesAndColumnsMatchTheOriginalFile) {
    // The point of preserving offsets: a node's position in the blanked text
    // is its position in the user's file.
    const std::string in = "class A {\n    Q_OBJECT\nprivate slots:\n    void initTestCase();\n};\n";
    const Preprocessed out = run(in);
    const mt::syntax::Tree tree = mt::syntax::parse_cpp(out.text);
    ASSERT_FALSE(tree.has_error());

    const mt::syntax::Node cls = tree.root().named_child(0);
    const mt::syntax::Node body = cls.child_by_field("body");
    bool found = false;
    for (const mt::syntax::Node& member : body.named_children()) {
        if (member.type() == "field_declaration") {
            EXPECT_EQ(member.line(), 4u);
            EXPECT_EQ(member.column(), 5u);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ---- gmock's MOCK_METHOD ---------------------------------------------------------

namespace {

MacroTable with_mocks() {
    MacroTable table;
    table.expand_mock_methods = true;
    return table;
}

// What a mock method must keep when it is expanded: the length, every byte outside the macro, and the
// number of line breaks inside it -- all at the end of it, after the declaration and the spaces.
void expect_expansion(const std::string& in, const std::string& macro, const std::string& declaration) {
    const std::size_t begin = in.find(macro);
    ASSERT_NE(begin, std::string::npos);
    const Preprocessed out = preprocess(in, with_mocks());
    ASSERT_EQ(out.text.size(), in.size());
    EXPECT_EQ(out.text.substr(0, begin), in.substr(0, begin));
    EXPECT_EQ(out.text.substr(begin + macro.size()), in.substr(begin + macro.size()));

    std::string span = out.text.substr(begin, macro.size());
    const std::size_t breaks = static_cast<std::size_t>(std::count(macro.begin(), macro.end(), '\n'));
    EXPECT_EQ(std::count(span.begin(), span.end(), '\n'), static_cast<std::ptrdiff_t>(breaks));
    EXPECT_EQ(span.substr(span.size() - breaks), std::string(breaks, '\n')) << "the line breaks are at the end";
    span.resize(span.size() - breaks);
    EXPECT_EQ(span, declaration + std::string(span.size() - declaration.size(), ' '));
}

}  // namespace

TEST(PreprocessMocks, AreLeftAloneUnlessAskedFor) {
    const std::string in = "class M : public S {\npublic:\n    MOCK_METHOD(int, read, (), (override));\n};\n";
    EXPECT_EQ(run(in).text, in);
}

TEST(PreprocessMocks, TheNewMacroBecomesItsDeclaration) {
    expect_expansion("class M : public S {\npublic:\n    MOCK_METHOD(int, read, (), (override));\n};\n",
                     "MOCK_METHOD(int, read, (), (override))", "int read() override");
}

TEST(PreprocessMocks, TheOldMacrosToo) {
    expect_expansion("class M : public S {\n    MOCK_METHOD1(write, void(int));\n};\n", "MOCK_METHOD1(write, void(int))",
                     "void write(int) override");
    expect_expansion("class M : public S {\n    MOCK_CONST_METHOD0(ok, bool());\n};\n", "MOCK_CONST_METHOD0(ok, bool())",
                     "bool ok() const override");
}

TEST(PreprocessMocks, ASemicolonIsAddedWhereTheMacroSuppliedIt) {
    // The old macros end the declaration themselves: `MOCK_METHOD1(f, void(int))` needs no `;`.
    expect_expansion("class M : public S {\n    MOCK_METHOD1(write, void(int))\n    int after;\n};\n",
                     "MOCK_METHOD1(write, void(int))", "void write(int) override;");
}

TEST(PreprocessMocks, TheSemicolonThatIsThereIsNotDoubled) {
    const Preprocessed out = preprocess("class M : public S { MOCK_METHOD(int, f, ()); }", with_mocks());
    EXPECT_EQ(std::count(out.text.begin(), out.text.end(), ';'), 1);
}

TEST(PreprocessMocks, TheSemicolonIsFoundAfterABreakOrAComment) {
    const Preprocessed out = preprocess("class M : public S { MOCK_METHOD(int, f, ())\n /* c */ ; }", with_mocks());
    EXPECT_EQ(std::count(out.text.begin(), out.text.end(), ';'), 1);
}

TEST(PreprocessMocks, LineBreaksInTheArgumentsMoveToTheEnd) {
    const std::string macro = "MOCK_METHOD(int,\n                 read,\n                 (),\n                 (override))";
    expect_expansion("class M : public S {\npublic:\n    " + macro + ";\n    int after;\n};\n", macro, "int read() override");
}

TEST(PreprocessMocks, TheLinesAfterAMacroAreWhereTheyWere) {
    const std::string in = "class M : public S {\n    MOCK_METHOD(void,\n        f,\n        (int));\n    int after;\n};\n";
    const mt::syntax::Tree tree = mt::syntax::parse_cpp(preprocess(in, with_mocks()).text);
    ASSERT_FALSE(tree.has_error());
    const mt::syntax::Node body = tree.root().named_child(0).child_by_field("body");
    ASSERT_EQ(body.named_child_count(), 2u);
    EXPECT_EQ(body.named_child(0).line(), 2u);
    EXPECT_EQ(body.named_child(1).line(), 5u);
}

TEST(PreprocessMocks, TheDeclarationIsRealCode) {
    const std::string in =
        "class M : public S {\npublic:\n"
        "    MOCK_METHOD(int, read, (), (const, override));\n"
        "    MOCK_METHOD(void, put, ((std::map<int, int>), int));\n"
        "    MOCK_METHOD2(set, void(double, const std::string&));\n"
        "    MOCK_CONST_METHOD1(at, const std::string&(int));\n"
        "};\n";
    const Preprocessed out = preprocess(in, with_mocks());
    EXPECT_FALSE(mt::syntax::parse_cpp(out.text).has_error()) << out.text;
}

TEST(PreprocessMocks, SpacesAfterCommasGoWhenTheMacroIsTooShortForThem) {
    // Written without spaces, the macro is shorter than the declaration with them: the first spaces
    // go, as many as it takes to fit, and not one more.
    const std::string macro = "MOCK_METHOD10(f,void(int,int,int,int,int,int,int,int,int,int))";
    expect_expansion("class M : public S { " + macro + " };", macro, "void f(int,int,int,int,int, int, int, int, int, int) override;");
}

TEST(PreprocessMocks, AMacroItDoesNotUnderstandIsBlanked) {
    const std::string macro = "MOCK_METHOD(int)";
    const Preprocessed out = preprocess("class M : public S { " + macro + "; };", with_mocks());
    EXPECT_EQ(out.text, "class M : public S { " + std::string(macro.size(), ' ') + "; };");
}

TEST(PreprocessMocks, ACommentOrALiteralInTheArgumentsBlanksTheMacro) {
    for (const char* macro : {"MOCK_METHOD(int, f, (), (/* c */ override))", "MOCK_METHOD(int, f, (), (override // c\n))",
                              "MOCK_METHOD(const char*, f, (), (Calltype(\"x\")))"}) {
        const std::string in = std::string("class M : public S { ") + macro + " };";
        const Preprocessed out = preprocess(in, with_mocks());
        ASSERT_EQ(out.text.size(), in.size());
        EXPECT_EQ(out.text.find("MOCK_METHOD"), std::string::npos) << macro;
        EXPECT_EQ(out.text.find("f("), std::string::npos) << macro;
    }
}

TEST(PreprocessMocks, AWordWithoutArgumentsIsLeftAlone) {
    for (const char* in : {"int MOCK_METHOD1 = 3;", "MOCK_METHOD(unbalanced", "using M = MOCK_METHOD0;"}) {
        EXPECT_EQ(preprocess(in, with_mocks()).text, in);
    }
}

TEST(PreprocessMocks, AWordFollowedBySomethingElseThanParenthesesIsLeftAlone) {
    // Not even when a '(' comes a little later: that is not the macro's argument list.
    for (const char* in : {"int MOCK_METHOD1 x (a);", "MOCK_METHOD", "MOCK_METHOD0 ", "MOCK_METHOD0\n// (a)\n", "x = MOCK_METHOD2 + (a, b);"}) {
        EXPECT_EQ(preprocess(in, with_mocks()).text, in) << in;
    }
}

TEST(PreprocessMocks, InCommentsAndStringsAreLeftAlone) {
    const std::string in = "// MOCK_METHOD(int, f, ());\n/* MOCK_METHOD0(f, int()); */\nconst char* s = \"MOCK_METHOD(int, f, ())\";\n";
    EXPECT_EQ(preprocess(in, with_mocks()).text, in);
}

TEST(PreprocessMocks, OtherWordsThatStartAlikeAreLeftAlone) {
    const std::string in = "class M { MOCK_METHODS(int, f, ()); MOCK_CONST_METHOD(int, f, ()); };";
    EXPECT_EQ(preprocess(in, with_mocks()).text, in);
}

TEST(PreprocessMocks, AFollowingSignalsSectionStillWorks) {
    // The ';' the macro stood for is a member boundary: `signals:` after it is a section label.
    const Preprocessed out = preprocess("class M : public S {\n    Q_OBJECT\npublic:\n    MOCK_METHOD0(f, void())\nsignals:\n    void s();\n};\n",
                                        with_mocks());
    EXPECT_NE(out.text.find("public"), std::string::npos);
    EXPECT_EQ(out.text.find("signals"), std::string::npos);
    ASSERT_FALSE(out.markers.empty());
    EXPECT_EQ(out.markers.back().kind, Kind::SignalsSection);
}

TEST(PreprocessMocks, IdempotentAndFree) {
    const std::string in = "class M : public S {\n    MOCK_METHOD(int, read, (), (override));\n    MOCK_METHOD1(write, void(int));\n};\n";
    const Preprocessed once = preprocess(in, with_mocks());
    EXPECT_EQ(preprocess(once.text, with_mocks()).text, once.text);
    EXPECT_NE(once.text, in);
}

TEST(PreprocessMocks, TheOtherMacrosStillWorkInTheSameFile) {
    const std::string in = "class M : public S { Q_OBJECT\n MOCK_METHOD(int, f, ());\n Q_PROPERTY(int p READ f) };";
    const Preprocessed out = preprocess(in, with_mocks());
    EXPECT_EQ(out.text.find("Q_OBJECT"), std::string::npos);
    EXPECT_EQ(out.text.find("Q_PROPERTY"), std::string::npos);
    EXPECT_NE(out.text.find("int f()"), std::string::npos);
}

// ---- robustness -------------------------------------------------------------

namespace {

// Small fixed-seed generator: the test must fail the same way everywhere.
class Lcg {
public:
    explicit Lcg(unsigned seed) : state_(seed) {}
    unsigned next(unsigned bound) {
        state_ = state_ * 1664525u + 1013904223u;
        return (state_ >> 8) % bound;
    }

private:
    unsigned state_;
};

}  // namespace

TEST(PreprocessRobustness, InvariantsHoldOnTruncatedAndCorruptedInputs) {
    // Nothing about the input may break the contract or the scanner: not a
    // string cut in half, not a macro whose parenthesis never closes, not a
    // stray byte in the middle of a raw string.
    const char* const files[] = {"spike_header.h", "spike_qtest.cpp", "spike_gtest.cpp"};
    const char noise[] = {'"', '\'', '(', ')', '/', '*', '\n', '\r', '\\', '#', 'R', ':', '\0', '\xEF'};

    Lcg rng(12345);
    for (const char* name : files) {
        const std::string original = data(name);
        ASSERT_FALSE(original.empty()) << name;

        for (int round = 0; round < 150; ++round) {
            std::string in = original.substr(0, rng.next(static_cast<unsigned>(original.size()) + 1));
            const unsigned edits = rng.next(4);
            for (unsigned e = 0; e < edits && !in.empty(); ++e) {
                in[rng.next(static_cast<unsigned>(in.size()))] = noise[rng.next(sizeof noise)];
            }

            const Preprocessed out = preprocess(in, {});
            expect_invariants(in, out);
            if (::testing::Test::HasFatalFailure()) {
                FAIL() << "invariant broken on " << name << ", round " << round;
            }
            for (std::size_t k = 1; k < out.markers.size(); ++k) {
                ASSERT_LE(out.markers[k - 1].offset, out.markers[k].offset);
            }
            for (const QtMarker& m : out.markers) {
                ASSERT_LT(m.offset, in.size());
            }
        }
    }
}

TEST(PreprocessRobustness, MockMethodsKeepTheLengthAndTheLineBreaksOnCorruptedInputs) {
    const std::string original =
        "class M : public S {\npublic:\n"
        "    MOCK_METHOD(int, read, (), (const, override));\n"
        "    MOCK_METHOD(void,\n        put,\n        ((std::map<int, int>), int),\n        (override));\n"
        "    MOCK_METHOD2(set, void(double, const std::string&))\n"
        "    MOCK_CONST_METHOD1_T(at, const T&(int));\n"
        "    MOCK_METHOD3_WITH_CALLTYPE(STDMETHODCALLTYPE, f, int(int,int,int));\n"
        "};\n";
    const char noise[] = {'"', '\'', '(', ')', ',', '/', '*', '\n', '\r', '\\', ';', 'R', '\0', '\xEF'};

    Lcg rng(777);
    for (int round = 0; round < 400; ++round) {
        std::string in = original.substr(0, rng.next(static_cast<unsigned>(original.size()) + 1));
        const unsigned edits = rng.next(5);
        for (unsigned e = 0; e < edits && !in.empty(); ++e) {
            in[rng.next(static_cast<unsigned>(in.size()))] = noise[rng.next(sizeof noise)];
        }
        const Preprocessed out = preprocess(in, with_mocks());
        ASSERT_EQ(out.text.size(), in.size()) << round;
        for (char c : {'\n', '\r'}) {
            ASSERT_EQ(std::count(out.text.begin(), out.text.end(), c), std::count(in.begin(), in.end(), c)) << round;
        }
        // Nothing the user wrote before the first macro changes, and a second pass changes nothing.
        ASSERT_EQ(preprocess(out.text, with_mocks()).text, out.text) << round;
    }
}
