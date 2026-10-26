// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/syntax/extract.h"
#include "mirrortest/syntax/tree.h"

using namespace mt::syntax;
using mt::ir::Origin;
using mt::ir::RefKind;

namespace {

// Parses a source and hands out the members of its first class, so a test can
// write `class A { <the declaration> };` and look at that declaration.
class Parsed {
public:
    explicit Parsed(std::string source) : tree_(parse_cpp(std::move(source))) {}

    // The index-th named child of the first class body (comments included).
    Node member(unsigned index) const {
        const Node cls = find_first(tree_.root(), "class_specifier");
        return cls.child_by_field("body").named_child(index);
    }

    bool clean() const { return !tree_.has_error(); }
    const Tree& tree() const { return tree_; }

private:
    Tree tree_;
};

Parsed in_class(const std::string& members) {
    Parsed p("class A {\n" + members + "\n};\n");
    EXPECT_TRUE(p.clean()) << p.tree().root().sexp();
    return p;
}

// For a declaration with a function declarator.
const Node function_of(const Declarator& d) { return d.function; }

}  // namespace

// ---- finding nodes -------------------------------------------------------------------

TEST(ExtractFind, FindFirstAndAllInSourceOrder) {
    const Tree tree = parse_cpp("int f() { return g(1) + h(2); } int k() { return g(3); }");
    const std::vector<Node> calls = find_all(tree.root(), "call_expression");
    ASSERT_EQ(calls.size(), 3u);
    EXPECT_EQ(calls[0].text(), "g(1)");
    EXPECT_EQ(calls[1].text(), "h(2)");
    EXPECT_EQ(calls[2].text(), "g(3)");
    EXPECT_EQ(find_first(tree.root(), "call_expression").text(), "g(1)");
    EXPECT_TRUE(find_first(tree.root(), "template_declaration").is_null());
}

TEST(ExtractFind, WalkCanSkipASubtree) {
    const Tree tree = parse_cpp("void f() { a(); } void g() { b(); }");
    std::vector<std::string> seen;
    walk(tree.root(), [&](const Node& n) {
        if (n.type() == "call_expression") seen.emplace_back(n.text());
        return n.type() != "function_definition" || n.child_by_field("declarator").text().rfind("f", 0) != 0;
    });
    EXPECT_EQ(seen, std::vector<std::string>{"b()"}) << "the body of f was skipped";
}

TEST(ExtractFind, NullRootIsHarmless) {
    EXPECT_TRUE(find_all(Node(), "x").empty());
    EXPECT_TRUE(find_first(Node(), "x").is_null());
    EXPECT_FALSE(has_token(Node(), "virtual"));
}

TEST(ExtractFind, AnonymousKeywordsAreFoundAsTokens) {
    const Parsed p = in_class("virtual void a(); void b();");
    EXPECT_TRUE(has_token(p.member(0), "virtual"));
    EXPECT_FALSE(has_token(p.member(1), "virtual"));
}

TEST(ExtractFind, ChildrenByFieldReturnsEveryDeclarator) {
    const Parsed p = in_class("int a, *b, &c = ref;");
    const std::vector<Node> declarators = children_by_field(p.member(0), "declarator");
    ASSERT_EQ(declarators.size(), 3u);
    EXPECT_EQ(declarators[0].text(), "a");
    EXPECT_EQ(declarators[1].text(), "*b");
    EXPECT_EQ(declarators[2].text(), "&c");
}

TEST(ExtractFind, LocationIsTheStartOfTheNode) {
    const Parsed p = in_class("int x;");
    const mt::ir::SourceLoc loc = location(p.member(0), "tests/foo.h");
    EXPECT_EQ(loc.file, "tests/foo.h");
    EXPECT_EQ(loc.line, 2u);
    EXPECT_EQ(loc.column, 1u);
}

// ---- declarators --------------------------------------------------------------------------

