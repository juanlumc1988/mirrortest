// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The implementation backend: which header holds what, what each file
// includes, how declarations and stubs look, and the text that comes out of the
// templates.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/backend/impl_backend.h"
#include "mirrortest/frontend/pipeline.h"
#include "mirrortest/ir/type.h"

using namespace mt;

namespace {

using Strings = std::vector<std::string>;

// ---- building models ---------------------------------------------------------------------------------------------

ir::Param param(const char* type, const char* name) {
    ir::Param p;
    p.type = ir::type_from_spelling(type);
    p.name = name;
    return p;
}

ir::Method method(const char* name, const char* ret, std::vector<ir::Param> params = {}, bool is_const = false) {
    ir::Method m;
    m.name = name;
    m.ret = ir::type_from_spelling(ret);
    m.params = std::move(params);
    m.is_const = is_const;
    return m;
}

ir::Method ctor(const char* cls, std::vector<ir::Param> params = {}) {
    ir::Method m;
    m.name = cls;
    m.kind = ir::MethodKind::Constructor;
    m.params = std::move(params);
    return m;
}

ir::Field field(const char* type, const char* name) {
    ir::Field f;
    f.type = ir::type_from_spelling(type);
    f.name = name;
    return f;
}

ir::Class klass(const char* name, std::vector<std::string> ns = {}, std::vector<ir::Method> methods = {}) {
    ir::Class c;
    c.name = name;
    c.ns = std::move(ns);
    c.methods = std::move(methods);
    return c;
}

ir::Function function(const char* name, const char* ret, std::vector<ir::Param> params = {}, std::vector<std::string> ns = {}) {
    ir::Function f;
    f.name = name;
    f.ns = std::move(ns);
    f.ret = ir::type_from_spelling(ret);
    f.params = std::move(params);
    return f;
}

struct Generated {
    std::vector<backend::ImplViews> views;
    core::Diagnostics               diagnostics;
};

Generated build(const ir::Model& model, backend::ImplOptions options = {}) {
    Generated g;
    g.views = backend::build_impl_views(model, options, g.diagnostics);
    return g;
}

backend::ImplOptions with_headers(Strings headers) {
    backend::ImplOptions o;
    o.source = "tests/t.cpp";
    o.headers_to_generate = std::move(headers);
    return o;
}

Strings header_paths(const Generated& g) {
    Strings out;
    for (const backend::ImplViews& v : g.views) out.push_back(v.header_path);
    return out;
}

const backend::ImplViews* view_of(const Generated& g, const std::string& header) {
    for (const backend::ImplViews& v : g.views) {
        if (v.header_path == header) return &v;
    }
    ADD_FAILURE() << "no header " << header;
    return nullptr;
}

Strings codes(const Generated& g) {
    Strings out;
    for (const core::Diagnostic& d : g.diagnostics.sorted()) out.push_back(d.code);
    return out;
}

// The classes a header view declares, in order, as "ns::Name".
Strings class_heads(const nlohmann::json& header_view) {
    Strings out;
    for (const auto& section : header_view["sections"]) {
        for (const auto& block : section["blocks"]) {
            if (block["kind"] == "class") out.push_back(block["head"].get<std::string>());
        }
    }
    return out;
}

// Every declaration line of a class block, in order.
Strings member_lines(const nlohmann::json& header_view, const char* head) {
    Strings out;
    for (const auto& section : header_view["sections"]) {
        for (const auto& block : section["blocks"]) {
            if (block["kind"] != "class" || block["head"] != head) continue;
            for (const auto& m : block["members"]) out.push_back(m["line"].get<std::string>());
        }
    }
    return out;
}

Strings includes_of(const nlohmann::json& view) {
    Strings out;
    for (const auto& line : view["includes"]) out.push_back(line.get<std::string>());
    return out;
}

// All the lines of every definition of a source view.
Strings definition_lines(const nlohmann::json& source_view) {
    Strings out;
    for (const auto& section : source_view["sections"]) {
        for (const auto& block : section["blocks"]) {
            for (const auto& d : block["definitions"]) {
                for (const auto& line : d["lines"]) out.push_back(line.get<std::string>());
            }
        }
    }
    return out;
}

// ---- which header gets what -------------------------------------------------------------------------------------

TEST(ImplBackendAssign, AClassGoesToTheHeaderWhoseNameMatches) {
    ir::Model model;
    model.classes = {klass("FooBar"), klass("Baz")};
    const Generated g = build(model, with_headers({"baz.h", "acme/foo_bar.h"}));
    EXPECT_EQ(class_heads(view_of(g, "acme/foo_bar.h")->header_view), Strings{"class FooBar {"});
    EXPECT_EQ(class_heads(view_of(g, "baz.h")->header_view), Strings{"class Baz {"});
    EXPECT_TRUE(g.diagnostics.empty());
}

TEST(ImplBackendAssign, TheMatchIgnoresCaseUnderscoresAndHyphens) {
    ir::Model model;
    model.classes = {klass("HTTPServer"), klass("RingBuffer")};
    const Generated g = build(model, with_headers({"ring-buffer.h", "httpserver.h"}));
    EXPECT_EQ(class_heads(view_of(g, "httpserver.h")->header_view), Strings{"class HTTPServer {"});
    EXPECT_EQ(class_heads(view_of(g, "ring-buffer.h")->header_view), Strings{"class RingBuffer {"});
}

TEST(ImplBackendAssign, AClassWithoutAPairGoesToTheFirstHeaderTheTestAsksFor) {
    ir::Model model;
    model.classes = {klass("Foo"), klass("Helper")};
    const Generated g = build(model, with_headers({"foo.h", "other.h"}));
    EXPECT_EQ(class_heads(view_of(g, "foo.h")->header_view), (Strings{"class Foo {", "class Helper {"}));
    EXPECT_TRUE(class_heads(view_of(g, "other.h")->header_view).empty());
    EXPECT_TRUE(g.diagnostics.empty());
}

TEST(ImplBackendAssign, WithNoHeaderToGenerateEachGetsItsOwnAndAWarning) {
    ir::Model model;
    model.classes = {klass("HTTPServer", {"acme"})};
    model.functions = {function("clamp", "int")};
    const Generated g = build(model, with_headers({}));
    EXPECT_EQ(header_paths(g), (Strings{"http_server.h", "clamp.h"}));
    EXPECT_EQ(codes(g), (Strings{"mt-no-include", "mt-no-include"}));
    const std::string first = g.diagnostics.sorted()[0].message;
    EXPECT_NE(first.find("acme::HTTPServer"), std::string::npos) << first;
    EXPECT_NE(first.find("http_server.h"), std::string::npos) << first;
}

TEST(ImplBackendAssign, TheNamingPatternDecidesTheFallbackName) {
    ir::Model model;
    model.classes = {klass("RingBuffer")};
    backend::ImplOptions o = with_headers({});
    o.config.naming.header = "{stem}.hpp";
    const Generated g = build(model, o);
    EXPECT_EQ(header_paths(g), Strings{"ring_buffer.hpp"});
}

TEST(ImplBackendAssign, AnAnnotationNamesTheHeaderAndWins) {
    ir::Model model;
    ir::Class c = klass("Foo");
    c.header = "acme/special.h";
    model.classes = {c, klass("Bar")};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_EQ(header_paths(g), (Strings{"foo.h", "acme/special.h"}));
    EXPECT_EQ(class_heads(view_of(g, "acme/special.h")->header_view), Strings{"class Foo {"});
    EXPECT_EQ(class_heads(view_of(g, "foo.h")->header_view), Strings{"class Bar {"});
    EXPECT_TRUE(g.diagnostics.empty());
}

TEST(ImplBackendAssign, FunctionsFollowTheSameRules) {
    ir::Model model;
    model.functions = {function("clamp", "int"), function("lerp", "double")};
    const Generated g = build(model, with_headers({"util.h", "lerp.h"}));
    EXPECT_EQ(view_of(g, "lerp.h")->header_view["sections"][0]["blocks"][0]["members"].size(), 1u);
    EXPECT_EQ(view_of(g, "util.h")->header_view["sections"][0]["blocks"][0]["members"].size(), 1u);
}

TEST(ImplBackendAssign, AnUnclaimedHeaderIsStillGeneratedEmptySoTheTestCompiles) {
    ir::Model model;
    const Generated g = build(model, with_headers({"constants.h"}));
    ASSERT_EQ(g.views.size(), 1u);
    EXPECT_EQ(g.views[0].header_path, "constants.h");
    EXPECT_TRUE(g.views[0].header_view["sections"].empty());
    EXPECT_TRUE(g.views[0].source_path.empty());
}

TEST(ImplBackendAssign, RepeatedAndMessyHeaderPathsAreCleanedUp) {
    ir::Model model;
    model.classes = {klass("Foo")};
    const Generated g = build(model, with_headers({"./acme//foo.h", "acme/foo.h", ""}));
    EXPECT_EQ(header_paths(g), Strings{"acme/foo.h"});
}

TEST(ImplBackendAssign, AHeaderOutsideTheOutputDirectoryIsGeneratedByItsFileNameWithAWarning) {
    ir::Model model;
    model.classes = {klass("Foo")};
    for (const char* path : {"../common/foo.h", "/abs/foo.h", "a/../../foo.h"}) {
        const Generated g = build(model, with_headers({path}));
        EXPECT_EQ(header_paths(g), Strings{"foo.h"}) << path;
        EXPECT_EQ(codes(g), Strings{"mt-unsupported"}) << path;
    }
}

TEST(ImplBackendAssign, TheOrderOfTheHeadersIsTheOrderOfTheTestThenTheFallbacks) {
    ir::Model model;
    model.classes = {klass("Zed")};
    ir::Class annotated = klass("Lone");
    annotated.header = "lone.h";
    model.classes.push_back(annotated);
    const Generated g = build(model, with_headers({"b.h", "a.h"}));
    EXPECT_EQ(header_paths(g), (Strings{"b.h", "a.h", "lone.h"}));
}

// ---- sections and namespaces ---------------------------------------------------------------------------------

TEST(ImplBackendSections, NestedNamespacesAreNestedBlocksValidInCxx11) {
    ir::Model model;
    model.classes = {klass("Foo", {"acme", "core"})};
    const Generated g = build(model, with_headers({"foo.h"}));
    const auto& section = g.views[0].header_view["sections"][0];
    EXPECT_EQ(section["open"], (nlohmann::json{"namespace acme {", "namespace core {"}));
    EXPECT_EQ(section["close"], (nlohmann::json{"}  // namespace core", "}  // namespace acme"}));
}

TEST(ImplBackendSections, TheGlobalNamespaceHasNoLinesAround) {
    ir::Model model;
    model.classes = {klass("Foo")};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_TRUE(g.views[0].header_view["sections"][0]["open"].empty());
    EXPECT_TRUE(g.views[0].header_view["sections"][0]["close"].empty());
}

TEST(ImplBackendSections, ClassesOfDifferentNamespacesInOneHeaderGetOneSectionEachInOrderOfAppearance) {
    ir::Model model;
    model.classes = {klass("A", {"one"}), klass("B", {"two"}), klass("C", {"one"})};
    const Generated g = build(model, with_headers({"all.h"}));
    const auto& sections = g.views[0].header_view["sections"];
    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0]["open"][0], "namespace one {");
    EXPECT_EQ(sections[0]["blocks"].size(), 2u);   // A and C together
    EXPECT_EQ(sections[1]["open"][0], "namespace two {");
}

