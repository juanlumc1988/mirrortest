// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/core/source.h"
#include "mirrortest/frontend/code_frontend.h"
#include "test_support.h"

using namespace mt;
using ir::Access;
using ir::MethodKind;
using ir::QtRole;
using ir::RefKind;

namespace {

struct Read {
    ir::Model         model;
    core::Diagnostics diagnostics;
};

Read read(const std::string& text, const std::string& include = "foo.h",
          const syntax::MacroTable& macros = {}) {
    core::SourceFile file{"foo.h", text};
    Read r;
    r.model = frontend::read_header(file, include, macros, r.diagnostics);
    return r;
}

const ir::Class* find_class(const Read& r, const char* name) {
    for (const ir::Class& c : r.model.classes) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// `const char*` and not std::string: a string parameter would bind a temporary
// made from the literal, and GCC then (wrongly) warns that the returned
// reference might dangle.
const ir::Class& the_class(const Read& r, const char* name) {
    static const ir::Class none;
    const ir::Class* c = find_class(r, name);
    EXPECT_NE(c, nullptr) << "no class " << name;
    return c ? *c : none;
}

const ir::Method* find_method(const ir::Class& c, const char* name) {
    for (const ir::Method& m : c.methods) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

std::vector<std::string> method_names(const ir::Class& c) {
    std::vector<std::string> names;
    for (const ir::Method& m : c.methods) names.push_back(m.name);
    return names;
}

}  // namespace

// ---- the spike header, field by field -------------------------------------------------------

class SpikeHeader : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string text = mt::test::read_file(std::string(MT_TEST_DATA_DIR) + "/spike_header.h");
        ASSERT_FALSE(text.empty());
        read_ = read(text, "acme/widget.h");
    }
    Read read_;
};

TEST_F(SpikeHeader, ParsesCleanlyAfterBlanking) {
    EXPECT_TRUE(read_.diagnostics.empty()) << core::format_diagnostic(read_.diagnostics.all()[0]);
}

TEST_F(SpikeHeader, Includes) {
    ASSERT_EQ(read_.model.includes.size(), 2u);
    EXPECT_EQ(read_.model.includes[0].path, "string");
    EXPECT_TRUE(read_.model.includes[0].is_system);
    EXPECT_EQ(read_.model.includes[1].path, "QObject");
}

TEST_F(SpikeHeader, TheWidgetClass) {
    ASSERT_EQ(read_.model.classes.size(), 2u);
    const ir::Class& w = the_class(read_, "Widget");
    EXPECT_EQ(w.ns, (std::vector<std::string>{"acme", "core"}));
    EXPECT_FALSE(w.is_struct);
    EXPECT_EQ(w.header, "acme/widget.h");
    EXPECT_TRUE(w.is_qobject);
    EXPECT_FALSE(w.is_gadget);
    EXPECT_FALSE(w.template_params.has_value());
    EXPECT_EQ(w.origin, ir::Origin::Declared);
    EXPECT_EQ(w.where.file, "foo.h");
    EXPECT_GT(w.where.line, 0u);

    ASSERT_EQ(w.bases.size(), 2u);
    EXPECT_EQ(w.bases[0].name, "QObject");
    EXPECT_EQ(w.bases[0].access, Access::Public);
    EXPECT_EQ(w.bases[1].name, "Base");
    EXPECT_EQ(w.bases[1].access, Access::Private);
}

TEST_F(SpikeHeader, TheWidgetMembersInDeclarationOrder) {
    const ir::Class& w = the_class(read_, "Widget");
    EXPECT_EQ(method_names(w), (std::vector<std::string>{
                                   "Widget", "~Widget", "Widget", "value", "setValue", "create", "apply",
                                   "operator==", "operator bool", "as", "ping", "reset", "valueChanged",
                                   "hidden", "item"}));
}

TEST_F(SpikeHeader, ConstructorsAndTheDestructor) {
    const ir::Class& w = the_class(read_, "Widget");
    const ir::Method& ctor = w.methods[0];
    EXPECT_EQ(ctor.kind, MethodKind::Constructor);
    EXPECT_TRUE(ctor.is_explicit);
    EXPECT_TRUE(ctor.ret.empty());
    ASSERT_EQ(ctor.params.size(), 1u);
    EXPECT_EQ(ctor.params[0].type.spelling, "QObject*");
    EXPECT_EQ(ctor.params[0].name, "parent");
    EXPECT_EQ(ctor.params[0].default_value, std::optional<std::string>("nullptr"));
    EXPECT_EQ(ctor.access, Access::Public);

    const ir::Method& dtor = w.methods[1];
    EXPECT_EQ(dtor.kind, MethodKind::Destructor);
    EXPECT_TRUE(dtor.is_override);
    EXPECT_TRUE(dtor.ret.empty());

    const ir::Method& copy = w.methods[2];
    EXPECT_EQ(copy.kind, MethodKind::Constructor);
    EXPECT_TRUE(copy.is_deleted);
    ASSERT_EQ(copy.params.size(), 1u);
    EXPECT_EQ(copy.params[0].type.spelling, "const Widget&");
}

TEST_F(SpikeHeader, OrdinaryMethods) {
    const ir::Class& w = the_class(read_, "Widget");

    const ir::Method& value = *find_method(w, "value");
    EXPECT_EQ(value.ret.spelling, "int");
    EXPECT_TRUE(value.is_const);
    EXPECT_TRUE(value.is_noexcept);
    EXPECT_FALSE(value.is_static || value.is_virtual || value.is_pure);

    const ir::Method& set = *find_method(w, "setValue");
    EXPECT_TRUE(set.ret.is_void());
    ASSERT_EQ(set.params.size(), 1u);
    EXPECT_EQ(set.params[0].name, "v");
    EXPECT_FALSE(set.is_const);

    const ir::Method& create = *find_method(w, "create");
    EXPECT_TRUE(create.is_static);
    EXPECT_EQ(create.ret.spelling, "Widget*");
    ASSERT_EQ(create.params.size(), 2u);
    EXPECT_EQ(create.params[0].type.spelling, "const std::string&");
    EXPECT_EQ(create.params[1].type.spelling, "std::size_t");
    EXPECT_EQ(create.params[1].default_value, std::optional<std::string>("0u"));

    const ir::Method& apply = *find_method(w, "apply");
    EXPECT_TRUE(apply.is_virtual);
    EXPECT_TRUE(apply.is_pure);
    EXPECT_FALSE(apply.is_const);
}

