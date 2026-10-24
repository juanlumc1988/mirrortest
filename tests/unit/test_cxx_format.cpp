// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Declarations, definitions and stub bodies as text.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/ir/type.h"

using namespace mt;

namespace {

ir::Param param(const char* type, const char* name = "", const char* def = nullptr) {
    ir::Param p;
    p.type = ir::type_from_spelling(type);
    p.name = name;
    if (def != nullptr) p.default_value = def;
    return p;
}

ir::Method method(const char* name, const char* ret, std::vector<ir::Param> params = {}) {
    ir::Method m;
    m.name = name;
    m.ret = ir::type_from_spelling(ret);
    m.params = std::move(params);
    return m;
}

ir::Method ctor(const char* cls, std::vector<ir::Param> params = {}) {
    ir::Method m;
    m.name = cls;
    m.kind = ir::MethodKind::Constructor;
    m.params = std::move(params);
    return m;
}

ir::Class cls(const char* name, std::vector<std::string> ns = {}) {
    ir::Class c;
    c.name = name;
    c.ns = std::move(ns);
    return c;
}

// ---- declare ---------------------------------------------------------------------------------------------------

TEST(CxxFormatDeclare, ATypeAndItsName) {
    EXPECT_EQ(backend::declare(ir::type_from_spelling("int"), "x"), "int x");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("const std::string&"), "name"), "const std::string& name");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("char*"), "p"), "char* p");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("unsigned int"), "n"), "unsigned int n");
}

TEST(CxxFormatDeclare, NoNameIsTheBareType) {
    EXPECT_EQ(backend::declare(ir::type_from_spelling("int"), ""), "int");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("void (*)(int)"), ""), "void(*)(int)");   // spellings are normalised
}

TEST(CxxFormatDeclare, TheNameGoesWhereC11WantsIt) {
    EXPECT_EQ(backend::declare(ir::type_from_spelling("void (*)(int)"), "cb"), "void(*cb)(int)");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("int (&)[3]"), "a"), "int(&a)[3]");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("int[3]"), "values"), "int values[3]");
    EXPECT_EQ(backend::declare(ir::type_from_spelling("char[16]"), "buf"), "char buf[16]");
}

// ---- method declarations --------------------------------------------------------------------------------------

TEST(CxxFormatMethod, AnOrdinaryMethodAndItsQualifiers) {
    ir::Method m = method("size", "unsigned int");
    EXPECT_EQ(backend::method_declaration(m), "unsigned int size();");
    m.is_const = true;
    EXPECT_EQ(backend::method_declaration(m), "unsigned int size() const;");
    m.is_noexcept = true;
    EXPECT_EQ(backend::method_declaration(m), "unsigned int size() const noexcept;");
    m.ref_qualifier = ir::RefKind::LValue;
    EXPECT_EQ(backend::method_declaration(m), "unsigned int size() const & noexcept;");
    m.ref_qualifier = ir::RefKind::RValue;
    EXPECT_EQ(backend::method_declaration(m), "unsigned int size() const && noexcept;");
}

TEST(CxxFormatMethod, ParametersKeepTheirNamesAndDefaultsInTheDeclaration) {
    const ir::Method m = method("set", "void", {param("int", "index"), param("const std::string&", "value", "\"\""), param("double")});
    EXPECT_EQ(backend::method_declaration(m), "void set(int index, const std::string& value = \"\", double);");
}

TEST(CxxFormatMethod, StaticVirtualAndTheOverrideFamily) {
    ir::Method m = method("make", "Foo");
    m.is_static = true;
    EXPECT_EQ(backend::method_declaration(m), "static Foo make();");

    ir::Method v = method("run", "void");
    v.is_virtual = true;
    EXPECT_EQ(backend::method_declaration(v), "virtual void run();");
    v.is_pure = true;
    EXPECT_EQ(backend::method_declaration(v), "virtual void run() = 0;");
    v.is_pure = false;
    v.is_override = true;
    v.is_final = true;
    EXPECT_EQ(backend::method_declaration(v), "virtual void run() override final;");

    ir::Method d = method("gone", "void");
    d.is_deleted = true;
    EXPECT_EQ(backend::method_declaration(d), "void gone() = delete;");
    ir::Method f = method("kept", "void");
    f.is_defaulted = true;
    EXPECT_EQ(backend::method_declaration(f), "void kept() = default;");
}

