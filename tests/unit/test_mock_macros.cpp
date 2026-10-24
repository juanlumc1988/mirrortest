// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>

#include "mirrortest/syntax/mock_macros.h"

using namespace mt::syntax;

// ---- which words are the macros ------------------------------------------------------

TEST(MockMacroName, TheNewMacro) {
    EXPECT_TRUE(is_mock_method_macro("MOCK_METHOD"));
}

TEST(MockMacroName, TheNumberedMacros) {
    for (const char* name : {"MOCK_METHOD0", "MOCK_METHOD1", "MOCK_METHOD10", "MOCK_METHOD20", "MOCK_CONST_METHOD0",
                             "MOCK_CONST_METHOD7", "MOCK_CONST_METHOD15"}) {
        EXPECT_TRUE(is_mock_method_macro(name)) << name;
    }
}

TEST(MockMacroName, TheTemplateAndCalltypeForms) {
    for (const char* name : {"MOCK_METHOD1_T", "MOCK_CONST_METHOD2_T", "MOCK_METHOD3_WITH_CALLTYPE", "MOCK_CONST_METHOD4_WITH_CALLTYPE",
                             "MOCK_METHOD5_T_WITH_CALLTYPE", "MOCK_CONST_METHOD6_T_WITH_CALLTYPE"}) {
        EXPECT_TRUE(is_mock_method_macro(name)) << name;
    }
}

TEST(MockMacroName, OtherWordsAreNot) {
    for (const char* name : {"", "MOCK", "MOCK_", "MOCK_METHODS", "MOCK_METHOD_", "MOCK_METHOD1X", "MOCK_METHOD123", "MOCK_CONST_METHOD",
                             "MOCK_CONST_METHOD_T", "MOCK_METHOD1_WITH", "MOCK_METHOD1_T_T", "MOCK_METHOD1_CALLTYPE", "mock_method",
                             "MOCK_METHODX", "EXPECT_CALL", "ON_CALL", "FAKE_MOCK_METHOD", "MOCK_CONST_METHODS3"}) {
        EXPECT_FALSE(is_mock_method_macro(name)) << name;
    }
}

TEST(MockMacroName, ADeclarationOfAWordThatIsNotAMacroIsEmpty) {
    EXPECT_EQ(mock_method_declaration("EXPECT_CALL", "int, read, ()"), "");
    EXPECT_EQ(mock_method_declaration("", ""), "");
}

// ---- MOCK_METHOD(Ret, Name, (Args), (Specs)) -----------------------------------------

TEST(MockMethodNew, WithoutSpecifiers) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, ()"), "int read()");
}

TEST(MockMethodNew, Arguments) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "void, set, (double, const std::string&)"), "void set(double, const std::string&)");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "void, set, (double value, int count)"), "void set(double value, int count)");
}

TEST(MockMethodNew, TheSpecifiersInTheOrderTheLanguageWantsThem) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (override)"), "int read() override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (const)"), "int read() const");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (noexcept)"), "int read() noexcept");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (final)"), "int read() final");
    // However the user wrote them, `const` comes first and `override` last.
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (override, noexcept, const)"), "int read() const noexcept override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (final, override, const, noexcept)"),
              "int read() const noexcept override final");
}

TEST(MockMethodNew, TheReferenceQualifier) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (const, ref(&), override)"), "int read() const & override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (ref(&&))"), "int read() &&");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (ref( & ))"), "int read() &");
}

TEST(MockMethodNew, TheCallingConventionIsLeftOut) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (override, Calltype(STDMETHODCALLTYPE))"), "int read() override");
}

TEST(MockMethodNew, ParenthesisedReturnTypeAndArguments) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "(std::map<int, int>), table, ()"), "std::map<int, int> table()");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "void, put, ((std::map<int, int>), int)"), "void put(std::map<int, int>, int)");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "(std::pair<int, int>), pair, ((std::pair<int, int>)), (const)"),
              "std::pair<int, int> pair(std::pair<int, int>) const");
}