TEST_F(SpikeHeader, OperatorsAndConversions) {
    const ir::Class& w = the_class(read_, "Widget");

    const ir::Method& eq = *find_method(w, "operator==");
    EXPECT_EQ(eq.kind, MethodKind::Operator);
    EXPECT_EQ(eq.ret.spelling, "bool");
    EXPECT_TRUE(eq.is_const);
    ASSERT_EQ(eq.params.size(), 1u);
    EXPECT_EQ(eq.params[0].type.spelling, "const Widget&");

    const ir::Method& cast = *find_method(w, "operator bool");
    EXPECT_EQ(cast.kind, MethodKind::Conversion);
    EXPECT_TRUE(cast.is_explicit);
    EXPECT_TRUE(cast.is_const);
    EXPECT_TRUE(cast.ret.empty());
    EXPECT_TRUE(cast.params.empty());
}

TEST_F(SpikeHeader, MemberTemplatesAreMarked) {
    const ir::Method& as = *find_method(the_class(read_, "Widget"), "as");
    EXPECT_TRUE(as.is_template);
    EXPECT_EQ(as.ret.spelling, "T");
    EXPECT_TRUE(as.is_const);
}

TEST_F(SpikeHeader, QtRolesAndAccess) {
    const ir::Class& w = the_class(read_, "Widget");
    EXPECT_EQ(find_method(w, "ping")->qt_role, QtRole::Invokable);
    EXPECT_EQ(find_method(w, "ping")->access, Access::Public);

    EXPECT_EQ(find_method(w, "reset")->qt_role, QtRole::Slot);
    EXPECT_EQ(find_method(w, "reset")->access, Access::Public);

    EXPECT_EQ(find_method(w, "valueChanged")->qt_role, QtRole::Signal);
    EXPECT_EQ(find_method(w, "valueChanged")->access, Access::Public) << "signals are public";
    EXPECT_EQ(find_method(w, "valueChanged")->params[0].type.spelling, "int");

    EXPECT_EQ(find_method(w, "hidden")->access, Access::Protected);
    EXPECT_EQ(find_method(w, "hidden")->qt_role, QtRole::None) << "the section ended";
    EXPECT_EQ(find_method(w, "item")->access, Access::Private);
    EXPECT_EQ(find_method(w, "value")->qt_role, QtRole::None);
}

TEST_F(SpikeHeader, FieldsAndNestedTypes) {
    const ir::Class& w = the_class(read_, "Widget");
    ASSERT_EQ(w.fields.size(), 1u);
    EXPECT_EQ(w.fields[0].name, "value_");
    EXPECT_EQ(w.fields[0].type.spelling, "int");
    EXPECT_EQ(w.fields[0].access, Access::Private);
    EXPECT_FALSE(w.fields[0].is_static);
    EXPECT_EQ(w.nested_types, std::vector<std::string>{"Item"});
    EXPECT_EQ(find_method(w, "item")->ret.spelling, "Item");
}

TEST_F(SpikeHeader, TheClassTemplate) {
    const ir::Class& box = the_class(read_, "Box");
    EXPECT_EQ(box.template_params, std::optional<std::string>("typename T"));
    EXPECT_EQ(box.ns, (std::vector<std::string>{"acme", "core"}));
    ASSERT_EQ(box.methods.size(), 1u);
    EXPECT_EQ(box.methods[0].name, "get");
    EXPECT_TRUE(box.methods[0].is_const);
    EXPECT_FALSE(box.is_qobject);
}

TEST_F(SpikeHeader, EnumsAndFreeFunctions) {
    ASSERT_EQ(read_.model.enums.size(), 1u);
    const ir::Enum& color = read_.model.enums[0];
    EXPECT_EQ(color.name, "Color");
    EXPECT_TRUE(color.is_scoped);
    EXPECT_EQ(color.enumerators, (std::vector<std::string>{"Red", "Green"}));
    EXPECT_EQ(color.ns, (std::vector<std::string>{"acme", "core"}));
    EXPECT_EQ(color.header, "acme/widget.h");

    ASSERT_EQ(read_.model.functions.size(), 1u);
    const ir::Function& f = read_.model.functions[0];
    EXPECT_EQ(f.name, "freeFunc");
    EXPECT_EQ(f.ns, (std::vector<std::string>{"acme", "core"}));
    EXPECT_EQ(f.ret.spelling, "int");
    ASSERT_EQ(f.params.size(), 2u);
    EXPECT_EQ(f.params[0].type.spelling, "int");
    EXPECT_EQ(f.params[1].name, "b");
    EXPECT_EQ(f.header, "acme/widget.h");
}

// ---- scopes ------------------------------------------------------------------------------------

