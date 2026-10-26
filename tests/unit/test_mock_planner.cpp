// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Model in, mocks out: which classes are interfaces, which of their functions a
// mock must declare, and what it leaves out.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/backend/test_planner.h"
#include "mirrortest/frontend/code_frontend.h"

using namespace mt;

namespace {

struct Planned {
    std::vector<ir::MockClass> mocks;
    core::Diagnostics          diagnostics;
};

core::Config with_mocks() {
    core::Config c;
    c.gtest.mocks = true;
    return c;
}

Planned plan(const std::string& header, core::Config config = with_mocks(), ir::Framework framework = ir::Framework::GTest) {
    Planned p;
    const ir::Model model = frontend::read_header({"foo.h", header}, "acme/foo.h", {}, p.diagnostics);
    backend::PlannerOptions options;
    options.config = std::move(config);
    options.framework = framework;
    options.header_include = "acme/foo.h";
    p.mocks = backend::plan_mocks(model, options, p.diagnostics);
    return p;
}

// "int read()", "void set(double, const std::string&) const noexcept &": a method as the mock declares it.
std::string show(const ir::MockMethod& m) {
    std::string out = m.ret.spelling + " " + m.name + "(";
    for (std::size_t i = 0; i < m.params.size(); ++i) out += (i == 0 ? "" : ", ") + m.params[i].spelling;
    out += ")";
    if (m.is_const) out += " const";
    if (m.is_noexcept) out += " noexcept";
    if (m.ref_qualifier == ir::RefKind::LValue) out += " &";
    if (m.ref_qualifier == ir::RefKind::RValue) out += " &&";
    return out;
}

std::vector<std::string> methods(const Planned& p, std::size_t mock = 0) {
    std::vector<std::string> out;
    if (mock >= p.mocks.size()) {
        ADD_FAILURE() << "no mock #" << mock;
        return out;
    }
    for (const ir::MockMethod& m : p.mocks[mock].methods) out.push_back(show(m));
    return out;
}

std::vector<std::string> names(const Planned& p) {
    std::vector<std::string> out;
    for (const ir::MockClass& m : p.mocks) out.push_back(m.name);
    return out;
}

using Lines = std::vector<std::string>;

}  // namespace

// ---- when ---------------------------------------------------------------------------------------------------

TEST(MockPlannerWhen, NotUnlessAskedFor) {
    EXPECT_TRUE(plan("class I { public: virtual void f() = 0; };", core::Config{}).mocks.empty());
}

TEST(MockPlannerWhen, NotForQtTest) {
    EXPECT_TRUE(plan("class I { public: virtual void f() = 0; };", with_mocks(), ir::Framework::QTest).mocks.empty());
}

TEST(MockPlannerWhen, OneMockForEveryInterfaceAndNoneForTheRest) {
    const Planned p = plan(
        "class Plain { public: int size() const; virtual void v(); };\n"
        "class A { public: virtual void a() = 0; };\n"
        "class Concrete : public A { public: void a() override; };\n"
        "class B { public: virtual int b() const = 0; };\n");
    EXPECT_EQ(names(p), (Lines{"MockA", "MockB"}));
}

TEST(MockPlannerWhen, AClassWithOnlyAPureVirtualDestructorIsNotAnInterfaceToMock) {
    EXPECT_TRUE(plan("class A { public: virtual ~A() = 0; };").mocks.empty());
}

TEST(MockPlannerWhen, NothingInTheHeaderIsFine) {
    EXPECT_TRUE(plan("").mocks.empty());
    EXPECT_TRUE(plan("int add(int, int);").mocks.empty());
}

// ---- names and places ------------------------------------------------------------------------------------------

TEST(MockPlannerNames, TheMockAndItsFileFollowThePatterns) {
    const Planned p = plan("namespace acme { namespace hw { class TempSensor { public: virtual int read() = 0; }; } }");
    ASSERT_EQ(p.mocks.size(), 1u);
    EXPECT_EQ(p.mocks[0].name, "MockTempSensor");
    EXPECT_EQ(p.mocks[0].file, "mock_temp_sensor.h");
    EXPECT_EQ(p.mocks[0].base, "TempSensor");
    EXPECT_EQ(p.mocks[0].ns, (std::vector<std::string>{"acme", "hw"}));
    EXPECT_EQ(p.mocks[0].header, "acme/foo.h");
}