TEST(ImplBackendSections, FreeFunctionsAreOneRegionPerNamespaceLabelledByIt) {
    ir::Model model;
    model.functions = {function("a", "int", {}, {}), function("b", "int", {}, {"acme"})};
    const Generated g = build(model, with_headers({"f.h"}));
    const auto& sections = g.views[0].header_view["sections"];
    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0]["blocks"][0]["begin"], "// <mt:begin functions>");
    EXPECT_EQ(sections[1]["blocks"][0]["begin"], "// <mt:begin acme::functions>");
    EXPECT_EQ(sections[1]["blocks"][0]["end"], "// <mt:end acme::functions>");
}

// ---- classes ----------------------------------------------------------------------------------------------------------

TEST(ImplBackendClass, ConstructorsFirstThenMethodsInOrderThenFields) {
    ir::Class c = klass("Foo", {}, {method("size", "int"), ctor("Foo", {param("int", "arg1")}), method("empty", "bool"), ctor("Foo")});
    c.fields = {field("int", "x")};
    ir::Model model;
    model.classes = {c};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_EQ(member_lines(g.views[0].header_view, "class Foo {"),
              (Strings{"Foo(int arg1);", "Foo();", "int size();", "bool empty();", "int x;"}));
}

TEST(ImplBackendClass, ANonStructClassLabelsItsMembersPublicAndAStructDoesNot) {
    ir::Class c = klass("Foo", {}, {method("f", "void")});
    ir::Class s = klass("Point");
    s.is_struct = true;
    s.fields = {field("int", "x")};
    ir::Model model;
    model.classes = {c, s};
    const Generated g = build(model, with_headers({"all.h"}));
    const auto& blocks = g.views[0].header_view["sections"][0]["blocks"];
    EXPECT_EQ(blocks[0]["head"], "class Foo {");
    EXPECT_EQ(blocks[0]["access"], "public:");
    EXPECT_EQ(blocks[1]["head"], "struct Point {");
    EXPECT_EQ(blocks[1]["access"], "");
}

