// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The tables themselves are checked for order and duplicates by the compiler
// (static_assert in known_names.cpp); these tests check what they say.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/frontend/known_names.h"

using namespace mt::frontend;

namespace {

TEST(KnownNames, MacroLikeMeansOnlyUppercaseDigitsAndUnderscores) {
    for (const char* name : {"FOO", "MY_EXPECT_OK", "X", "A1", "ASSERT_VALID", "_FOO", "CRC16"}) {
        EXPECT_TRUE(is_macro_like(name)) << name;
    }
    for (const char* name : {"", "Foo", "foo", "_", "123", "FOo", "fOO", "my_macro", "_1"}) {
        EXPECT_FALSE(is_macro_like(name)) << name;
    }
}

TEST(KnownNames, QtClassesAndFunctionsByTheirCapital) {
    for (const char* name : {"QString", "QObject", "QTest", "QVector"}) EXPECT_TRUE(is_qt_class(name)) << name;
    for (const char* name : {"Q", "Qt", "Quick", "qString", "Foo", ""}) EXPECT_FALSE(is_qt_class(name)) << name;
    for (const char* name : {"qDebug", "qMax", "qFuzzyCompare", "qRound"}) EXPECT_TRUE(is_qt_function(name)) << name;
    for (const char* name : {"q", "qsort", "quit", "QDebug", "qint64"}) EXPECT_FALSE(is_qt_function(name)) << name;
}

TEST(KnownNames, StandardCTypesAndQtTypedefs) {
    for (const char* name : {"size_t", "ssize_t", "ptrdiff_t", "uint8_t", "int64_t", "uint_fast16_t", "intptr_t",
                             "uintmax_t", "FILE", "time_t", "va_list", "wchar_t", "nullptr_t", "max_align_t",
                             "qint64", "quint8", "qreal", "qsizetype", "uchar", "uint", "ulong", "ushort"}) {
        EXPECT_TRUE(is_standard_c_type(name)) << name;
    }
    for (const char* name : {"Size_t", "string", "uint128_t", "int", "Foo", ""}) EXPECT_FALSE(is_standard_c_type(name)) << name;
}

TEST(KnownNames, StdTypes) {
    for (const char* name : {"string", "vector", "unique_ptr", "shared_ptr", "map", "runtime_error", "ostringstream",
                             "function", "optional", "pair", "wstring", "unordered_multiset"}) {
        EXPECT_TRUE(is_std_type(name)) << name;
    }
    for (const char* name : {"String", "Vector", "Foo", "size_t", ""}) EXPECT_FALSE(is_std_type(name)) << name;
}

TEST(KnownNames, TestingTypes) {
    for (const char* name : {"Test", "TestWithParam", "WithParamInterface", "Environment", "NiceMock", "NaggyMock", "StrictMock",
                             "Mock", "Sequence", "InSequence", "Expectation", "Matcher", "Action", "Cardinality", "Types"}) {
        EXPECT_TRUE(is_testing_type(name)) << name;
    }
    // The functions (Return, _, AtLeast) are another list; a name that merely starts alike is not here.
    for (const char* name : {"TestFixture", "Mocks", "nice_mock", "Return", "string", "Foo", ""}) {
        EXPECT_FALSE(is_testing_type(name)) << name;
    }
}

TEST(KnownNames, TheNamesOfTheHookHeader) {
    for (const char* name : {"mt_not_implemented", "mt_set_not_implemented_handler", "mt_current_not_implemented_handler",
                             "mt_default_not_implemented_handler"}) {
        EXPECT_TRUE(is_hook_function(name)) << name;
        EXPECT_FALSE(is_hook_type(name)) << name;
    }
    EXPECT_TRUE(is_hook_type("mt_not_implemented_handler"));
    EXPECT_FALSE(is_hook_function("mt_not_implemented_handler"));
    for (const char* name : {"not_implemented", "mt_not_implemented_", "mt_other", "Mt_not_implemented", ""}) {
        EXPECT_FALSE(is_hook_function(name)) << name;
        EXPECT_FALSE(is_hook_type(name)) << name;
    }
}

TEST(KnownNames, CFunctionsAndTheKeywordLikeOperators) {
    for (const char* name : {"strlen", "memcpy", "memset", "abs", "printf", "snprintf", "malloc", "free", "sqrt",
                             "pow", "floor", "assert", "offsetof", "va_start", "alignof", "static_assert",
                             "toupper", "qsort", "strtoull", "vsnprintf", "abort"}) {
        EXPECT_TRUE(is_c_function(name)) << name;
    }
    for (const char* name : {"compute", "add", "read", "write", "open", "move", "swap", "Strlen", ""}) {
        EXPECT_FALSE(is_c_function(name)) << name;
    }
}

TEST(KnownNames, StdFunctionsNeedUsingNamespaceStdAndAreSeparateFromTheCLibrary) {
    for (const char* name : {"move", "swap", "make_pair", "to_string", "sort", "min", "max", "forward", "stoi", "begin"}) {
        EXPECT_TRUE(is_std_function(name)) << name;
    }
    for (const char* name : {"strlen", "compute", "add", ""}) EXPECT_FALSE(is_std_function(name)) << name;
}

TEST(KnownNames, FrameworkMembers) {
    for (const char* name : {"SetUp", "TearDown", "SetUpTestSuite", "TearDownTestCase", "GetParam", "RecordProperty",
                             "HasFailure", "TestBody"}) {
        EXPECT_TRUE(is_framework_member(name)) << name;
    }
    for (const char* name : {"setUp", "Setup", "Reset", ""}) EXPECT_FALSE(is_framework_member(name)) << name;
}

TEST(KnownNames, ForeignNamespaces) {
    for (const char* name : {"std", "testing", "QTest", "Qt", "__gnu_cxx"}) EXPECT_TRUE(is_foreign_namespace(name)) << name;
    for (const char* name : {"acme", "Std", "testing2", "internal", ""}) EXPECT_FALSE(is_foreign_namespace(name)) << name;
}

}  // namespace