TEST(MockPlannerNames, TheConfigurationChangesBoth) {
    core::Config c = with_mocks();
    c.naming.mock_class = "{Class}Mock";
    c.naming.mock_file = "mocks/{stem}_mock.h";
    const Planned p = plan("class TempSensor { public: virtual int read() = 0; };", c);
    ASSERT_EQ(p.mocks.size(), 1u);
    EXPECT_EQ(p.mocks[0].name, "TempSensorMock");
    EXPECT_EQ(p.mocks[0].file, "mocks/temp_sensor_mock.h");
}

TEST(MockPlannerNames, AMockTheHeaderHasAlreadyIsNotMadeAgain) {
    const Planned p = plan(
        "class Sensor { public: virtual int read() = 0; };\n"
        "class MockSensor : public Sensor { public: int read() override; };\n");
    EXPECT_TRUE(p.mocks.empty());
    ASSERT_EQ(p.diagnostics.all().size(), 1u);
    EXPECT_EQ(p.diagnostics.all()[0].severity, core::Severity::Note);
    EXPECT_EQ(p.diagnostics.all()[0].code, core::code::unsupported);
    EXPECT_EQ(p.diagnostics.all()[0].message, "the header declares 'MockSensor' already; no mock is generated for 'Sensor'");
}

// ---- functions ----------------------------------------------------------------------------------------------------