TEST(ImplBackendClass, NotesBecomeTodoCommentsBeforeTheirDeclaration) {
    ir::Method m = method("size", "unsigned int");
    m.notes = {"return type inferred from literal 3u (t.cpp:6)", "consider making this const"};
    ir::Model model;
    model.classes = {klass("Foo", {}, {m})};
    const Generated g = build(model, with_headers({"foo.h"}));
    const auto& member = g.views[0].header_view["sections"][0]["blocks"][0]["members"][0];
    EXPECT_EQ(member["notes"], (nlohmann::json{"// TODO(mt): return type inferred from literal 3u (t.cpp:6)", "// TODO(mt): consider making this const"}));
    EXPECT_EQ(member["line"], "unsigned int size();");
}

TEST(ImplBackendClass, FieldNotesAreKeptToo) {
    ir::Class s = klass("Point");
    s.is_struct = true;
    ir::Field f = field("int", "x");
    f.notes = {"type inferred from literal 5 (t.cpp:4)"};
    s.fields = {f};
    ir::Model model;
    model.classes = {s};
    const Generated g = build(model, with_headers({"point.h"}));
    EXPECT_EQ(g.views[0].header_view["sections"][0]["blocks"][0]["members"][0]["notes"][0], "// TODO(mt): type inferred from literal 5 (t.cpp:4)");
}

TEST(ImplBackendClass, RegionMarkersCarryTheQualifiedName) {
    ir::Model model;
    model.classes = {klass("Foo", {"acme"}, {method("f", "void")})};
    const Generated g = build(model, with_headers({"foo.h"}));
    const auto& block = g.views[0].header_view["sections"][0]["blocks"][0];
    EXPECT_EQ(block["begin"], "// <mt:begin acme::Foo>");
    EXPECT_EQ(block["end"], "// <mt:end acme::Foo>");
    const auto& src = g.views[0].source_view["sections"][0]["blocks"][0];
    EXPECT_EQ(src["begin"], "// <mt:begin acme::Foo>");
    EXPECT_EQ(src["end"], "// <mt:end acme::Foo>");
}

// ---- forward declarations and ordering --------------------------------------------------------------------

TEST(ImplBackendOrder, AClassNamingALaterOneGetsItForwardDeclared) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("get", "B"), method("put", "void", {param("const C&", "c")})}), klass("B"), klass("C")};
    model.classes[1].is_struct = true;
    const Generated g = build(model, with_headers({"all.h"}));
    const auto& blocks = g.views[0].header_view["sections"][0]["blocks"];
    ASSERT_EQ(blocks[0]["kind"], "forward");
    EXPECT_EQ(blocks[0]["lines"], (nlohmann::json{"struct B;", "class C;"}));
    EXPECT_EQ(class_heads(g.views[0].header_view), (Strings{"class A {", "struct B {", "class C {"}));
}

TEST(ImplBackendOrder, ALaterClassIsForwardDeclaredOnlyOnceHoweverOftenItIsNamed) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("c1", "C"), method("c2", "C")}), klass("B", {}, {method("c3", "C")}), klass("C")};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_EQ(g.views[0].header_view["sections"][0]["blocks"][0]["lines"], (nlohmann::json{"class C;"}));
}

TEST(ImplBackendOrder, NoForwardDeclarationWhenEverythingIsAlreadyDefined) {
    ir::Model model;
    model.classes = {klass("B"), klass("A", {}, {method("get", "B")})};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_NE(g.views[0].header_view["sections"][0]["blocks"][0]["kind"], "forward");
}

TEST(ImplBackendOrder, AByValueFieldNeedsItsClassCompleteSoItComesFirst) {
    ir::Class outer = klass("Outer");
    outer.is_struct = true;
    outer.fields = {field("Inner", "inner")};
    ir::Class inner = klass("Inner");
    inner.is_struct = true;
    inner.fields = {field("int", "x")};
    ir::Model model;
    model.classes = {outer, inner};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_EQ(class_heads(g.views[0].header_view), (Strings{"struct Inner {", "struct Outer {"}));
}

TEST(ImplBackendOrder, APointerOrReferenceFieldDoesNotReorder) {
    ir::Class outer = klass("Outer");
    outer.is_struct = true;
    outer.fields = {field("Inner*", "p"), field("Inner&", "r")};
    ir::Class inner = klass("Inner");
    inner.is_struct = true;
    ir::Model model;
    model.classes = {outer, inner};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_EQ(class_heads(g.views[0].header_view), (Strings{"struct Outer {", "struct Inner {"}));
}

TEST(ImplBackendOrder, ACycleOfValueFieldsIsLeftAsTheTestFirstUsedIt) {
    ir::Class a = klass("A");
    a.fields = {field("B", "b")};
    ir::Class b = klass("B");
    b.fields = {field("A", "a")};
    ir::Model model;
    model.classes = {a, b};
    const Generated g = build(model, with_headers({"all.h"}));
    ASSERT_EQ(class_heads(g.views[0].header_view).size(), 2u);   // no hang, nothing lost
}