TEST(CxxFormatMethod, ConstructorsAndDestructorsHaveNoReturnType) {
    ir::Method c = ctor("Foo", {param("int", "arg1")});
    c.is_explicit = true;
    EXPECT_EQ(backend::method_declaration(c), "explicit Foo(int arg1);");
    EXPECT_EQ(backend::method_declaration(ctor("Foo")), "Foo();");

    ir::Method d;
    d.name = "~Foo";
    d.kind = ir::MethodKind::Destructor;
    d.is_virtual = true;
    EXPECT_EQ(backend::method_declaration(d), "virtual ~Foo();");
}

TEST(CxxFormatMethod, OperatorsAndConversions) {
    ir::Method op = method("operator==", "bool", {param("const Foo&", "other")});
    op.is_const = true;
    EXPECT_EQ(backend::method_declaration(op), "bool operator==(const Foo& other) const;");

    ir::Method conv;
    conv.name = "operator bool";
    conv.kind = ir::MethodKind::Conversion;
    conv.is_const = true;
    conv.is_explicit = true;
    EXPECT_EQ(backend::method_declaration(conv), "explicit operator bool() const;");
}

// ---- method definitions -----------------------------------------------------------------------------------------

TEST(CxxFormatDefinition, TheHeadHasTheClassQualifierAndCommentedNames) {
    const ir::Class foo = cls("Foo", {"acme"});
    EXPECT_EQ(backend::method_definition_head(foo, method("size", "unsigned int")), "unsigned int Foo::size()");
    ir::Method m = method("set", "void", {param("int", "index"), param("const std::string&", "value", "\"\""), param("double")});
    m.is_const = true;
    EXPECT_EQ(backend::method_definition_head(foo, m), "void Foo::set(int /*index*/, const std::string& /*value*/, double) const");
}

TEST(CxxFormatDefinition, ConstructorsAndDestructors) {
    const ir::Class foo = cls("Foo");
    EXPECT_EQ(backend::method_definition_head(foo, ctor("Foo", {param("int", "arg1")})), "Foo::Foo(int /*arg1*/)");
    ir::Method d;
    d.name = "~Foo";
    d.kind = ir::MethodKind::Destructor;
    EXPECT_EQ(backend::method_definition_head(foo, d), "Foo::~Foo()");
    ir::Method conv;
    conv.name = "operator bool";
    conv.kind = ir::MethodKind::Conversion;
    conv.is_const = true;
    EXPECT_EQ(backend::method_definition_head(foo, conv), "Foo::operator bool() const");
}

TEST(CxxFormatDefinition, OnlyTheDeclarationCarriesStaticVirtualExplicitOverrideAndDefaults) {
    const ir::Class foo = cls("Foo");
    ir::Method m = method("make", "Foo", {param("int", "n", "3")});
    m.is_static = true;
    m.is_virtual = true;
    m.is_override = true;
    m.is_final = true;
    EXPECT_EQ(backend::method_definition_head(foo, m), "Foo Foo::make(int /*n*/)");
    ir::Method c = ctor("Foo", {param("int", "n")});
    c.is_explicit = true;
    EXPECT_EQ(backend::method_definition_head(foo, c), "Foo::Foo(int /*n*/)");
}

TEST(CxxFormatDefinition, QualifiersAreRepeatedBecauseTheyAreTheFunctionsType) {
    const ir::Class foo = cls("Foo");
    ir::Method m = method("f", "int");
    m.is_const = true;
    m.is_noexcept = true;
    m.ref_qualifier = ir::RefKind::RValue;
    EXPECT_EQ(backend::method_definition_head(foo, m), "int Foo::f() const && noexcept");
}

TEST(CxxFormatDefinition, AnUnnamedParameterIsJustItsType) {
    EXPECT_EQ(backend::method_definition_head(cls("Foo"), method("f", "void", {param("int"), param("char", "c")})), "void Foo::f(int, char /*c*/)");
}