TEST(ExtractDeclarator, AFunctionReturningAPointer) {
    const Parsed p = in_class("static Widget* create(const std::string& name, std::size_t n = 0u);");
    const Declarator d = parse_declarator(p.member(0).child_by_field("declarator"));
    EXPECT_EQ(d.name, "create");
    EXPECT_EQ(d.suffix, "*") << "the star belongs to the return type";
    ASSERT_FALSE(d.function.is_null());
    EXPECT_EQ(d.function.type(), "function_declarator");
    EXPECT_FALSE(d.is_function_pointer);
}

TEST(ExtractDeclarator, ConstructorsDestructorsAndOperators) {
    const Parsed p = in_class("explicit A(int x); ~A(); A& operator=(A&&) noexcept = default;"
                              " bool operator==(const A& o) const;");
    const Declarator ctor = parse_declarator(p.member(0).child_by_field("declarator"));
    EXPECT_EQ(ctor.name, "A");
    EXPECT_EQ(ctor.suffix, "");
    EXPECT_FALSE(ctor.function.is_null());

    const Declarator dtor = parse_declarator(p.member(1).child_by_field("declarator"));
    EXPECT_EQ(dtor.name, "~A");

    const Declarator assign = parse_declarator(p.member(2).child_by_field("declarator"));
    EXPECT_EQ(assign.name, "operator=");
    EXPECT_EQ(assign.suffix, "&");

    const Declarator eq = parse_declarator(p.member(3).child_by_field("declarator"));
    EXPECT_EQ(eq.name, "operator==");
    EXPECT_EQ(eq.suffix, "");
}

TEST(ExtractDeclarator, AFunctionPointerFieldIsNotAMethod) {
    const Parsed p = in_class("int (*cb)(int);");
    const Declarator d = parse_declarator(p.member(0).child_by_field("declarator"));
    EXPECT_TRUE(d.is_function_pointer);
    EXPECT_TRUE(d.function.is_null());
    EXPECT_EQ(d.name, "cb");
    EXPECT_EQ(d.suffix, "(*)(int)");
}

TEST(ExtractDeclarator, SeveralDeclaratorsInOneDeclaration) {
    const Parsed p = in_class("int a, *b, &c = ref;");
    const std::vector<Node> nodes = children_by_field(p.member(0), "declarator");
    ASSERT_EQ(nodes.size(), 3u);
    const Declarator a = parse_declarator(nodes[0]);
    const Declarator b = parse_declarator(nodes[1]);
    const Declarator c = parse_declarator(nodes[2]);
    EXPECT_EQ(a.name, "a");
    EXPECT_EQ(a.suffix, "");
    EXPECT_EQ(b.name, "b");
    EXPECT_EQ(b.suffix, "*");
    EXPECT_EQ(c.name, "c");
    EXPECT_EQ(c.suffix, "&");
}

TEST(ExtractDeclarator, ArraysAndQualifiedPointers) {
    const Parsed p = in_class("const char* const names[3];");
    const Declarator d = parse_declarator(p.member(0).child_by_field("declarator"));
    EXPECT_EQ(d.name, "names");
    EXPECT_EQ(d.suffix, "* const[3]");
}

TEST(ExtractDeclarator, AnOutOfClassDefinitionKeepsItsQualifiedName) {
    const Tree tree = parse_cpp("int Foo::bar(int x) const { return x; }");
    const Node def = tree.root().named_child(0);
    const Declarator d = parse_declarator(def.child_by_field("declarator"));
    EXPECT_EQ(d.name, "Foo::bar");
    EXPECT_FALSE(d.function.is_null());
}

TEST(ExtractDeclarator, TheValueOfAnInitDeclaratorIsNotPartOfTheType) {
    const Tree tree = parse_cpp("void f() { int* p = nullptr; int x(3); }");
    const std::vector<Node> inits = find_all(tree.root(), "init_declarator");
    ASSERT_EQ(inits.size(), 2u);
    const Declarator p = parse_declarator(inits[0]);
    EXPECT_EQ(p.name, "p");
    EXPECT_EQ(p.suffix, "*");
    const Declarator x = parse_declarator(inits[1]);
    EXPECT_EQ(x.name, "x");
    EXPECT_EQ(x.suffix, "");
}