TEST(ImplBackendOrder, ASuffixHasToStartAtANamespaceBoundary) {
    // `Item` in a signature is acme::Item, not acme::SubItem.
    ir::Model model;
    model.classes = {klass("User", {"acme"}, {method("get", "Item")}), klass("SubItem", {"acme"}), klass("Item", {"acme"})};
    const Generated g = build(model, with_headers({"all.h"}));
    const auto& blocks = g.views[0].header_view["sections"][0]["blocks"];
    ASSERT_EQ(blocks[0]["kind"], "forward");
    EXPECT_EQ(blocks[0]["lines"], (nlohmann::json{"class Item;"}));
}

// ---- includes --------------------------------------------------------------------------------------------------------

TEST(ImplBackendIncludes, StandardTypesAreIncludedSystemFirstAlphabetically) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("names", "std::vector<std::string>"), method("when", "std::chrono::seconds"),
                                       method("at", "std::uint8_t", {param("std::size_t", "i")})})};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_EQ(includes_of(g.views[0].header_view),
              (Strings{"#include <chrono>", "#include <cstddef>", "#include <cstdint>", "#include <string>", "#include <vector>"}));
}

TEST(ImplBackendIncludes, QtTypesAreTheirOwnHeader) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("name", "QString"), method("parent", "QObject*"), method("n", "qint64")})};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_EQ(includes_of(g.views[0].header_view), (Strings{"#include <QObject>", "#include <QString>", "#include <QtGlobal>"}));
}

TEST(ImplBackendIncludes, AClassOfAnotherGeneratedHeaderIsAQuotedIncludeAfterTheSystemOnes) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("bar", "Bar"), method("s", "std::string")}), klass("Bar")};
    const Generated g = build(model, with_headers({"foo.h", "acme/bar.h"}));
    EXPECT_EQ(includes_of(view_of(g, "foo.h")->header_view), (Strings{"#include <string>", "#include \"acme/bar.h\""}));
    EXPECT_TRUE(includes_of(view_of(g, "acme/bar.h")->header_view).empty());
}

TEST(ImplBackendIncludes, ANameOfTheSameHeaderNeedsNoInclude) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("bar", "Bar")}), klass("Bar")};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_TRUE(includes_of(g.views[0].header_view).empty());
}

TEST(ImplBackendIncludes, ATypeOfAnExistingHeaderIsIncludedFromThere) {
    ir::Model known;
    ir::Class item = klass("Item", {"acme"});
    item.header = "acme/item.h";
    known.classes = {item};
    ir::Enum mode;
    mode.name = "Mode";
    mode.ns = {"acme"};
    mode.header = "acme/mode.h";
    known.enums = {mode};

    ir::Model model;
    model.classes = {klass("Foo", {"acme"}, {method("get", "Item"), method("mode", "acme::Mode"), method("other", "Unrelated")})};
    backend::ImplOptions o = with_headers({"foo.h"});
    o.known = {known};
    const Generated g = build(model, o);
    EXPECT_EQ(includes_of(g.views[0].header_view), (Strings{"#include \"acme/item.h\"", "#include \"acme/mode.h\""}));
}

TEST(ImplBackendIncludes, TheSourceIncludesWhatItsStubsNeedAndItsOwnHeaderFirst) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};
    const Generated g = build(model, with_headers({"acme/foo.h"}));
    const nlohmann::json& s = g.views[0].source_view;
    EXPECT_EQ(s["own_include"], "#include \"acme/foo.h\"");
    EXPECT_EQ(includes_of(s), Strings{"#include <stdexcept>"});
}

TEST(ImplBackendIncludes, AbortNeedsCstdlibAndDefaultNeedsNothingUnlessItFallsBack) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};

    backend::ImplOptions abort = with_headers({"foo.h"});
    abort.config.stub_policy = core::StubPolicy::Abort;
    EXPECT_EQ(includes_of(build(model, abort).views[0].source_view), Strings{"#include <cstdlib>"});

    backend::ImplOptions no_exceptions = with_headers({"foo.h"});
    no_exceptions.config.no_exceptions = true;
    EXPECT_EQ(includes_of(build(model, no_exceptions).views[0].source_view), Strings{"#include <cstdlib>"});

    backend::ImplOptions def = with_headers({"foo.h"});
    def.config.stub_policy = core::StubPolicy::Default;
    EXPECT_TRUE(includes_of(build(model, def).views[0].source_view).empty());
}

TEST(ImplBackendIncludes, TwoHeadersThatIncludeEachOtherAreReportedOnce) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("b", "B")}), klass("B", {}, {method("a", "A")})};
    const Generated g = build(model, with_headers({"a.h", "b.h"}));
    ASSERT_EQ(codes(g), Strings{"mt-unsupported"});
    const std::string message = g.diagnostics.sorted()[0].message;
    EXPECT_NE(message.find("'a.h', 'b.h'"), std::string::npos) << message;
    EXPECT_NE(message.find("include each other"), std::string::npos) << message;
}

TEST(ImplBackendIncludes, ALongerCycleIsOneWarningNamingEveryHeader) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("b", "B")}), klass("B", {}, {method("c", "C")}), klass("C", {}, {method("a", "A")}),
                     klass("D", {}, {method("a", "A")})};
    const Generated g = build(model, with_headers({"a.h", "b.h", "c.h", "d.h"}));
    ASSERT_EQ(codes(g), Strings{"mt-unsupported"});
    EXPECT_NE(g.diagnostics.sorted()[0].message.find("'a.h', 'b.h', 'c.h'"), std::string::npos);
    EXPECT_EQ(g.diagnostics.sorted()[0].message.find("d.h"), std::string::npos);   // D depends on the cycle, it is not in it
}

TEST(ImplBackendIncludes, AChainOrADiamondIsNotACycle) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("b", "B"), method("c", "C")}), klass("B", {}, {method("d", "D")}),
                     klass("C", {}, {method("d", "D")}), klass("D")};
    const Generated g = build(model, with_headers({"a.h", "b.h", "c.h", "d.h"}));
    EXPECT_TRUE(g.diagnostics.empty());
}

TEST(ImplBackendIncludes, ClassesOfOneHeaderUsingEachOtherAreNotACycle) {
    ir::Model model;
    model.classes = {klass("A", {}, {method("b", "B")}), klass("B", {}, {method("a", "A")})};
    EXPECT_TRUE(build(model, with_headers({"all.h"})).diagnostics.empty());
}

// ---- definitions and stubs ---------------------------------------------------------------------------------------