TEST(CxxFormatDefinition, WhichMethodsAreDefinedInTheSourceFile) {
    ir::Method m = method("f", "void");
    EXPECT_TRUE(backend::needs_definition(m));
    ir::Method pure = m;
    pure.is_pure = true;
    ir::Method deleted = m;
    deleted.is_deleted = true;
    ir::Method defaulted = m;
    defaulted.is_defaulted = true;
    EXPECT_FALSE(backend::needs_definition(pure));
    EXPECT_FALSE(backend::needs_definition(deleted));
    EXPECT_FALSE(backend::needs_definition(defaulted));
}

// ---- fields ------------------------------------------------------------------------------------------------------

TEST(CxxFormatField, DeclarationAndTheDefinitionOfAStaticOne) {
    ir::Field f;
    f.type = ir::type_from_spelling("int");
    f.name = "x";
    EXPECT_EQ(backend::field_declaration(f), "int x;");
    EXPECT_EQ(backend::field_definition(cls("Point"), f), "");

    f.is_static = true;
    EXPECT_EQ(backend::field_declaration(f), "static int x;");
    EXPECT_EQ(backend::field_definition(cls("Point", {"acme"}), f), "int Point::x{};");

    f.type = ir::type_from_spelling("char[16]");
    f.name = "buf";
    EXPECT_EQ(backend::field_declaration(f), "static char buf[16];");
    EXPECT_EQ(backend::field_definition(cls("Point"), f), "char Point::buf[16]{};");
}

// ---- free functions -------------------------------------------------------------------------------------------

TEST(CxxFormatFunction, DeclarationAndDefinition) {
    ir::Function f;
    f.name = "add";
    f.ns = {"acme"};
    f.ret = ir::type_from_spelling("int");
    f.params = {param("int", "arg1"), param("int", "arg2", "0")};
    EXPECT_EQ(backend::function_declaration(f), "int add(int arg1, int arg2 = 0);");
    EXPECT_EQ(backend::function_definition_head(f), "int add(int /*arg1*/, int /*arg2*/)");
    EXPECT_EQ(backend::function_signature(f), "acme::add(int, int)");
    f.is_noexcept = true;
    EXPECT_EQ(backend::function_declaration(f), "int add(int arg1, int arg2 = 0) noexcept;");
    EXPECT_EQ(backend::function_definition_head(f), "int add(int /*arg1*/, int /*arg2*/) noexcept");
}

TEST(CxxFormatFunction, NoParametersAndNoNamespace) {
    ir::Function f;
    f.name = "tick";
    f.ret = ir::type_from_spelling("void");
    EXPECT_EQ(backend::function_declaration(f), "void tick();");
    EXPECT_EQ(backend::function_definition_head(f), "void tick()");
    EXPECT_EQ(backend::function_signature(f), "tick()");
}

// ---- messages -------------------------------------------------------------------------------------------------

TEST(CxxFormatSignature, QualifiedWithTypesAndTheQualifiersThatTellOverloadsApart) {
    const ir::Class foo = cls("Foo", {"acme", "core"});
    ir::Method m = method("at", "int", {param("std::size_t", "index")});
    EXPECT_EQ(backend::method_signature(foo, m), "acme::core::Foo::at(std::size_t)");
    m.is_const = true;
    EXPECT_EQ(backend::method_signature(foo, m), "acme::core::Foo::at(std::size_t) const");
    m.ref_qualifier = ir::RefKind::LValue;
    EXPECT_EQ(backend::method_signature(foo, m), "acme::core::Foo::at(std::size_t) const &");
    m.is_noexcept = true;
    EXPECT_EQ(backend::method_signature(foo, m), "acme::core::Foo::at(std::size_t) const &");
    EXPECT_EQ(backend::method_signature(cls("Foo"), method("f", "void", {param("int", "a"), param("const char*", "b")})),
              "Foo::f(int, const char*)");
}

TEST(CxxFormatSignature, TodoComments) {
    EXPECT_EQ(backend::todo_comment("return type inferred from literal 3u (t.cpp:12)"),
              "// TODO(mt): return type inferred from literal 3u (t.cpp:12)");
}

// ---- stubs --------------------------------------------------------------------------------------------------------

const ir::Type kInt = ir::type_from_spelling("int");
const ir::Type kVoid = ir::type_from_spelling("void");
const ir::Type kRef = ir::type_from_spelling("const std::string&");