TEST(MockMethodNew, TemplateArgumentsWithoutCommasPassThrough) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "std::vector<int>, items, (const std::vector<int>&), (const)"),
              "std::vector<int> items(const std::vector<int>&) const");
}

TEST(MockMethodNew, FunctionPointerArgument) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "void, on, (void (*)(int))"), "void on(void (*)(int))");
}

TEST(MockMethodNew, WhitespaceAndNewlinesAreNormalised) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "  int ,\n  read ,\n (  ) ,\n ( const ,\n override )  "), "int read() const override");
}

TEST(MockMethodNew, TheDeclarationIsOneLine) {
    // A macro is often written over several lines, and a type or a qualifier over two of them.
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "const\n    std::string&,\n  name,\n  (std::map<int,\n        int>\t&m,\r\n int),\n (const,\n  override)"),
              "const std::string& name(std::map<int, int> &m, int) const override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (ref(\n &\n))"), "int read() &");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "bool, operator\n==, (int)"), "bool operator ==(int)");
}

TEST(MockMethodNew, OperatorNames) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "bool, operator==, (const Sensor&), (const, override)"),
              "bool operator==(const Sensor&) const override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, operator(), (int)"), "int operator()(int)");
}

TEST(MockMethodNew, NotWhatTheMacroTakesIsEmpty) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", ""), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (), (const), extra"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, int"), "");         // no parentheses around the arguments
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (int"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (int) (int)"), "");  // the parentheses close before the end
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", ", read, ()"), "");              // no return type
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, , ()"), "");               // no name
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, 1read, ()"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, re ad, ()"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, a-b, ()"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (int, )"), "");      // an empty argument
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD", "int, read, (, int)"), "");
}

// ---- MOCK_METHODn(Name, Ret(Args)), MOCK_CONST_METHODn ------------------------------------

TEST(MockMethodOld, WithoutArguments) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD0", "read, int()"), "int read() override");
}

TEST(MockMethodOld, WithArguments) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, void(int)"), "void write(int) override");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD2", "set, void(double, const std::string&)"), "void set(double, const std::string&) override");
}

TEST(MockMethodOld, TheConstMacros) {
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD0", "ok, bool()"), "bool ok() const override");
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD1", "at, const std::string&(int)"), "const std::string& at(int) const override");
}

TEST(MockMethodOld, TheTemplateForms) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1_T", "write, void(T)"), "void write(T) override");
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD0_T", "get, T()"), "T get() const override");
}

TEST(MockMethodOld, TheCalltypeIsTheFirstArgumentAndIsLeftOut) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1_WITH_CALLTYPE", "STDMETHODCALLTYPE, write, void(int)"), "void write(int) override");
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD0_WITH_CALLTYPE", "STDMETHODCALLTYPE, ok, bool()"), "bool ok() const override");
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD1_T_WITH_CALLTYPE", "Cdecl, at, T(int)"), "T at(int) const override");
}

TEST(MockMethodOld, ACommaInsideTheArgumentsIsInsideParentheses) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD2", "put, void(std::map<int, int>, int)"), "void put(std::map<int, int>, int) override");
}

TEST(MockMethodOld, TheDeclarationIsOneLine) {
    EXPECT_EQ(mock_method_declaration("MOCK_CONST_METHOD2", "\n   at,\n   const\n std::string&(int,\n  std::map<int,\n int>)"),
              "const std::string& at(int, std::map<int, int>) const override");
}

TEST(MockMethodOld, AReturnTypeThatHasParenthesesItself) {
    // The parenthesis that closes the text opens the arguments, whatever comes before it.
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "handler, std::function<void(int)>(int)"), "std::function<void(int)> handler(int) override");
}

TEST(MockMethodOld, NotWhatTheMacroTakesIsEmpty) {
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", ""), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, void(int), extra"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, void"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, (int)"), "");           // no return type
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, void(int"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", ", void(int)"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "1write, void(int)"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1", "write, void(int, )"), "");
    EXPECT_EQ(mock_method_declaration("MOCK_METHOD1_WITH_CALLTYPE", "write, void(int)"), "");   // the calltype is missing
}