TEST(ImplBackendStub, ThrowByDefaultWithTheSignatureInTheMessage) {
    ir::Model model;
    model.classes = {klass("Foo", {"acme"}, {ctor("Foo", {param("int", "arg1")}), method("size", "unsigned int", {}, true)})};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_EQ(definition_lines(g.views[0].source_view),
              (Strings{"Foo::Foo(int /*arg1*/) {}", "unsigned int Foo::size() const {",
                       "    throw std::logic_error(\"mirrortest: not implemented: acme::Foo::size() const\");", "}"}));
}

TEST(ImplBackendStub, AbortAndDefaultPolicies) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int"), method("g", "void")})};

    backend::ImplOptions abort = with_headers({"foo.h"});
    abort.config.stub_policy = core::StubPolicy::Abort;
    EXPECT_EQ(definition_lines(build(model, abort).views[0].source_view),
              (Strings{"int Foo::f() {", "    std::abort();", "}", "void Foo::g() {", "    std::abort();", "}"}));

    backend::ImplOptions def = with_headers({"foo.h"});
    def.config.stub_policy = core::StubPolicy::Default;
    EXPECT_EQ(definition_lines(build(model, def).views[0].source_view), (Strings{"int Foo::f() {", "    return {};", "}", "void Foo::g() {}"}));
}

TEST(ImplBackendStub, DefaultSaysOnceThatStubsMayPassAndFallsBackToAbortWhereItCannotReturnAnEmptyValue) {
    ir::Model model;
    ir::Method ref = method("ref", "const std::string&");
    ir::Method mystery = method("mystery", "Mystery");
    ir::Method fine = method("fine", "int");
    ir::Method fine2 = method("fine2", "int");
    ref.where = {"t.cpp", 4, 1};
    mystery.where = {"t.cpp", 5, 1};
    model.classes = {klass("Foo", {}, {ref, mystery, fine, fine2})};

    backend::ImplOptions o = with_headers({"foo.h"});
    o.config.stub_policy = core::StubPolicy::Default;
    const Generated g = build(model, o);
    // Sorted: the one without a location first.
    EXPECT_EQ(codes(g), (Strings{"mt-stub-may-pass", "mt-unsupported", "mt-unsupported"}));
    EXPECT_EQ(g.diagnostics.sorted()[1].where.line, 4u);
    EXPECT_EQ(g.diagnostics.sorted()[2].where.line, 5u);
    EXPECT_EQ(includes_of(g.views[0].source_view), Strings{"#include <cstdlib>"});
}

TEST(ImplBackendStub, TheConfigurationCanVouchForADefaultConstructibleType) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("mystery", "Mystery")})};
    backend::ImplOptions o = with_headers({"foo.h"});
    o.config.stub_policy = core::StubPolicy::Default;
    o.config.default_constructible_types = {"Mystery"};
    const Generated g = build(model, o);
    EXPECT_EQ(codes(g), Strings{"mt-stub-may-pass"});
    EXPECT_EQ(definition_lines(g.views[0].source_view), (Strings{"Mystery Foo::mystery() {", "    return {};", "}"}));
}

TEST(ImplBackendStub, ADefaultConstructibleGeneratedClassCanBeReturnedByBraces) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("make", "Bar")}), klass("Bar", {}, {ctor("Bar")})};
    backend::ImplOptions o = with_headers({"all.h"});
    o.config.stub_policy = core::StubPolicy::Default;
    const Generated g = build(model, o);
    EXPECT_EQ(codes(g), Strings{"mt-stub-may-pass"});
}

TEST(ImplBackendStub, DefaultSaysNothingWhenNothingIsStubbed) {
    // Constructors are empty under every policy and a struct has no code, so no
    // test can pass by way of a returned value.
    ir::Class s = klass("Point");
    s.is_struct = true;
    s.fields = {field("int", "x")};
    ir::Model model;
    model.classes = {s, klass("Foo", {}, {ctor("Foo", {param("int", "n")})})};
    backend::ImplOptions o = with_headers({"all.h"});
    o.config.stub_policy = core::StubPolicy::Default;
    EXPECT_TRUE(build(model, o).diagnostics.empty());
}

TEST(ImplBackendStub, OtherPoliciesDoNotWarn) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};
    EXPECT_TRUE(build(model, with_headers({"foo.h"})).diagnostics.empty());
}

TEST(ImplBackendStub, AFunctionIsDefinedLikeAMethodWithTheNamespaceInTheMessage) {
    ir::Model model;
    model.functions = {function("add", "int", {param("int", "arg1"), param("int", "arg2")}, {"acme"})};
    const Generated g = build(model, with_headers({"add.h"}));
    EXPECT_EQ(definition_lines(g.views[0].source_view),
              (Strings{"int add(int /*arg1*/, int /*arg2*/) {",
                       "    throw std::logic_error(\"mirrortest: not implemented: acme::add(int, int)\");", "}"}));
    EXPECT_EQ(g.views[0].source_view["sections"][0]["open"][0], "namespace acme {");
}

TEST(ImplBackendStub, MethodsThatAreNotDefinedAreNotStubbed) {
    ir::Method pure = method("run", "void");
    pure.is_pure = true;
    pure.is_virtual = true;
    ir::Method deleted = method("gone", "void");
    deleted.is_deleted = true;
    ir::Model model;
    model.classes = {klass("Foo", {}, {pure, deleted})};
    const Generated g = build(model, with_headers({"foo.h"}));
    EXPECT_TRUE(g.views[0].source_view.is_null());
    EXPECT_TRUE(g.views[0].source_path.empty());
    EXPECT_EQ(member_lines(g.views[0].header_view, "class Foo {"), (Strings{"virtual void run() = 0;", "void gone() = delete;"}));
}

TEST(ImplBackendStub, AStaticFieldIsDefinedInTheSource) {
    ir::Class c = klass("Counter");
    ir::Field f = field("int", "count");
    f.is_static = true;
    c.fields = {f};
    ir::Model model;
    model.classes = {c};
    const Generated g = build(model, with_headers({"counter.h"}));
    EXPECT_EQ(member_lines(g.views[0].header_view, "class Counter {"), Strings{"static int count;"});
    EXPECT_EQ(definition_lines(g.views[0].source_view), Strings{"int Counter::count{};"});
}

// ---- the source file's place -------------------------------------------------------------------------------------