TEST(CxxFormatStub, ThrowIsTheDefaultAndNeedsStdexcept) {
    const backend::Stub s = backend::make_stub(core::StubPolicy::Throw, kInt, false, true, "acme::Foo::size() const");
    ASSERT_EQ(s.body.size(), 1u);
    EXPECT_EQ(s.body[0], "throw std::logic_error(\"mirrortest: not implemented: acme::Foo::size() const\");");
    EXPECT_EQ(s.include, "stdexcept");
    EXPECT_FALSE(s.fell_back);
}

TEST(CxxFormatStub, ThrowEvenForVoidAndForReferences) {
    for (const ir::Type& t : {kVoid, kRef}) {
        const backend::Stub s = backend::make_stub(core::StubPolicy::Throw, t, false, false, "f()");
        ASSERT_EQ(s.body.size(), 1u) << t.spelling;
        EXPECT_EQ(s.body[0], "throw std::logic_error(\"mirrortest: not implemented: f()\");");
    }
}

TEST(CxxFormatStub, TheMessageIsEscapedSoThatTheLiteralIsValid) {
    const backend::Stub s = backend::make_stub(core::StubPolicy::Throw, kInt, false, true, "f(char \"x\" \\)");
    ASSERT_EQ(s.body.size(), 1u);
    EXPECT_EQ(s.body[0], "throw std::logic_error(\"mirrortest: not implemented: f(char \\\"x\\\" \\\\)\");");
}

TEST(CxxFormatStub, AbortNeedsCstdlib) {
    for (const ir::Type& t : {kInt, kVoid, kRef}) {
        const backend::Stub s = backend::make_stub(core::StubPolicy::Abort, t, false, true, "f()");
        ASSERT_EQ(s.body.size(), 1u) << t.spelling;
        EXPECT_EQ(s.body[0], "std::abort();");
        EXPECT_EQ(s.include, "cstdlib");
        EXPECT_FALSE(s.fell_back);
    }
}

TEST(CxxFormatStub, DefaultReturnsBracesAndNeedsNoInclude) {
    const backend::Stub s = backend::make_stub(core::StubPolicy::Default, kInt, false, true, "f()");
    ASSERT_EQ(s.body.size(), 1u);
    EXPECT_EQ(s.body[0], "return {};");
    EXPECT_EQ(s.include, "");
    EXPECT_FALSE(s.fell_back);
}

TEST(CxxFormatStub, DefaultForVoidIsAnEmptyBody) {
    const backend::Stub s = backend::make_stub(core::StubPolicy::Default, kVoid, false, false, "f()");
    EXPECT_TRUE(s.body.empty());
    EXPECT_EQ(s.include, "");
    EXPECT_FALSE(s.fell_back);
}

TEST(CxxFormatStub, DefaultFallsBackToAbortWhenBracesWillNotDo) {
    const backend::Stub unknown = backend::make_stub(core::StubPolicy::Default, ir::type_from_spelling("Mystery"), false, false, "f()");
    ASSERT_EQ(unknown.body.size(), 1u);
    EXPECT_EQ(unknown.body[0], "std::abort();");
    EXPECT_EQ(unknown.include, "cstdlib");
    EXPECT_TRUE(unknown.fell_back);

    // A reference is never `{}`, even if the caller thought the type was fine.
    const backend::Stub ref = backend::make_stub(core::StubPolicy::Default, kRef, false, true, "f()");
    ASSERT_EQ(ref.body.size(), 1u);
    EXPECT_EQ(ref.body[0], "std::abort();");
    EXPECT_TRUE(ref.fell_back);
}

TEST(CxxFormatStub, ConstructorsAndDestructorsAreEmptyWhateverThePolicy) {
    for (core::StubPolicy p : {core::StubPolicy::Throw, core::StubPolicy::Abort, core::StubPolicy::Default}) {
        const backend::Stub s = backend::make_stub(p, ir::Type{}, true, false, "Foo::Foo(int)");
        EXPECT_TRUE(s.body.empty());
        EXPECT_EQ(s.include, "");
        EXPECT_FALSE(s.fell_back);
    }
}

}  // namespace