TEST(ExtractDeclarator, AnUnnamedParameterHasAnAbstractDeclarator) {
    const Parsed p = in_class("void f(const Widget&, int*, int);");
    const Node params = find_first(p.member(0), "parameter_list");
    const std::vector<Node> list = params.named_children();
    ASSERT_EQ(list.size(), 3u);
    const Declarator ref = parse_declarator(list[0].child_by_field("declarator"));
    EXPECT_TRUE(ref.name_node.is_null());
    EXPECT_EQ(ref.name, "");
    EXPECT_EQ(ref.suffix, "&");
    EXPECT_EQ(parse_declarator(list[1].child_by_field("declarator")).suffix, "*");
    EXPECT_EQ(parse_declarator(list[2].child_by_field("declarator")).suffix, "");
}

TEST(ExtractDeclarator, ANullDeclaratorIsEmpty) {
    const Declarator d = parse_declarator(Node());
    EXPECT_TRUE(d.name_node.is_null());
    EXPECT_EQ(d.name, "");
    EXPECT_EQ(d.suffix, "");
    EXPECT_TRUE(function_of(d).is_null());
}

// ---- specifiers -----------------------------------------------------------------------------

TEST(ExtractSpecifiers, StaticAndTheTypeText) {
    const Parsed p = in_class("static Widget* create(int);");
    const Specifiers s = read_specifiers(p.member(0));
    EXPECT_TRUE(s.is_static);
    EXPECT_EQ(s.type_text, "Widget");
    EXPECT_FALSE(s.is_virtual || s.is_explicit || s.is_inline || s.is_constexpr || s.is_mutable);
}

TEST(ExtractSpecifiers, VirtualIsAnAnonymousToken) {
    const Parsed p = in_class("virtual void apply(int) = 0;");
    EXPECT_TRUE(read_specifiers(p.member(0)).is_virtual);
}

TEST(ExtractSpecifiers, ExplicitConstructorsHaveNoType) {
    const Parsed p = in_class("explicit A(int x);");
    const Specifiers s = read_specifiers(p.member(0));
    EXPECT_TRUE(s.is_explicit);
    EXPECT_TRUE(s.type.is_null());
    EXPECT_EQ(s.type_text, "");
}

TEST(ExtractSpecifiers, InlineConstexprMutableExtern) {
    EXPECT_TRUE(read_specifiers(in_class("inline int f() const { return 1; }").member(0)).is_inline);
    const Specifiers k = read_specifiers(in_class("static constexpr int kMax = 3;").member(0));
    EXPECT_TRUE(k.is_static);
    EXPECT_TRUE(k.is_constexpr);
    EXPECT_EQ(k.type_text, "int") << "constexpr is not part of the type";
    const Specifiers m = read_specifiers(in_class("mutable int m;").member(0));
    EXPECT_TRUE(m.is_mutable);
    EXPECT_EQ(m.type_text, "int");

    const Tree tree = parse_cpp("extern int counter;");
    EXPECT_TRUE(read_specifiers(tree.root().named_child(0)).is_extern);
}

TEST(ExtractSpecifiers, ConstAndVolatileKeepTheirPlaceAroundTheType) {
    EXPECT_EQ(read_specifiers(in_class("const char* p;").member(0)).type_text, "const char");
    const Specifiers east = read_specifiers(in_class("int const x;").member(0));
    EXPECT_EQ(east.type_text, "int const") << "east const stays east const";
    EXPECT_TRUE(east.is_const);
    const Specifiers vol = read_specifiers(in_class("volatile int v;").member(0));
    EXPECT_TRUE(vol.is_volatile);
    EXPECT_EQ(vol.type_text, "volatile int");
}

TEST(ExtractSpecifiers, AttributesAreNotPartOfTheType) {
    const Specifiers s = read_specifiers(in_class("[[nodiscard]] int attr() const;").member(0));
    EXPECT_EQ(s.type_text, "int");
}

// ---- types -----------------------------------------------------------------------------------