TEST(ImplBackendSource, NextToItsHeaderWithTheNamingPattern) {
    ir::Model model;
    model.classes = {klass("FooBar", {}, {method("f", "void")})};
    const Generated g = build(model, with_headers({"acme/core/foo_bar.h"}));
    EXPECT_EQ(g.views[0].source_path, "acme/core/foo_bar.cpp");

    backend::ImplOptions o = with_headers({"acme/foo_bar.h"});
    o.config.naming.source = "{stem}_impl.cc";
    EXPECT_EQ(build(model, o).views[0].source_path, "acme/foo_bar_impl.cc");
}

TEST(ImplBackendSource, NoSourceForAHeaderWithNothingToDefine) {
    ir::Class s = klass("Point");
    s.is_struct = true;
    s.fields = {field("int", "x")};
    ir::Model model;
    model.classes = {s};
    const Generated g = build(model, with_headers({"point.h"}));
    EXPECT_TRUE(g.views[0].source_path.empty());
}

TEST(ImplBackendSource, ThePathsHonourTheOutputDirectories) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "void")})};
    core::Diagnostics d;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, d);
    backend::ImplOptions o = with_headers({"acme/foo.h"});
    o.out_include = "out/include";
    o.out_src = "out/src/";
    const core::OutputPlan plan = backend::generate_impl(model, o, store, d);
    ASSERT_EQ(plan.files.size(), 2u);
    EXPECT_EQ(plan.files[0].path, "out/include/acme/foo.h");
    EXPECT_EQ(plan.files[1].path, "out/src/acme/foo.cpp");

    backend::ImplOptions here = with_headers({"foo.h"});
    here.out_include = ".";
    const core::OutputPlan plain = backend::generate_impl(model, here, store, d);
    EXPECT_EQ(plain.files[0].path, "foo.h");
    EXPECT_EQ(plain.files[1].path, "foo.cpp");
}

// ---- warnings about the regions -----------------------------------------------------------------------------

TEST(ImplBackendRegions, AGlobalClassNamedLikeARegionIsReported) {
    ir::Model model;
    model.classes = {klass("includes", {}, {method("f", "void")}), klass("functions", {}, {method("g", "void")})};
    model.functions = {function("h", "void")};
    const Generated g = build(model, with_headers({"all.h"}));
    EXPECT_EQ(codes(g), (Strings{"mt-unsupported", "mt-unsupported"}));
}

TEST(ImplBackendRegions, DistinctNamesProduceNoWarning) {
    ir::Model model;
    model.classes = {klass("A", {"x"}, {method("f", "void")}), klass("A", {"y"}, {method("f", "void")})};
    model.functions = {function("h", "void", {}, {"x"}), function("h", "void", {}, {})};
    EXPECT_TRUE(build(model, with_headers({"all.h"})).diagnostics.empty());
}

// ---- guards and the file header ----------------------------------------------------------------------------

TEST(ImplBackendView, GuardAndFileHeaderComeFromTheOptions) {
    ir::Model model;
    model.classes = {klass("Foo")};
    backend::ImplOptions o = with_headers({"../a/foo.h"});
    o.config.file_header = "// (c) ACME";
    const Generated g = build(model, o);
    EXPECT_EQ(g.views[0].header_view["guard"], "FOO_H");   // generated as foo.h: it left the output directory
    EXPECT_EQ(g.views[0].header_view["file_header"], "// (c) ACME");
    EXPECT_EQ(g.views[0].header_view["source"], "tests/t.cpp");
    EXPECT_FALSE(g.views[0].header_view["pragma_once"].get<bool>());

    backend::ImplOptions p = with_headers({"acme/foo.h"});
    p.config.guard = core::GuardStyle::PragmaOnce;
    const Generated gp = build(model, p);
    EXPECT_TRUE(gp.views[0].header_view["pragma_once"].get<bool>());
    EXPECT_EQ(gp.views[0].header_view["guard"], "ACME_FOO_H");
}

TEST(ImplBackendView, EveryViewHasEveryVariableTheTemplatesUse) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "void")})};
    model.functions = {function("g", "void")};
    const Generated g = build(model, with_headers({"foo.h"}));
    for (const char* key : {"file_header", "source", "pragma_once", "guard", "includes_begin", "includes_end", "includes", "sections"}) {
        EXPECT_TRUE(g.views[0].header_view.contains(key)) << key;
    }
    for (const char* key : {"file_header", "source", "own_include", "includes_begin", "includes_end", "includes", "sections"}) {
        EXPECT_TRUE(g.views[0].source_view.contains(key)) << key;
    }
}

// ---- the rendered text -------------------------------------------------------------------------------------------

struct Rendered {
    core::OutputPlan  plan;
    core::Diagnostics diagnostics;
    std::string       text(std::size_t i) const { return i < plan.files.size() ? plan.files[i].content : std::string("<missing>"); }
};

Rendered render(const ir::Model& model, const backend::ImplOptions& options) {
    Rendered r;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, r.diagnostics);
    r.plan = backend::generate_impl(model, options, store, r.diagnostics);
    return r;
}