TEST(MockPlannerMethods, EveryPureVirtualFunctionInOrder) {
    const Planned p = plan(
        "class Sensor {\n"
        "public:\n"
        "    virtual ~Sensor() = default;\n"
        "    virtual int read() = 0;\n"
        "    virtual void calibrate(double offset, const std::string& unit) = 0;\n"
        "    virtual bool ok() const = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), (Lines{"int read()", "void calibrate(double, const std::string&)", "bool ok() const"}));
}

TEST(MockPlannerMethods, WhatIsNotPureIsNotMocked) {
    const Planned p = plan(
        "class Sensor {\n"
        "public:\n"
        "    Sensor();\n"
        "    virtual ~Sensor();\n"
        "    virtual int read() = 0;\n"
        "    virtual void notify();\n"
        "    int plain() const;\n"
        "    static int count();\n"
        "};\n");
    EXPECT_EQ(methods(p), Lines{"int read()"});
}

TEST(MockPlannerMethods, ConstNoexceptAndReferenceQualifiers) {
    const Planned p = plan(
        "class I {\n"
        "public:\n"
        "    virtual int a() const noexcept = 0;\n"
        "    virtual void b() & = 0;\n"
        "    virtual void c() && = 0;\n"
        "    virtual void d() const & noexcept = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), (Lines{"int a() const noexcept", "void b() &", "void c() &&", "void d() const noexcept &"}));
}

TEST(MockPlannerMethods, ProtectedAndPrivateOnesToo) {
    // A mock that left one out would still be abstract.
    const Planned p = plan(
        "class I {\n"
        "public:\n    virtual void a() = 0;\n"
        "protected:\n    virtual void b() = 0;\n"
        "private:\n    virtual void c() = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), (Lines{"void a()", "void b()", "void c()"}));
}

TEST(MockPlannerMethods, OverloadsAreDifferentFunctions) {
    const Planned p = plan(
        "class I {\n"
        "public:\n"
        "    virtual void put(int) = 0;\n"
        "    virtual void put(double) = 0;\n"
        "    virtual void put(int) const = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), (Lines{"void put(int)", "void put(double)", "void put(int) const"}));
}

TEST(MockPlannerMethods, TypesWithCommasAndSmartPointers) {
    const Planned p = plan(
        "class I {\n"
        "public:\n"
        "    virtual std::map<int, std::string> table(const std::map<int, int>& in) const = 0;\n"
        "    virtual std::unique_ptr<I> clone(std::unique_ptr<I> other, void (*cb)(int, int)) = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), (Lines{"std::map<int, std::string> table(const std::map<int, int>&) const",
                                 "std::unique_ptr<I> clone(std::unique_ptr<I>, void(*)(int, int))"}));
}

TEST(MockPlannerMethods, NestedTypesAreNamedThroughTheirClass) {
    const Planned p = plan(
        "class I {\n"
        "public:\n"
        "    struct Reading { int v; };\n"
        "    enum class Mode { A };\n"
        "    virtual Reading last(Mode m, std::vector<Reading> all) const = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), Lines{"I::Reading last(I::Mode, std::vector<I::Reading>) const"});
}

TEST(MockPlannerMethods, DefaultArgumentsAreNotPartOfTheMock) {
    const Planned p = plan("class I { public: virtual void f(int a, int b = 3) = 0; };");
    EXPECT_EQ(methods(p), Lines{"void f(int, int)"});
}

// ---- what gmock cannot mock ---------------------------------------------------------------------------------------

TEST(MockPlannerLeftOut, OperatorsConversionsAndVariadicFunctionsAreNotesOnTheMock) {
    const Planned p = plan(
        "class I {\n"
        "public:\n"
        "    virtual int a() = 0;\n"
        "    virtual bool operator==(const I& other) const = 0;\n"
        "    virtual operator bool() const = 0;\n"
        "    virtual void log(const char* fmt, ...) = 0;\n"
        "};\n");
    EXPECT_EQ(methods(p), Lines{"int a()"});
    EXPECT_EQ(p.mocks[0].notes,
              (Lines{"'I::operator==' is pure virtual and gmock cannot mock an operator: declare it by hand",
                     "'I::operator bool' is pure virtual and gmock cannot mock an operator: declare it by hand",
                     "'I::log' is pure virtual and gmock cannot mock a C-style variadic function: declare it by hand"}));
}

TEST(MockPlannerLeftOut, AnInterfaceThatIsNothingButAnOperatorStillGetsItsNote) {
    // Nothing is left to mock, so there is no mock: the class is not found to be an interface.
    EXPECT_TRUE(plan("class I { public: virtual bool operator==(const I&) const = 0; };").mocks.empty());
}

TEST(MockPlannerLeftOut, ClassTemplatesGetANote) {
    const Planned p = plan("namespace a { template <typename T> class I { public: virtual T get() = 0; }; }");
    EXPECT_TRUE(p.mocks.empty());
    ASSERT_EQ(p.diagnostics.all().size(), 1u);
    EXPECT_EQ(p.diagnostics.all()[0].severity, core::Severity::Note);
    EXPECT_EQ(p.diagnostics.all()[0].message, "the interface 'a::I' is a class template; no mock is generated for it");
}

// ---- inheritance ----------------------------------------------------------------------------------------------------

TEST(MockPlannerBases, WhatABaseInTheHeaderLeftPureIsMockedToo) {
    const Planned p = plan(
        "class Base { public: virtual void a() = 0; virtual void b() = 0; };\n"
        "class Derived : public Base { public: virtual void c() = 0; };\n");
    EXPECT_EQ(methods(p, 0), (Lines{"void a()", "void b()"}));
    EXPECT_EQ(methods(p, 1), (Lines{"void c()", "void a()", "void b()"})) << "its own first, then the bases'";
}

TEST(MockPlannerBases, WhatTheDerivedClassImplementsIsNot) {
    const Planned p = plan(
        "class Base { public: virtual void a() = 0; virtual void b() = 0; virtual int c() const = 0; };\n"
        "class Derived : public Base { public: void a() override; int c() const override; virtual void d() = 0; };\n");
    EXPECT_EQ(methods(p, 1), (Lines{"void d()", "void b()"}));
}

TEST(MockPlannerBases, ADerivedClassThatImplementsEverythingIsNoInterface) {
    const Planned p = plan(
        "class Base { public: virtual void a() = 0; };\n"
        "class Derived : public Base { public: void a() override; };\n");
    EXPECT_EQ(names(p), Lines{"MockBase"});
}

TEST(MockPlannerBases, ThePureRedeclarationOfAFunctionIsListedOnce) {
    const Planned p = plan(
        "class Base { public: virtual void a() = 0; };\n"
        "class Derived : public Base { public: virtual void a() = 0; virtual void b() = 0; };\n");
    EXPECT_EQ(methods(p, 1), (Lines{"void a()", "void b()"}));
}

TEST(MockPlannerBases, SeveralBasesAndABaseOfABase) {
    const Planned p = plan(
        "class A { public: virtual void a() = 0; };\n"
        "class B : public A { public: virtual void b() = 0; };\n"
        "class C { public: virtual void c() = 0; };\n"
        "class D : public B, public C { public: virtual void d() = 0; };\n");
    EXPECT_EQ(methods(p, 3), (Lines{"void d()", "void b()", "void a()", "void c()"}));
}

TEST(MockPlannerBases, TheNestedTypesOfABaseAreNamedThroughTheBase) {
    const Planned p = plan(
        "class Base { public: struct Item { int v; }; virtual Item get() const = 0; };\n"
        "class Derived : public Base { public: virtual void other() = 0; };\n");
    EXPECT_EQ(methods(p, 1), (Lines{"void other()", "Base::Item get() const"}));
}

TEST(MockPlannerBases, ABaseFoundByItsQualifiedName) {
    const Planned p = plan(
        "namespace a { class Base { public: virtual void x() = 0; }; }\n"
        "namespace b { class Derived : public a::Base { public: virtual void y() = 0; }; }\n");
    EXPECT_EQ(methods(p, 1), (Lines{"void y()", "void x()"}));
}

TEST(MockPlannerBases, ABaseOutsideTheHeaderIsANoteOnTheMock) {
    const Planned p = plan("class Derived : public vendor::Base, public Other { public: virtual void y() = 0; };");
    ASSERT_EQ(p.mocks.size(), 1u);
    EXPECT_EQ(methods(p), Lines{"void y()"});
    EXPECT_EQ(p.mocks[0].notes,
              (Lines{"the base 'vendor::Base' is declared elsewhere or is a template: if it has pure virtual functions, the mock must declare them too",
                     "the base 'Other' is declared elsewhere or is a template: if it has pure virtual functions, the mock must declare them too"}));
}

TEST(MockPlannerBases, ACycleDoesNotHang) {
    const Planned p = plan(
        "class A : public B { public: virtual void a() = 0; };\n"
        "class B : public A { public: virtual void b() = 0; };\n");
    EXPECT_EQ(names(p), (Lines{"MockA", "MockB"}));
}

TEST(MockPlannerBases, ATemplateBaseIsNotGuessedAt) {
    // The template's functions depend on T, which the mock cannot say.
    const Planned p = plan(
        "template <typename T> class Base { public: virtual T get() = 0; };\n"
        "class Derived : public Base<int> { public: virtual void y() = 0; };\n");
    ASSERT_EQ(p.mocks.size(), 1u);
    EXPECT_EQ(p.mocks[0].name, "MockDerived");
    EXPECT_EQ(methods(p, 0), Lines{"void y()"});
    EXPECT_EQ(p.mocks[0].notes,
              Lines{"the base 'Base<int>' is declared elsewhere or is a template: if it has pure virtual functions, the mock must declare them too"});
}

// ---- determinism ----------------------------------------------------------------------------------------------------

TEST(MockPlannerDeterminism, TheSameHeaderTheSameMocks) {
    const std::string header =
        "class A { public: virtual void a() = 0; };\nclass B : public A { public: virtual void b() = 0; };\n";
    const Planned x = plan(header);
    const Planned y = plan(header);
    ASSERT_EQ(x.mocks.size(), y.mocks.size());
    for (std::size_t i = 0; i < x.mocks.size(); ++i) EXPECT_EQ(methods(x, i), methods(y, i));
}