namespace {

// The declared type of the first declarator of the member at `index`.
mt::ir::Type type_of(const Parsed& p, unsigned index, const char* file = "f.h") {
    const Node member = p.member(index);
    return make_declared_type(read_specifiers(member), parse_declarator(member.child_by_field("declarator")), file);
}

}  // namespace

TEST(ExtractType, AReferenceToConst) {
    const Parsed p = in_class("void f(const std::string& s);");
    const auto params = read_parameters(find_first(p.member(0), "parameter_list"), "f.h");
    ASSERT_EQ(params.size(), 1u);
    const mt::ir::Type& t = params[0].type;
    EXPECT_EQ(t.spelling, "const std::string&");
    EXPECT_EQ(t.base, "std::string");
    EXPECT_TRUE(t.is_const);
    EXPECT_EQ(t.ref, RefKind::LValue);
    EXPECT_EQ(t.pointer_depth, 0u);
    EXPECT_EQ(t.origin, Origin::Declared);
}

TEST(ExtractType, AReturnTypeComesFromTheDeclaratorWrappingTheFunction) {
    EXPECT_EQ(type_of(in_class("static Widget* create(int);"), 0).spelling, "Widget*");
    const mt::ir::Type t = type_of(in_class("static Widget* create(int);"), 0);
    EXPECT_EQ(t.pointer_depth, 1u);
    EXPECT_EQ(t.base, "Widget");
    EXPECT_EQ(type_of(in_class("A& operator=(const A&);"), 0).spelling, "A&");
    EXPECT_EQ(type_of(in_class("const std::string& name() const;"), 0).spelling, "const std::string&");
    EXPECT_EQ(type_of(in_class("void run();"), 0).spelling, "void");
    EXPECT_TRUE(type_of(in_class("void run();"), 0).is_void());
}

TEST(ExtractType, ConstOnThePointerIsNotConstOnTheBase) {
    const mt::ir::Type both = type_of(in_class("const char* const names;"), 0);
    EXPECT_EQ(both.spelling, "const char* const");
    EXPECT_EQ(both.base, "char");
    EXPECT_TRUE(both.is_const) << "const char*: the pointee is const";
    EXPECT_EQ(both.pointer_depth, 1u);
    EXPECT_EQ(both.ref, RefKind::None);

    const mt::ir::Type pointer_only = type_of(in_class("char* const p;"), 0);
    EXPECT_EQ(pointer_only.spelling, "char* const");
    EXPECT_FALSE(pointer_only.is_const);
    EXPECT_EQ(pointer_only.pointer_depth, 1u);
}

TEST(ExtractType, EastConstIsKeptAsWritten) {
    const mt::ir::Type t = type_of(in_class("int const x;"), 0);
    EXPECT_EQ(t.spelling, "int const");
    EXPECT_TRUE(t.is_const);
    EXPECT_EQ(t.base, "int");
}

TEST(ExtractType, PointersAndReferencesCompose) {
    const mt::ir::Type rr = type_of(in_class("int*&& p;"), 0);
    EXPECT_EQ(rr.spelling, "int*&&");
    EXPECT_EQ(rr.pointer_depth, 1u);
    EXPECT_EQ(rr.ref, RefKind::RValue);

    const mt::ir::Type pp = type_of(in_class("char** argv;"), 0);
    EXPECT_EQ(pp.pointer_depth, 2u);
}

TEST(ExtractType, ConstInsideTemplateArgumentsDoesNotMakeTheBaseConst) {
    const mt::ir::Type t = type_of(in_class("std::vector<const int>& v;"), 0);
    EXPECT_EQ(t.base, "std::vector<const int>");
    EXPECT_FALSE(t.is_const);
    EXPECT_EQ(t.ref, RefKind::LValue);
}

TEST(ExtractType, TemplateAndMultiWordTypes) {
    EXPECT_EQ(type_of(in_class("std::map<int, std::vector<std::string>> table() const;"), 0).spelling,
              "std::map<int, std::vector<std::string>>");
    EXPECT_EQ(type_of(in_class("unsigned long long big() const;"), 0).spelling, "unsigned long long");
    EXPECT_EQ(type_of(in_class("::ns::Foo x;"), 0).spelling, "::ns::Foo");
}