TEST(ImplBackendRender, TheExampleOfTheDesignDocumentExactly) {
    ir::Method size = method("size", "unsigned int", {}, true);
    size.notes = {"return type inferred from literal 3u (tests/foo_test.cpp:12)"};
    ir::Method run = method("run", "void");
    run.notes = {"result never used; assumed void (tests/foo_test.cpp:17)"};
    ir::Method c = ctor("Foo", {param("int", "arg1")});
    c.is_explicit = true;
    c.notes = {"parameter 1 of 'acme::Foo::Foo' inferred from literal 3 (tests/foo_test.cpp:11)"};
    ir::Model model;
    model.classes = {klass("Foo", {"acme"}, {c, size, run})};

    backend::ImplOptions o = with_headers({"acme/foo.h"});
    o.source = "tests/foo_test.cpp";
    const Rendered r = render(model, o);
    ASSERT_EQ(r.plan.files.size(), 2u);
    EXPECT_TRUE(r.diagnostics.empty());

    EXPECT_EQ(r.plan.files[0].path, "acme/foo.h");
    EXPECT_EQ(r.text(0),
              "// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).\n"
              "#ifndef ACME_FOO_H\n"
              "#define ACME_FOO_H\n"
              "\n"
              "// <mt:begin includes>\n"
              "// <mt:end includes>\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "class Foo {\n"
              "public:\n"
              "    // <mt:begin acme::Foo>\n"
              "    // TODO(mt): parameter 1 of 'acme::Foo::Foo' inferred from literal 3 (tests/foo_test.cpp:11)\n"
              "    explicit Foo(int arg1);\n"
              "    // TODO(mt): return type inferred from literal 3u (tests/foo_test.cpp:12)\n"
              "    unsigned int size() const;\n"
              "    // TODO(mt): result never used; assumed void (tests/foo_test.cpp:17)\n"
              "    void run();\n"
              "    // <mt:end acme::Foo>\n"
              "};\n"
              "\n"
              "}  // namespace acme\n"
              "\n"
              "#endif  // ACME_FOO_H\n");

    EXPECT_EQ(r.plan.files[1].path, "acme/foo.cpp");
    EXPECT_EQ(r.text(1),
              "// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).\n"
              "#include \"acme/foo.h\"\n"
              "\n"
              "// <mt:begin includes>\n"
              "#include <stdexcept>\n"
              "// <mt:end includes>\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "// <mt:begin acme::Foo>\n"
              "Foo::Foo(int /*arg1*/) {}\n"
              "\n"
              "unsigned int Foo::size() const {\n"
              "    throw std::logic_error(\"mirrortest: not implemented: acme::Foo::size() const\");\n"
              "}\n"
              "\n"
              "void Foo::run() {\n"
              "    throw std::logic_error(\"mirrortest: not implemented: acme::Foo::run()\");\n"
              "}\n"
              "// <mt:end acme::Foo>\n"
              "\n"
              "}  // namespace acme\n");
}

TEST(ImplBackendRender, GlobalNamespacePragmaOnceAndACorporateHeader) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};
    backend::ImplOptions o = with_headers({"foo.h"});
    o.config.guard = core::GuardStyle::PragmaOnce;
    o.config.file_header = "// (c) ACME";
    const Rendered r = render(model, o);
    EXPECT_EQ(r.text(0),
              "// (c) ACME\n"
              "// Generated by mirrortest from tests/t.cpp. Review every TODO(mt).\n"
              "#pragma once\n"
              "\n"
              "// <mt:begin includes>\n"
              "// <mt:end includes>\n"
              "\n"
              "class Foo {\n"
              "public:\n"
              "    // <mt:begin Foo>\n"
              "    int f();\n"
              "    // <mt:end Foo>\n"
              "};\n");
    EXPECT_EQ(r.text(1),
              "// (c) ACME\n"
              "// Generated by mirrortest from tests/t.cpp. Review every TODO(mt).\n"
              "#include \"foo.h\"\n"
              "\n"
              "// <mt:begin includes>\n"
              "#include <stdexcept>\n"
              "// <mt:end includes>\n"
              "\n"
              "// <mt:begin Foo>\n"
              "int Foo::f() {\n"
              "    throw std::logic_error(\"mirrortest: not implemented: Foo::f()\");\n"
              "}\n"
              "// <mt:end Foo>\n");
}

TEST(ImplBackendRender, FreeFunctionsInTheirOwnRegionAndForwardDeclarationsBeforeTheClasses) {
    ir::Model model;
    model.classes = {klass("A", {"x"}, {method("get", "B")}), klass("B", {"x"})};
    model.functions = {function("add", "int", {param("int", "arg1")}, {"x"})};
    const Rendered r = render(model, with_headers({"all.h"}));
    EXPECT_EQ(r.text(0),
              "// Generated by mirrortest from tests/t.cpp. Review every TODO(mt).\n"
              "#ifndef ALL_H\n"
              "#define ALL_H\n"
              "\n"
              "// <mt:begin includes>\n"
              "// <mt:end includes>\n"
              "\n"
              "namespace x {\n"
              "\n"
              "class B;\n"
              "\n"
              "class A {\n"
              "public:\n"
              "    // <mt:begin x::A>\n"
              "    B get();\n"
              "    // <mt:end x::A>\n"
              "};\n"
              "\n"
              "class B {\n"
              "public:\n"
              "    // <mt:begin x::B>\n"
              "    // <mt:end x::B>\n"
              "};\n"
              "\n"
              "// <mt:begin x::functions>\n"
              "int add(int arg1);\n"
              "// <mt:end x::functions>\n"
              "\n"
              "}  // namespace x\n"
              "\n"
              "#endif  // ALL_H\n");
}

TEST(ImplBackendRender, TwoNamespacesAndNestedOnes) {
    ir::Model model;
    model.classes = {klass("A", {"one", "two"}, {method("f", "void")}), klass("B", {}, {method("g", "void")})};
    const Rendered r = render(model, with_headers({"all.h"}));
    const std::string source = r.text(1);
    EXPECT_NE(source.find("namespace one {\nnamespace two {\n\n// <mt:begin one::two::A>"), std::string::npos) << source;
    EXPECT_NE(source.find("// <mt:end one::two::A>\n\n}  // namespace two\n}  // namespace one\n\n// <mt:begin B>"), std::string::npos) << source;
}

TEST(ImplBackendRender, AnOutputFileEndsWithExactlyOneNewlineAndHasNoTrailingSpaces) {
    ir::Model model;
    model.classes = {klass("Foo", {"a"}, {ctor("Foo"), method("f", "int")})};
    model.functions = {function("g", "void", {}, {"a"})};
    const Rendered r = render(model, with_headers({"foo.h"}));
    for (const core::OutputFile& f : r.plan.files) {
        ASSERT_FALSE(f.content.empty());
        EXPECT_EQ(f.content.back(), '\n') << f.path;
        EXPECT_NE(f.content[f.content.size() - 2], '\n') << f.path;
        EXPECT_EQ(f.content.find(" \n"), std::string::npos) << f.path;
        EXPECT_EQ(f.content.find("\r"), std::string::npos) << f.path;
        EXPECT_EQ(f.content.find("\n\n\n"), std::string::npos) << f.path;
    }
}