TEST(CodeFrontendScopes, NestedAndCompactNamespaces) {
    const Read r = read("namespace a { namespace b { class X {}; } }\nnamespace c::d::e { class Y {}; }");
    EXPECT_EQ(the_class(r, "X").ns, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(the_class(r, "Y").ns, (std::vector<std::string>{"c", "d", "e"}));
}

TEST(CodeFrontendScopes, TheNamespaceIsRestoredAfterABlock) {
    const Read r = read("namespace a { class In {}; }\nclass Out {};\nnamespace a::b { } \nclass AlsoOut {};");
    EXPECT_EQ(the_class(r, "In").ns, std::vector<std::string>{"a"});
    EXPECT_TRUE(the_class(r, "Out").ns.empty());
    EXPECT_TRUE(the_class(r, "AlsoOut").ns.empty());
}

TEST(CodeFrontendScopes, AnAnonymousNamespaceIsInvisibleToTests) {
    const Read r = read("namespace { class Hidden {}; int helper(); }\nclass Visible {};");
    EXPECT_EQ(find_class(r, "Hidden"), nullptr);
    EXPECT_NE(find_class(r, "Visible"), nullptr);
    EXPECT_TRUE(r.model.functions.empty());
}

namespace {

// A translation unit read the way a test file is.
ir::Model read_test_unit(const std::string& text) {
    const syntax::Preprocessed pre = syntax::preprocess(text);
    const syntax::Tree tree = syntax::parse_cpp(pre.text);
    ir::Model model;
    core::Diagnostics d;
    frontend::read_declarations(tree, pre.markers, "t.cpp", "", model, d, true);
    return model;
}

}  // namespace

TEST(CodeFrontendScopes, AnAnonymousNamespaceOfATestFileIsItsOwn) {
    const ir::Model model = read_test_unit("namespace { class Fake {}; int helper(int); }\nclass Visible {};");
    ASSERT_EQ(model.classes.size(), 2u);
    EXPECT_EQ(model.classes[0].name, "Fake");
    EXPECT_TRUE(model.classes[0].ns.empty());
    EXPECT_EQ(model.classes[1].name, "Visible");
    ASSERT_EQ(model.functions.size(), 1u);
    EXPECT_EQ(model.functions[0].name, "helper");
}

TEST(CodeFrontendScopes, TheNamespaceAroundAnAnonymousOneIsKept) {
    const ir::Model model = read_test_unit("namespace acme { namespace { class Fake {}; } class Out {}; }\nclass Visible {};");
    ASSERT_EQ(model.classes.size(), 3u);
    EXPECT_EQ(model.classes[0].ns, std::vector<std::string>{"acme"});
    EXPECT_EQ(model.classes[1].ns, std::vector<std::string>{"acme"});
    EXPECT_TRUE(model.classes[2].ns.empty());
}

TEST(CodeFrontendScopes, AnAnonymousNamespaceInsideAnAnonymousOne) {
    const ir::Model model = read_test_unit("namespace { namespace { class Deep {}; } }");
    ASSERT_EQ(model.classes.size(), 1u);
    EXPECT_EQ(model.classes[0].name, "Deep");
}

TEST(CodeFrontendScopes, ExternC) {
    const Read r = read("extern \"C\" { int c_api(int); }\nextern \"C\" int c_one(void);");
    ASSERT_EQ(r.model.functions.size(), 2u);
    EXPECT_EQ(r.model.functions[0].name, "c_api");
    EXPECT_EQ(r.model.functions[1].name, "c_one");
    EXPECT_TRUE(r.model.functions[1].params.empty()) << "(void) is no parameters";
}

TEST(CodeFrontendScopes, ADefinitionAfterAStructKeepsBoth) {
    const Read r = read("struct S { int x; } s;");
    EXPECT_NE(find_class(r, "S"), nullptr);
}

TEST(CodeFrontendScopes, ForwardDeclarationsAreNotClasses) {
    const Read r = read("class Fwd;\nstruct AlsoFwd;\nenum class E;\nclass Real {};");
    EXPECT_EQ(r.model.classes.size(), 1u);
    EXPECT_TRUE(r.model.enums.empty());
}

// ---- includes ------------------------------------------------------------------------------------

TEST(CodeFrontendIncludes, QuotedAndSystem) {
    const Read r = read("#include \"acme/foo.h\"\n#include <vector>\n  #  include \"bar.h\"\n");
    ASSERT_EQ(r.model.includes.size(), 3u);
    EXPECT_EQ(r.model.includes[0].path, "acme/foo.h");
    EXPECT_FALSE(r.model.includes[0].is_system);
    EXPECT_EQ(r.model.includes[0].where.line, 1u);
    EXPECT_EQ(r.model.includes[1].path, "vector");
    EXPECT_TRUE(r.model.includes[1].is_system);
    EXPECT_EQ(r.model.includes[2].path, "bar.h");
}

TEST(CodeFrontendIncludes, InsideConditionalsToo) {
    const Read r = read("#ifdef X\n#include \"a.h\"\n#else\n#include \"b.h\"\n#endif\n");
    ASSERT_EQ(r.model.includes.size(), 2u);
}

// ---- classes ----------------------------------------------------------------------------------------

TEST(CodeFrontendClasses, DefaultAccessDependsOnTheKeyword) {
    const Read r = read("class C { int a; public: int b; private: int c; };\n"
                        "struct S { int a; private: int b; public: int c; protected: int d; };");
    const ir::Class& c = the_class(r, "C");
    EXPECT_FALSE(c.is_struct);
    EXPECT_EQ(c.fields[0].access, Access::Private);
    EXPECT_EQ(c.fields[1].access, Access::Public);
    EXPECT_EQ(c.fields[2].access, Access::Private);

    const ir::Class& s = the_class(r, "S");
    EXPECT_TRUE(s.is_struct);
    EXPECT_EQ(s.fields[0].access, Access::Public);
    EXPECT_EQ(s.fields[1].access, Access::Private);
    EXPECT_EQ(s.fields[2].access, Access::Public);
    EXPECT_EQ(s.fields[3].access, Access::Protected);
}

TEST(CodeFrontendClasses, BaseClassAccessAndKinds) {
    const Read r = read("class A : B {};\nstruct S : B {};\nclass V : virtual public B, protected C {};\n"
                        "class T : public std::vector<int>, public ns::Base {};");
    EXPECT_EQ(the_class(r, "A").bases[0].access, Access::Private) << "class: private by default";
    EXPECT_EQ(the_class(r, "S").bases[0].access, Access::Public) << "struct: public by default";

    const ir::Class& v = the_class(r, "V");
    ASSERT_EQ(v.bases.size(), 2u);
    EXPECT_TRUE(v.bases[0].is_virtual);
    EXPECT_EQ(v.bases[0].access, Access::Public);
    EXPECT_FALSE(v.bases[1].is_virtual);
    EXPECT_EQ(v.bases[1].access, Access::Protected);

    const ir::Class& t = the_class(r, "T");
    EXPECT_EQ(t.bases[0].name, "std::vector<int>");
    EXPECT_EQ(t.bases[1].name, "ns::Base");
}

TEST(CodeFrontendClasses, ClassTemplatesCarryTheirParameters) {
    const Read r = read("template <typename T, int N = 4>\nclass Buf { public: T at(int i) const; };");
    EXPECT_EQ(the_class(r, "Buf").template_params, std::optional<std::string>("typename T, int N = 4"));
}

TEST(CodeFrontendClasses, FunctionTemplatesAreSkippedWithAWarning) {
    const Read r = read("template <typename T> T twice(T v);\nint plain(int);");
    ASSERT_EQ(r.model.functions.size(), 1u);
    EXPECT_EQ(r.model.functions[0].name, "plain");
    ASSERT_EQ(r.diagnostics.all().size(), 1u);
    EXPECT_EQ(r.diagnostics.all()[0].code, core::code::unsupported);
    EXPECT_EQ(r.diagnostics.all()[0].severity, core::Severity::Warning);
    EXPECT_NE(r.diagnostics.all()[0].message.find("twice"), std::string::npos);
}

TEST(CodeFrontendClasses, NestedTypesAreRecordedByName) {
    const Read r = read("class C { public: struct N { int v; }; enum class E { X }; typedef int Handle;"
                        " using Alias = long; class Inner {}; };");
    EXPECT_EQ(the_class(r, "C").nested_types,
              (std::vector<std::string>{"N", "E", "Handle", "Alias", "Inner"}));
    EXPECT_EQ(r.model.classes.size(), 1u) << "their bodies are not read";
}

// ---- members -------------------------------------------------------------------------------------------

TEST(CodeFrontendMembers, DefaultedDeletedAndOperatorsOfSpecialMembers) {
    const Read r = read("class A { public:\n"
                        "  A() = default;\n"
                        "  A(const A&) = delete;\n"
                        "  A& operator=(A&&) noexcept = default;\n"
                        "  virtual ~A();\n"
                        "};");
    const ir::Class& a = the_class(r, "A");
    ASSERT_EQ(a.methods.size(), 4u);
    EXPECT_TRUE(a.methods[0].is_defaulted);
    EXPECT_TRUE(a.methods[1].is_deleted);
    const ir::Method& assign = a.methods[2];
    EXPECT_EQ(assign.name, "operator=");
    EXPECT_EQ(assign.kind, MethodKind::Operator);
    EXPECT_TRUE(assign.is_defaulted);
    EXPECT_TRUE(assign.is_noexcept);
    EXPECT_EQ(assign.ret.spelling, "A&");
    EXPECT_EQ(assign.params[0].type.spelling, "A&&");
    EXPECT_EQ(a.methods[3].kind, MethodKind::Destructor);
    EXPECT_TRUE(a.methods[3].is_virtual);
}

TEST(CodeFrontendMembers, ConstructorsWithInitialiserListsAndInlineBodies) {
    const Read r = read("class K { public: K() : v_(0), w_{1} {}\n int get() const { return v_; } int v_; int w_; };");
    const ir::Class& k = the_class(r, "K");
    ASSERT_EQ(k.methods.size(), 2u);
    EXPECT_EQ(k.methods[0].kind, MethodKind::Constructor);
    EXPECT_EQ(k.methods[1].name, "get");
    EXPECT_TRUE(k.methods[1].is_const);
    EXPECT_EQ(k.fields.size(), 2u);
}

TEST(CodeFrontendMembers, FinalOverrideAndRefQualifiers) {
    const Read r = read("class D : B { public: int f() const final; void g() &; void h() const &&;"
                        " void i() override; };");
    const ir::Class& d = the_class(r, "D");
    EXPECT_TRUE(find_method(d, "f")->is_final);
    EXPECT_EQ(find_method(d, "g")->ref_qualifier, RefKind::LValue);
    EXPECT_EQ(find_method(d, "h")->ref_qualifier, RefKind::RValue);
    EXPECT_TRUE(find_method(d, "h")->is_const);
    EXPECT_TRUE(find_method(d, "i")->is_override);
}

TEST(CodeFrontendMembers, TrailingReturnTypes) {
    const Read r = read("class T { public: auto f() -> int; auto g() const -> const std::string&; };\n"
                        "auto free_fn(int) -> double;");
    EXPECT_EQ(find_method(the_class(r, "T"), "f")->ret.spelling, "int");
    EXPECT_EQ(find_method(the_class(r, "T"), "g")->ret.spelling, "const std::string&");
    ASSERT_EQ(r.model.functions.size(), 1u);
    EXPECT_EQ(r.model.functions[0].ret.spelling, "double");
}

TEST(CodeFrontendMembers, OverloadsAreDistinctMembers) {
    const Read r = read("class O { public: void f(int); void f(double); void f(int) const; };");
    EXPECT_EQ(the_class(r, "O").methods.size(), 3u);
}

TEST(CodeFrontendMembers, ConversionOperatorsNameTheirTarget) {
    const Read r = read("class K { public: operator const char*() const; operator std::vector<int>&();"
                        " explicit operator bool() const noexcept; };");
    const ir::Class& k = the_class(r, "K");
    EXPECT_EQ(method_names(k), (std::vector<std::string>{"operator const char*", "operator std::vector<int>&",
                                                         "operator bool"}));
    for (const ir::Method& m : k.methods) EXPECT_EQ(m.kind, MethodKind::Conversion);
    EXPECT_TRUE(k.methods[2].is_noexcept);
    EXPECT_TRUE(k.methods[2].is_explicit);
}

TEST(CodeFrontendMembers, OperatorNamesAreNormalised) {
    const Read r = read("class O { public: bool operator ==(const O&) const; int& operator [] (int);"
                        " void* operator new (std::size_t); void operator delete [] (void*);"
                        " int operator()(int) const; };");
    EXPECT_EQ(method_names(the_class(r, "O")),
              (std::vector<std::string>{"operator==", "operator[]", "operator new", "operator delete[]",
                                        "operator()"}));
}

TEST(CodeFrontendMembers, ANameThatMerelyStartsWithOperatorIsNotAnOperator) {
    const Read r = read("class O { public: void operatorX(); };");
    const ir::Method& m = the_class(r, "O").methods[0];
    EXPECT_EQ(m.name, "operatorX");
    EXPECT_EQ(m.kind, MethodKind::Normal);
}

TEST(CodeFrontendMembers, FieldsOfEveryShape) {
    const Read r = read("class F { public:\n"
                        "  int a, *b, &c = ref;\n"
                        "  int (*cb)(int);\n"
                        "  static constexpr int kMax = 3;\n"
                        "  const char* const names[3];\n"
                        "  unsigned bits : 3;\n"
                        "  mutable int m;\n"
                        "};");
    const ir::Class& f = the_class(r, "F");
    ASSERT_EQ(f.fields.size(), 8u);
    EXPECT_EQ(f.fields[0].name, "a");
    EXPECT_EQ(f.fields[0].type.spelling, "int");
    EXPECT_EQ(f.fields[1].type.spelling, "int*");
    EXPECT_EQ(f.fields[2].type.spelling, "int&");
    EXPECT_EQ(f.fields[3].name, "cb");
    EXPECT_EQ(f.fields[3].type.spelling, "int(*)(int)");
    EXPECT_TRUE(f.methods.empty()) << "a function pointer is a field, not a method";
    EXPECT_TRUE(f.fields[4].is_static);
    EXPECT_EQ(f.fields[5].name, "names");
    EXPECT_EQ(f.fields[5].type.spelling, "const char* const[3]");
    EXPECT_EQ(f.fields[6].name, "bits");
    EXPECT_EQ(f.fields[6].type.spelling, "unsigned");
    EXPECT_EQ(f.fields[7].name, "m");
}

TEST(CodeFrontendMembers, FriendsUsingAndStaticAssertsAreNotMembers) {
    const Read r = read("class G { friend class F; friend void swap(G&, G&); using Base::Base;"
                        " static_assert(sizeof(int) == 4, \"\"); public: int real(); };");
    EXPECT_EQ(method_names(the_class(r, "G")), std::vector<std::string>{"real"});
}

TEST(CodeFrontendMembers, LocationsPointAtTheNamesInTheOriginalFile) {
    const Read r = read("class C {\npublic:\n    int size() const;\n    explicit C(int x);\n};");
    const ir::Class& c = the_class(r, "C");
    EXPECT_EQ(c.where.file, "foo.h");
    EXPECT_EQ(c.where.line, 1u);
    EXPECT_EQ(c.where.column, 7u);
    EXPECT_EQ(c.methods[0].where.line, 3u);
    EXPECT_EQ(c.methods[0].where.column, 9u);
    EXPECT_EQ(c.methods[1].where.line, 4u);
}

// ---- free functions ---------------------------------------------------------------------------------------

TEST(CodeFrontendFunctions, DeclarationsAndInlineDefinitions) {
    const Read r = read("int plain(int a);\nstatic int helper(int a) noexcept;\ninline int inl(int a) { return a; }\n"
                        "bool operator==(const A&, const A&);\nextern int counter;\nint value = 3;\nint (*fp)(int);\n"
                        "int Foo::member(int) { return 0; }");
    ASSERT_EQ(r.model.functions.size(), 4u);
    EXPECT_EQ(r.model.functions[0].name, "plain");
    EXPECT_EQ(r.model.functions[1].name, "helper");
    EXPECT_TRUE(r.model.functions[1].is_noexcept);
    EXPECT_EQ(r.model.functions[2].name, "inl");
    EXPECT_EQ(r.model.functions[3].name, "operator==");
    EXPECT_EQ(r.model.functions[3].params.size(), 2u);
}

// ---- enums ------------------------------------------------------------------------------------------------------

TEST(CodeFrontendEnums, ScopedUnscopedAndUnderlyingTypes) {
    const Read r = read("enum Color { Red, Green = 2, Blue };\n"
                        "enum class Mode : unsigned char { Off = 0, On };\n"
                        "enum struct Flag { A };\n"
                        "enum { Anonymous };");
    ASSERT_EQ(r.model.enums.size(), 3u);
    EXPECT_EQ(r.model.enums[0].name, "Color");
    EXPECT_FALSE(r.model.enums[0].is_scoped);
    EXPECT_EQ(r.model.enums[0].enumerators, (std::vector<std::string>{"Red", "Green", "Blue"}));
    EXPECT_FALSE(r.model.enums[0].underlying.has_value());

    EXPECT_TRUE(r.model.enums[1].is_scoped);
    EXPECT_EQ(r.model.enums[1].underlying, std::optional<std::string>("unsigned char"));
    EXPECT_EQ(r.model.enums[1].enumerators, (std::vector<std::string>{"Off", "On"}));

    EXPECT_TRUE(r.model.enums[2].is_scoped) << "enum struct is a scoped enum";
}

// ---- conditional compilation -----------------------------------------------------------------------------------------

TEST(CodeFrontendConditional, EveryBranchIsRead) {
    const Read r = read("#ifdef PLATFORM_A\nint platform_a_only();\n#else\nint platform_b_only();\n#endif\n");
    ASSERT_EQ(r.model.functions.size(), 2u);
    EXPECT_EQ(r.model.functions[0].name, "platform_a_only");
    EXPECT_EQ(r.model.functions[1].name, "platform_b_only");
}

TEST(CodeFrontendConditional, AClassDefinedInBothBranchesIsOneClassWithTheUnionOfItsMembers) {
    const Read r = read("#ifdef X\nclass A : public B { public: void f(); };\n#else\n"
                        "class A : public B { public: void f(); void g(); };\n#endif\n");
    ASSERT_EQ(r.model.classes.size(), 1u);
    const ir::Class& a = the_class(r, "A");
    EXPECT_EQ(method_names(a), (std::vector<std::string>{"f", "g"})) << "f is not listed twice";
    EXPECT_EQ(a.bases.size(), 1u) << "nor are the bases";
}

TEST(CodeFrontendConditional, MembersInsideAClassBody) {
    const Read r = read("class A { public:\n#ifdef X\n void f();\n#else\n void g();\n#endif\n void h();\n};");
    EXPECT_EQ(method_names(the_class(r, "A")), (std::vector<std::string>{"f", "g", "h"}));
}

TEST(CodeFrontendConditional, ReadingTheSameHeaderTwiceDoesNotDuplicate) {
    const std::string text = "namespace n { class A : public B { public: void f(); int x; }; }";
    const syntax::Preprocessed pre = syntax::preprocess(text);
    const syntax::Tree tree = syntax::parse_cpp(pre.text);
    ir::Model model;
    core::Diagnostics d;
    frontend::read_declarations(tree, pre.markers, "foo.h", "foo.h", model, d);
    frontend::read_declarations(tree, pre.markers, "foo.h", "foo.h", model, d);
    ASSERT_EQ(model.classes.size(), 1u);
    EXPECT_EQ(model.classes[0].methods.size(), 1u);
    EXPECT_EQ(model.classes[0].fields.size(), 1u);
    EXPECT_EQ(model.classes[0].bases.size(), 1u);
}

// ---- Qt -----------------------------------------------------------------------------------------------------------------

TEST(CodeFrontendQt, GadgetsAreNotQObjects) {
    const Read r = read("struct P { Q_GADGET public: int x; };\nclass O : public QObject { Q_OBJECT };");
    EXPECT_TRUE(the_class(r, "P").is_gadget);
    EXPECT_FALSE(the_class(r, "P").is_qobject);
    EXPECT_TRUE(the_class(r, "O").is_qobject);
    EXPECT_FALSE(the_class(r, "O").is_gadget);
}

TEST(CodeFrontendQt, DisableCopy) {
    const Read r = read("class N { Q_DISABLE_COPY(N) public: N(); };");
    EXPECT_TRUE(the_class(r, "N").disable_copy);
}

TEST(CodeFrontendQt, ANestedClassesMacrosDoNotMarkTheOuterClass) {
    const Read r = read("class Outer { public: struct In { Q_GADGET public: int x; }; void f(); };");
    EXPECT_FALSE(the_class(r, "Outer").is_gadget);
}

TEST(CodeFrontendQt, InvokableAppliesToTheNextMemberOnly) {
    const Read r = read("class A : public QObject { Q_OBJECT public: Q_INVOKABLE void a();\n void b();\n"
                        " /* note */ Q_INVOKABLE void c(); };");
    const ir::Class& a = the_class(r, "A");
    EXPECT_EQ(find_method(a, "a")->qt_role, QtRole::Invokable);
    EXPECT_EQ(find_method(a, "b")->qt_role, QtRole::None);
    EXPECT_EQ(find_method(a, "c")->qt_role, QtRole::Invokable);
}

TEST(CodeFrontendQt, SectionsEndAtTheNextAccessSpecifier) {
    const Read r = read("class A : public QObject {\nQ_OBJECT\nprivate slots:\n void s1();\n void s2();\n"
                        "public:\n void normal();\nQ_SIGNALS:\n void sig();\nprivate:\n void priv();\n};");
    const ir::Class& a = the_class(r, "A");
    EXPECT_EQ(find_method(a, "s1")->qt_role, QtRole::Slot);
    EXPECT_EQ(find_method(a, "s1")->access, Access::Private);
    EXPECT_EQ(find_method(a, "s2")->qt_role, QtRole::Slot);
    EXPECT_EQ(find_method(a, "normal")->qt_role, QtRole::None);
    EXPECT_EQ(find_method(a, "sig")->qt_role, QtRole::Signal);
    EXPECT_EQ(find_method(a, "priv")->qt_role, QtRole::None);
}

TEST(CodeFrontendQt, WithoutMarkersThereAreNoRoles) {
    // Plain C++ that happens to use the word `slots` as a name.
    const Read r = read("class A { public: int slots; void signals_(); };");
    EXPECT_EQ(the_class(r, "A").fields[0].name, "slots");
    EXPECT_EQ(find_method(the_class(r, "A"), "signals_")->qt_role, QtRole::None);
}

// ---- tolerating what it cannot parse ---------------------------------------------------------------------------------------------

TEST(CodeFrontendErrors, ParseErrorsWarnButDoNotStopTheRead) {
    const Read r = read("class A { public: void f(); };\nint x = = ;\nclass B { public: void g(); };");
    EXPECT_NE(find_class(r, "A"), nullptr);
    EXPECT_NE(find_class(r, "B"), nullptr);

    bool warned = false;
    for (const core::Diagnostic& d : r.diagnostics.all()) {
        if (d.code == core::code::parse) {
            warned = true;
            EXPECT_EQ(d.severity, core::Severity::Warning);
            EXPECT_EQ(d.where.file, "foo.h");
            EXPECT_EQ(d.where.line, 2u);
        }
    }
    EXPECT_TRUE(warned);
}

TEST(CodeFrontendErrors, ManyParseErrorsAreCappedWithASummary) {
    std::string text;
    for (int i = 0; i < 30; ++i) text += "int v" + std::to_string(i) + " = = ;\n";
    const Read r = read(text);

    std::size_t warnings = 0;
    bool summary = false;
    for (const core::Diagnostic& d : r.diagnostics.all()) {
        if (d.severity == core::Severity::Warning) ++warnings;
        if (d.severity == core::Severity::Note && d.message.find("more parse errors") != std::string::npos) summary = true;
    }
    EXPECT_EQ(warnings, 10u);
    EXPECT_TRUE(summary);
}

TEST(CodeFrontendErrors, EmptyInputGivesAnEmptyModel) {
    const Read r = read("");
    EXPECT_TRUE(r.model.classes.empty());
    EXPECT_TRUE(r.model.functions.empty());
    EXPECT_TRUE(r.model.enums.empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(CodeFrontendErrors, IgnoredMacrosKeepAHeaderParseable) {
    const std::string text = "class MY_API Widget { public: MY_ATTR(a) void f(); };";
    const Read without = read(text);
    EXPECT_NE(find_class(without, "Widget"), nullptr) << "the export macro is blanked by default";

    syntax::MacroTable table;
    table.ignore = {"MY_ATTR"};
    const Read with = read(text, "foo.h", table);
    EXPECT_TRUE(with.diagnostics.empty());
    EXPECT_EQ(method_names(the_class(with, "Widget")), std::vector<std::string>{"f"});
}

// ---- #if branches that disagree (6.5) ----------------------------------------------------------------------------------

namespace {

std::vector<core::Diagnostic> with_code(const Read& r, const char* code) {
    std::vector<core::Diagnostic> out;
    for (const core::Diagnostic& d : r.diagnostics.sorted()) {
        if (d.code == code) out.push_back(d);
    }
    return out;
}

}  // namespace

TEST(CodeFrontendConditionalNotes, TwoBranchesDeclaringTheSameNameAndArityDifferentlyAreNoted) {
    const Read r = read(
        "class A {\npublic:\n"
        "#ifdef PLATFORM_A\n"
        "    void set(int v);\n"
        "#else\n"
        "    void set(double v);\n"
        "#endif\n"
        "};\n");
    EXPECT_EQ(method_names(the_class(r, "A")), (std::vector<std::string>{"set", "set"})) << "both are kept";
    const std::vector<core::Diagnostic> notes = with_code(r, "mt-conditional");
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0].severity, core::Severity::Note);
    EXPECT_EQ(notes[0].message,
              "'A::set' is declared differently under the branches of an #if: 'void set(int)' and 'void set(double)'; both are kept");
    EXPECT_EQ(notes[0].where.line, 6u);
    ASSERT_EQ(notes[0].notes.size(), 1u);
    EXPECT_EQ(notes[0].notes[0].where.line, 4u);
}

TEST(CodeFrontendConditionalNotes, TheSameSignatureWithAnotherReturnTypeKeepsTheFirst) {
    const Read r = read(
        "class A {\npublic:\n"
        "#if X\n    int get();\n#else\n    long get();\n#endif\n"
        "};\n");
    const ir::Class& a = the_class(r, "A");
    ASSERT_EQ(a.methods.size(), 1u);
    EXPECT_EQ(a.methods[0].ret.spelling, "int");
    const std::vector<core::Diagnostic> notes = with_code(r, "mt-conditional");
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_NE(notes[0].message.find("the first is kept"), std::string::npos) << notes[0].message;
}

TEST(CodeFrontendConditionalNotes, TheSameDeclarationInBothBranchesIsNotWorthANote) {
    const Read r = read("class A {\npublic:\n#ifdef X\n    int get();\n#else\n    int get();\n#endif\n};\n");
    EXPECT_EQ(the_class(r, "A").methods.size(), 1u);
    EXPECT_TRUE(with_code(r, "mt-conditional").empty());
}

TEST(CodeFrontendConditionalNotes, OverloadsAreOnlyAlternativesIfTheyAreInDifferentBranches) {
    for (const char* body : {
             // plain overloads
             "    void set(int);\n    void set(double);\n",
             // both in the same branch
             "#ifdef X\n    void set(int);\n    void set(double);\n#endif\n",
             // one conditional, one not
             "#ifdef X\n    void set(int);\n#endif\n    void set(double);\n",
             // two separate conditionals
             "#ifdef X\n    void set(int);\n#endif\n#ifdef Y\n    void set(double);\n#endif\n",
             // the else of one conditional is not the opposite of the if of another
             "#ifdef X\n    void set(int);\n#endif\n#ifdef Y\n    void other();\n#else\n    void set(double);\n#endif\n"}) {
        const Read r = read(std::string("class A {\npublic:\n") + body + "};\n");
        EXPECT_TRUE(with_code(r, "mt-conditional").empty()) << body;
    }
}

TEST(CodeFrontendConditionalNotes, DifferentNamesOrAritiesAreDifferentMembers) {
    const Read r = read("class A {\npublic:\n#ifdef X\n    void set(int);\n#else\n    void set(int, int);\n    void put(int);\n#endif\n};\n");
    EXPECT_TRUE(with_code(r, "mt-conditional").empty());
}

TEST(CodeFrontendConditionalNotes, AnElifChainComparesEveryPair) {
    const Read r = read(
        "class A {\npublic:\n"
        "#if A\n    void f(int);\n#elif B\n    void f(long);\n#else\n    void f(char);\n#endif\n"
        "};\n");
    EXPECT_EQ(the_class(r, "A").methods.size(), 3u);
    EXPECT_EQ(with_code(r, "mt-conditional").size(), 3u) << "if/elif, if/else and elif/else";
}

TEST(CodeFrontendConditionalNotes, TheClassMayBeDefinedInEachBranch) {
    const Read r = read(
        "#ifdef X\nclass A { public: void f(int); };\n#else\nclass A { public: void f(double); };\n#endif\n");
    EXPECT_EQ(the_class(r, "A").methods.size(), 2u);
    ASSERT_EQ(with_code(r, "mt-conditional").size(), 1u);
    EXPECT_NE(with_code(r, "mt-conditional")[0].message.find("'A::f'"), std::string::npos);
}

TEST(CodeFrontendConditionalNotes, NamespacesAreInTheName) {
    const Read r = read("namespace ns {\nclass A {\npublic:\n#ifdef X\n void f(int);\n#else\n void f(char);\n#endif\n};\n}\n");
    ASSERT_EQ(with_code(r, "mt-conditional").size(), 1u);
    EXPECT_NE(with_code(r, "mt-conditional")[0].message.find("'ns::A::f'"), std::string::npos);
}

TEST(CodeFrontendConditionalNotes, NestedConditionalsAreTheirOwnChains) {
    const Read r = read(
        "class A {\npublic:\n"
        "#ifdef OUTER\n"
        "#ifdef INNER\n    void f(int);\n#else\n    void f(char);\n#endif\n"
        "#endif\n"
        "};\n");
    EXPECT_EQ(with_code(r, "mt-conditional").size(), 1u);
}

TEST(CodeFrontendConditionalNotes, ReadingTheSameHeaderTwiceAddsNoNotes) {
    core::SourceFile file{"foo.h", "class A {\npublic:\n#ifdef X\n void f(int);\n#else\n void f(int);\n#endif\n};\n"};
    ir::Model model;
    core::Diagnostics diagnostics;
    const syntax::Preprocessed pre = syntax::preprocess(file.text, {});
    const syntax::Tree tree = syntax::parse_cpp(pre.text);
    frontend::read_declarations(tree, pre.markers, "foo.h", "foo.h", model, diagnostics);
    frontend::read_declarations(tree, pre.markers, "foo.h", "foo.h", model, diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(model.classes.size(), 1u);
}

// ---- nested classes ---------------------------------------------------------------------------------------------------------

TEST(CodeFrontendNested, ANestedClassIsReadLikeAnyOther) {
    const Read r = read(
        "namespace ns {\n"
        "class Outer {\npublic:\n"
        "    struct Item { int x; Item(); explicit Item(int v); void reset(); };\n"
        "    Item make() const;\n"
        "private:\n"
        "    class Hidden { public: void f(); };\n"
        "};\n}\n");
    const ir::Class& outer = the_class(r, "Outer");
    EXPECT_EQ(outer.nested_types, (std::vector<std::string>{"Item", "Hidden"}));
    ASSERT_EQ(outer.nested_classes.size(), 2u);

    const ir::Class& item = outer.nested_classes[0];
    EXPECT_EQ(item.name, "Item");
    EXPECT_EQ(item.ns, std::vector<std::string>{"ns"});
    EXPECT_TRUE(item.is_struct);
    EXPECT_EQ(item.nested_access, Access::Public);
    EXPECT_EQ(item.methods.size(), 3u);
    EXPECT_EQ(item.fields.size(), 1u);
    EXPECT_EQ(item.methods[0].kind, MethodKind::Constructor);
    EXPECT_TRUE(item.methods[1].is_explicit);

    const ir::Class& hidden = outer.nested_classes[1];
    EXPECT_EQ(hidden.name, "Hidden");
    EXPECT_FALSE(hidden.is_struct);
    EXPECT_EQ(hidden.nested_access, Access::Private);
    EXPECT_EQ(method_names(hidden), std::vector<std::string>{"f"});

    // The outer class itself is untouched by what is inside.
    EXPECT_EQ(method_names(outer), std::vector<std::string>{"make"});
}

TEST(CodeFrontendNested, OnlyThePublicEnumsAreNestedEnums) {
    const Read r = read(
        "class A {\n"
        "    enum class Hidden { X };\n"
        "public:\n"
        "    enum class Mode { A, B };\n"
        "    enum Plain { P };\n"
        "    enum { Anonymous };\n"
        "    struct S { int x; };\n"
        "protected:\n"
        "    enum Guarded { G };\n"
        "};");
    const ir::Class& a = the_class(r, "A");
    EXPECT_EQ(a.nested_enums, (std::vector<std::string>{"Mode", "Plain"}));
    EXPECT_EQ(a.nested_types, (std::vector<std::string>{"Hidden", "Mode", "Plain", "S", "Guarded"}));
}

TEST(CodeFrontendNested, NestingGoesAsDeepAsTheCode) {
    const Read r = read("class A { public: class B { public: class C { public: void deep(); }; }; };");
    const ir::Class& a = the_class(r, "A");
    ASSERT_EQ(a.nested_classes.size(), 1u);
    ASSERT_EQ(a.nested_classes[0].nested_classes.size(), 1u);
    EXPECT_EQ(method_names(a.nested_classes[0].nested_classes[0]), std::vector<std::string>{"deep"});
    EXPECT_EQ(r.model.classes.size(), 1u) << "nested classes are not classes of the model";
}

TEST(CodeFrontendNested, UnionsEnumsAndAnonymousStructsAreNotNestedClasses) {
    const Read r = read("class A { public: union U { int i; }; enum class E { X }; struct { int a; } anon; };");
    EXPECT_TRUE(the_class(r, "A").nested_classes.empty());
    EXPECT_EQ(the_class(r, "A").nested_types, (std::vector<std::string>{"U", "E"})) << "still types of the class, by name";
}

TEST(CodeFrontendNested, ANestedClassOfAnIfBranchIsReadOnce) {
    const Read r = read("class A { public:\n#ifdef X\n struct N { void f(); };\n#else\n struct N { void f(); void g(); };\n#endif\n};");
    const ir::Class& a = the_class(r, "A");
    ASSERT_EQ(a.nested_classes.size(), 1u);
    EXPECT_EQ(method_names(a.nested_classes[0]), (std::vector<std::string>{"f", "g"}));
}

TEST(CodeFrontendNested, MethodsOfANestedClassAreNotMethodsOfTheOuter) {
    const Read r = read("class A { public: struct N { void inner(); }; void outer(); };");
    EXPECT_EQ(method_names(the_class(r, "A")), std::vector<std::string>{"outer"});
}

TEST(CodeFrontendNested, AConstructorOfANestedClassIsAConstructor) {
    const Read r = read("class A { public: struct N { N(int); ~N(); }; };");
    const ir::Class& n = the_class(r, "A").nested_classes[0];
    ASSERT_EQ(n.methods.size(), 2u);
    EXPECT_EQ(n.methods[0].kind, MethodKind::Constructor);
    EXPECT_EQ(n.methods[1].kind, MethodKind::Destructor);
}

// ---- what is not supported, said out loud (6.4) ------------------------------------------------------------------------

TEST(CodeFrontendUnsupported, SpecialisationsAreSkippedWithAWarning) {
    const Read r = read(
        "template <typename T> class Box { public: void put(T); };\n"
        "template <> class Box<int> { public: void put_int(int); };\n"
        "template <typename T> class Box<T*> { public: void put_ptr(T*); };\n");
    ASSERT_EQ(r.model.classes.size(), 1u) << "only the primary template";
    EXPECT_EQ(method_names(r.model.classes[0]), std::vector<std::string>{"put"});
    const std::vector<core::Diagnostic> w = with_code(r, "mt-unsupported");
    ASSERT_EQ(w.size(), 2u);
    EXPECT_EQ(w[0].message, "the specialisation 'Box<int>' is not supported and is skipped");
    EXPECT_EQ(w[1].message, "the specialisation 'Box<T*>' is not supported and is skipped");
    EXPECT_EQ(w[0].severity, core::Severity::Warning);
    EXPECT_EQ(w[0].where.line, 2u);
}

TEST(CodeFrontendUnsupported, ConceptsAndRequiresClauses) {
    const Read r = read(
        "template <typename T> concept Small = sizeof(T) < 8;\n"
        "template <typename T> requires Small<T> class R { public: void g(); };\n"
        "template <typename T> class S { public: void h() requires Small<T>; void ok(); };\n");
    const std::vector<core::Diagnostic> w = with_code(r, "mt-unsupported");
    std::vector<std::string> messages;
    for (const core::Diagnostic& d : w) messages.push_back(d.message);
    EXPECT_EQ(messages, (std::vector<std::string>{"concept 'Small' is not supported and is skipped",
                                                  "the requires-clause 'requires Small<T>' is ignored",
                                                  "the requires-clause of 'h' is ignored"}));
    // The classes are still read: a header that almost parses is far more useful than none.
    EXPECT_EQ(method_names(the_class(r, "R")), std::vector<std::string>{"g"});
    EXPECT_EQ(method_names(the_class(r, "S")), (std::vector<std::string>{"h", "ok"}));
}

TEST(CodeFrontendUnsupported, InheritedConstructorsAreWarnedAbout) {
    const Read r = read("class D : public B { public: using B::B; using B::f; void g(); };");
    const std::vector<core::Diagnostic> w = with_code(r, "mt-unsupported");
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w[0].message, "'using B::B;' inherits constructors, which are not supported");
}

TEST(CodeFrontendUnsupported, OtherUsingDeclarationsAreNotConstructorInheritance) {
    const Read r = read("class D : public ns::B { public: using ns::B::f; using std::swap; using Alias = int; };");
    EXPECT_TRUE(with_code(r, "mt-unsupported").empty());
}

TEST(CodeFrontendUnsupported, QualifiedInheritedConstructors) {
    const Read r = read("class D : public ns::B { public: using ns::B::B; };");
    EXPECT_EQ(with_code(r, "mt-unsupported").size(), 1u);
}

TEST(CodeFrontendUnsupported, FunctionTemplatesStillWarn) {
    const Read r = read("template <typename T> T twice(T x);\nint plain(int);\n");
    EXPECT_EQ(with_code(r, "mt-unsupported").size(), 1u);
    EXPECT_EQ(r.model.functions.size(), 1u);
}