TEST(ExtractType, FunctionPointersAndArraysKeepTheirSpellingAndClaimNothingElse) {
    const mt::ir::Type fp = type_of(in_class("int (*cb)(int);"), 0);
    EXPECT_EQ(fp.spelling, "int(*)(int)");
    EXPECT_EQ(fp.base, fp.spelling);
    EXPECT_EQ(fp.pointer_depth, 0u);
    EXPECT_EQ(fp.ref, RefKind::None);
    EXPECT_FALSE(fp.is_const);

    const mt::ir::Type arr = type_of(in_class("const int table[4];"), 0);
    EXPECT_EQ(arr.spelling, "const int[4]");
    EXPECT_EQ(arr.base, arr.spelling);
    EXPECT_FALSE(arr.is_const) << "not decomposed";
}

TEST(ExtractType, AutoIsReportedAsTheBase) {
    const Tree tree = parse_cpp("void f() { auto a = g(); const auto& b = g(); }");
    const std::vector<Node> declarations = find_all(tree.root(), "declaration");
    ASSERT_EQ(declarations.size(), 2u);
    const mt::ir::Type a = make_declared_type(read_specifiers(declarations[0]),
                                              parse_declarator(declarations[0].child_by_field("declarator")), "f.cpp");
    EXPECT_EQ(a.base, "auto");
    EXPECT_EQ(a.spelling, "auto");
    const mt::ir::Type b = make_declared_type(read_specifiers(declarations[1]),
                                              parse_declarator(declarations[1].child_by_field("declarator")), "f.cpp");
    EXPECT_EQ(b.spelling, "const auto&");
    EXPECT_EQ(b.base, "auto");
    EXPECT_TRUE(b.is_const);
}

TEST(ExtractType, ConstructorsHaveNoType) {
    EXPECT_TRUE(type_of(in_class("explicit A(int x);"), 0).empty());
    EXPECT_TRUE(type_of(in_class("~A();"), 0).empty());
}

TEST(ExtractType, CarriesItsPosition) {
    const mt::ir::Type t = type_of(in_class("\nstd::string name() const;"), 0, "include/foo.h");
    EXPECT_EQ(t.where.file, "include/foo.h");
    EXPECT_EQ(t.where.line, 3u);
    EXPECT_EQ(t.where.column, 1u);
}

// ---- function traits ----------------------------------------------------------------------------

namespace {

FunctionTraits traits_of(const std::string& member) {
    const Parsed p = in_class(member);
    return read_function_traits(parse_declarator(p.member(0).child_by_field("declarator")).function, "f.h");
}

}  // namespace

TEST(ExtractTraits, ConstAndNoexcept) {
    const FunctionTraits t = traits_of("int value() const noexcept;");
    EXPECT_TRUE(t.is_const);
    EXPECT_TRUE(t.is_noexcept);
    EXPECT_FALSE(t.is_override || t.is_final || t.is_volatile);
    EXPECT_EQ(t.ref_qualifier, RefKind::None);
    EXPECT_FALSE(t.has_trailing_return);
}

TEST(ExtractTraits, NothingSetByDefault) {
    const FunctionTraits t = traits_of("void run();");
    EXPECT_FALSE(t.is_const || t.is_noexcept || t.is_override || t.is_final || t.is_volatile);
    EXPECT_EQ(t.ref_qualifier, RefKind::None);
}

TEST(ExtractTraits, OverrideAndFinal) {
    EXPECT_TRUE(traits_of("~A() override;").is_override);
    const FunctionTraits fin = traits_of("virtual int fin() const final;");
    EXPECT_TRUE(fin.is_final);
    EXPECT_TRUE(fin.is_const);
    EXPECT_FALSE(fin.is_override);
}

TEST(ExtractTraits, RefQualifiers) {
    EXPECT_EQ(traits_of("void g() &;").ref_qualifier, RefKind::LValue);
    const FunctionTraits h = traits_of("void h() const &&;");
    EXPECT_EQ(h.ref_qualifier, RefKind::RValue);
    EXPECT_TRUE(h.is_const);
}