TEST(ImplBackendRender, ABrokenTemplateIsAnErrorAndLeavesTheFileOut) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};
    std::vector<core::EmbeddedTemplate> templates;
    for (const core::EmbeddedTemplate& t : core::embedded_templates()) {
        if (t.name != "impl/header.inja") templates.push_back(t);
    }
    templates.push_back({"impl/header.inja", "{{ no_such_variable }}\n"});
    core::Diagnostics d;
    const core::TemplateStore store(templates, std::nullopt, d);
    const core::OutputPlan plan = backend::generate_impl(model, with_headers({"foo.h"}), store, d);
    EXPECT_TRUE(d.has_errors());
    ASSERT_EQ(plan.files.size(), 1u);   // the source still renders; the header is missing
    EXPECT_EQ(plan.files[0].path, "foo.cpp");
}

TEST(ImplBackendRender, AnOverriddenTemplateChangesOnlyTheLayout) {
    ir::Model model;
    model.classes = {klass("Foo", {}, {method("f", "int")})};
    std::vector<core::EmbeddedTemplate> templates;
    for (const core::EmbeddedTemplate& t : core::embedded_templates()) {
        if (t.name != "impl/source.inja") templates.push_back(t);
    }
    templates.push_back({"impl/source.inja", "// my layout\n{{ own_include }}\n"});
    core::Diagnostics d;
    const core::TemplateStore store(templates, std::nullopt, d);
    const core::OutputPlan plan = backend::generate_impl(model, with_headers({"foo.h"}), store, d);
    ASSERT_EQ(plan.files.size(), 2u);
    EXPECT_EQ(plan.files[1].content, "// my layout\n#include \"foo.h\"\n");
    EXPECT_TRUE(d.empty());
}

// ---- from a test file ----------------------------------------------------------------------------------------------

struct FromTest {
    frontend::Analysis analysis;
    core::OutputPlan   plan;
    core::Diagnostics  diagnostics;
};

FromTest from_test(const std::string& text, Strings headers, core::Config config = {}) {
    FromTest r;
    frontend::AnalysisOptions ao;
    ao.config = config;
    r.analysis = frontend::analyse({"tests/t.cpp", text}, ao, r.diagnostics);
    backend::ImplOptions o;
    o.config = config;
    o.source = "tests/t.cpp";
    o.headers_to_generate = std::move(headers);
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, r.diagnostics);
    r.plan = backend::generate_impl(r.analysis.model, o, store, r.diagnostics);
    return r;
}

const char* kReadmeTest =
    "#include <gtest/gtest.h>\n"
    "#include \"acme/foo.h\"\n"
    "\n"
    "TEST(FooTest, StartsWithThreeItems) {\n"
    "    const acme::Foo foo(3);\n"
    "    EXPECT_EQ(foo.size(), 3u);\n"
    "    EXPECT_FALSE(foo.empty());\n"
    "}\n";

TEST(ImplBackendEndToEnd, TheReadmeExample) {
    const FromTest r = from_test(kReadmeTest, {"acme/foo.h"});
    EXPECT_FALSE(r.diagnostics.has_errors());
    ASSERT_EQ(r.plan.files.size(), 2u);
    const std::string& header = r.plan.files[0].content;
    EXPECT_NE(header.find("    explicit Foo(int arg1);\n"), std::string::npos) << header;
    EXPECT_NE(header.find("    unsigned int size() const;\n"), std::string::npos) << header;
    EXPECT_NE(header.find("    bool empty() const;\n"), std::string::npos) << header;
    EXPECT_NE(header.find("namespace acme {\n"), std::string::npos) << header;
    const std::string& source = r.plan.files[1].content;
    EXPECT_NE(source.find("Foo::Foo(int /*arg1*/) {}\n"), std::string::npos) << source;
    EXPECT_NE(source.find("throw std::logic_error(\"mirrortest: not implemented: acme::Foo::size() const\");"), std::string::npos) << source;
}

TEST(ImplBackendEndToEnd, UsingNamespaceStdDoesNotLeakIntoTheHeader) {
    const FromTest r = from_test(
        "#include <gtest/gtest.h>\n"
        "#include \"foo.h\"\n"
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    Foo foo(1);\n"
        "    string s = foo.name();\n"
        "    vector<int> v = foo.items();\n"
        "}\n",
        {"foo.h"});
    ASSERT_GE(r.plan.files.size(), 1u);
    const std::string& header = r.plan.files[0].content;
    EXPECT_NE(header.find("    std::string name();\n"), std::string::npos) << header;
    EXPECT_NE(header.find("    std::vector<int> items();\n"), std::string::npos) << header;
    EXPECT_NE(header.find("#include <string>\n"), std::string::npos) << header;
    EXPECT_NE(header.find("#include <vector>\n"), std::string::npos) << header;
}

TEST(ImplBackendEndToEnd, TheSameInputGivesTheSameBytes) {
    const FromTest a = from_test(kReadmeTest, {"acme/foo.h"});
    const FromTest b = from_test(kReadmeTest, {"acme/foo.h"});
    ASSERT_EQ(a.plan.files.size(), b.plan.files.size());
    for (std::size_t i = 0; i < a.plan.files.size(); ++i) {
        EXPECT_EQ(a.plan.files[i].path, b.plan.files[i].path);
        EXPECT_EQ(a.plan.files[i].content, b.plan.files[i].content);
    }
}

TEST(ImplBackendEndToEnd, EveryPrefixOfATestFileIsGeneratedWithoutIncident) {
    const std::string text =
        "#include <gtest/gtest.h>\n"
        "#include \"acme/foo.h\"\n"
        "using namespace acme;\n"
        "TEST(S, N) {\n"
        "    Foo foo(3, \"abc\");\n"
        "    EXPECT_EQ(foo.size(), 3u);\n"
        "    Bar bar;\n"
        "    foo.take(bar);\n"
        "    EXPECT_EQ(helper(1), 2.5);\n"
        "    Point p;\n"
        "    p.x = 5;\n"
        "}\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const FromTest r = from_test(text.substr(0, n), {"acme/foo.h"});
        for (const core::OutputFile& f : r.plan.files) {
            EXPECT_FALSE(f.path.empty()) << "cut at " << n;
            EXPECT_EQ(f.content.find("\r"), std::string::npos) << "cut at " << n;
        }
    }
}

TEST(ImplBackendEndToEnd, NothingToGenerateIsAnEmptyPlan) {
    const FromTest r = from_test("#include <gtest/gtest.h>\nTEST(S, N) { EXPECT_EQ(1, 1); }\n", {});
    EXPECT_TRUE(r.plan.files.empty());
}

}  // namespace