TEST(ExtractTraits, OnlyAConstantTrueNoexceptCounts) {
    EXPECT_TRUE(traits_of("void a() noexcept;").is_noexcept);
    EXPECT_TRUE(traits_of("void b() noexcept(true);").is_noexcept);
    EXPECT_TRUE(traits_of("void c() noexcept ( true );").is_noexcept);
    EXPECT_FALSE(traits_of("void d() noexcept(false);").is_noexcept);
    EXPECT_FALSE(traits_of("void e() noexcept(sizeof(int) > 2);").is_noexcept)
        << "a conditional noexcept is not claimed";
}

TEST(ExtractTraits, TrailingReturnType) {
    const FunctionTraits t = traits_of("auto f() -> int;");
    ASSERT_TRUE(t.has_trailing_return);
    EXPECT_EQ(t.trailing_return.spelling, "int");

    const FunctionTraits u = traits_of("auto g() const -> const std::string&;");
    ASSERT_TRUE(u.has_trailing_return);
    EXPECT_EQ(u.trailing_return.spelling, "const std::string&");
    EXPECT_TRUE(u.trailing_return.is_const);
    EXPECT_EQ(u.trailing_return.ref, RefKind::LValue);
    EXPECT_TRUE(u.is_const);
}

// ---- parameters ------------------------------------------------------------------------------------

namespace {

std::vector<mt::ir::Param> params_of(const std::string& member) {
    const Parsed p = in_class(member);
    return read_parameters(find_first(p.member(0), "parameter_list"), "f.h");
}

}  // namespace

TEST(ExtractParams, NamesTypesAndDefaults) {
    const auto params = params_of("void f(const std::string& name, std::size_t n = 0u);");
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].type.spelling, "const std::string&");
    EXPECT_EQ(params[0].name, "name");
    EXPECT_FALSE(params[0].default_value.has_value());
    EXPECT_EQ(params[1].type.spelling, "std::size_t");
    EXPECT_EQ(params[1].name, "n");
    EXPECT_EQ(params[1].default_value, std::optional<std::string>("0u"));
}

TEST(ExtractParams, NoneAndVoid) {
    EXPECT_TRUE(params_of("void f();").empty());
    EXPECT_TRUE(params_of("void f(void);").empty());
}

TEST(ExtractParams, UnnamedParametersHaveEmptyNames) {
    const auto params = params_of("void f(const Widget&, int*, double);");
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].type.spelling, "const Widget&");
    EXPECT_EQ(params[1].type.spelling, "int*");
    EXPECT_EQ(params[2].type.spelling, "double");
    for (const auto& p : params) EXPECT_EQ(p.name, "");
}

TEST(ExtractParams, AwkwardTypes) {
    const auto params = params_of("void fn(std::function<void(int)> f, int (&arr)[4], int*&& p);");
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].type.spelling, "std::function<void(int)>");
    EXPECT_EQ(params[0].name, "f");
    EXPECT_EQ(params[1].type.spelling, "int(&)[4]");
    EXPECT_EQ(params[1].name, "arr");
    EXPECT_EQ(params[2].type.spelling, "int*&&");
    EXPECT_EQ(params[2].name, "p");
}

TEST(ExtractParams, ACommentBetweenParametersIsIgnored) {
    const auto params = params_of("void f(int a /* first */, int b);");
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[1].name, "b");
}

TEST(ExtractParams, EachParameterCarriesItsPosition) {
    const auto params = params_of("void f(int a,\n       double b);");
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].type.where.line, 2u);
    EXPECT_EQ(params[1].type.where.line, 3u);
}

TEST(ExtractParams, ACStyleEllipsisIsAParameterNotNothing) {
    // f(int, ...) must not read as f(int): it would declare the wrong function.
    const auto params = params_of("void v(int a, ...);");
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].type.spelling, "int");
    EXPECT_EQ(params[1].type.spelling, "...");
    EXPECT_EQ(params[1].name, "");
    EXPECT_EQ(params[1].type.where.line, 2u);

    const auto only = params_of("void w(...);");
    ASSERT_EQ(only.size(), 1u);
    EXPECT_EQ(only[0].type.spelling, "...");
}

TEST(ExtractParams, ParameterPacksKeepTheirEllipsisInTheType) {
    const Parsed p("template <typename... Args> void p(int n, Args&&... args, Rest... rest);");
    const Node list = find_first(p.tree().root(), "parameter_list");
    const auto params = read_parameters(list, "f.h");
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].type.spelling, "int");
    EXPECT_EQ(params[1].type.spelling, "Args&&...");
    EXPECT_EQ(params[1].name, "args");
    EXPECT_EQ(params[2].type.spelling, "Rest...");
    EXPECT_EQ(params[2].name, "rest");
}

TEST(ExtractParams, ANullListIsEmpty) {
    EXPECT_TRUE(read_parameters(Node(), "f.h").empty());
}

// ---- QFETCH ---------------------------------------------------------------------------------------------

TEST(ExtractQFetch, TheTypeAndTheName) {
    const auto fetch = parse_qfetch("QFETCH(int, start)");
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->type, "int");
    EXPECT_EQ(fetch->name, "start");
}

TEST(ExtractQFetch, ACommaInsideTheTypeIsNotTheSeparator) {
    const auto map = parse_qfetch("QFETCH(std::map<int, QString>, rows)");
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->type, "std::map<int, QString>");
    EXPECT_EQ(map->name, "rows");
    const auto fn = parse_qfetch("QFETCH(std::function<int(int, int)>, cb)");
    ASSERT_TRUE(fn.has_value());
    EXPECT_EQ(fn->type, "std::function<int(int, int)>");
    EXPECT_EQ(fn->name, "cb");
    const auto arr = parse_qfetch("QFETCH(std::array<int, 3>, a)");
    ASSERT_TRUE(arr.has_value());
    EXPECT_EQ(arr->type, "std::array<int, 3>");
}

TEST(ExtractQFetch, TypesWithSeveralWordsAndSpaces) {
    const auto fetch = parse_qfetch("  QFETCH ( unsigned long long ,  big ) ;");
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->type, "unsigned long long");
    EXPECT_EQ(fetch->name, "big");
}

TEST(ExtractQFetch, ItIsTheLastCommaOutsideAnyBracketThatSeparates) {
    const auto fetch = parse_qfetch("QFETCH(a, b, c)");
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->type, "a, b");
    EXPECT_EQ(fetch->name, "c");
}

TEST(ExtractQFetch, TheGlobalVariant) {
    const auto fetch = parse_qfetch("QFETCH_GLOBAL(Foo, foo);");
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->type, "Foo");
    EXPECT_EQ(fetch->name, "foo");
}

TEST(ExtractQFetch, ANewlineBetweenTheParts) {
    const auto fetch = parse_qfetch("QFETCH(\n    QString,\n    label\n)");
    ASSERT_TRUE(fetch.has_value());
    EXPECT_EQ(fetch->type, "QString");
    EXPECT_EQ(fetch->name, "label");
}

TEST(ExtractQFetch, NothingElseIsAFetch) {
    for (const char* text : {"", "QFETCH", "QFETCH()", "QFETCH(int)", "QFETCH(, name)", "QFETCH(int, )", "QFETCHER(int, a)",
                             "QFETCH_X(int, a)", "XQFETCH(int, a)", "QFETCH(int, a) + 1", "QFETCH int, a", "QFETCH(int, a",
                             "foo(int, a)", "QTEST(int, a)",
                             "QFETCX(int, a)", "ABCDEF(int, a)",                    // six characters, then a parenthesis
                             "QFETCH int, a)",                                      // a closing parenthesis without an opening one
                             "QFETCH(std::map<int, QString>)",                      // the only comma is inside the brackets
                             "QFETCH(int, a), QFETCH(int, b)",                      // two macros
                             "QFETCH(int, a)) + (QFETCH(int, b)",                   // brackets that do not match
                             "QFETCH((int, a)"}) {
        EXPECT_FALSE(parse_qfetch(text).has_value()) << text;
    }
}
