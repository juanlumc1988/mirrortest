// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Names that belong to somebody else (docs/design.md, sections 7.7 and 7.8).
//
// A test calls strlen(), declares a size_t, throws std::runtime_error: none of
// that is code to generate. The syntax cannot tell, so the tool carries the
// lists of what the language, the C library and the frameworks already
// provide. They are deliberately short and exact -- a name missing here costs a
// stub nobody wants, one too many costs a function never generated -- and the
// user extends them with `external_types` and `external_functions`.

#ifndef MT_FRONTEND_KNOWN_NAMES_H
#define MT_FRONTEND_KNOWN_NAMES_H

#include <string_view>

namespace mt::frontend {

// MY_EXPECT_OK, ASSERT_VALID, X: only uppercase letters, digits and
// underscores, at least one letter (R21). A call to one is a macro.
bool is_macro_like(std::string_view name);

// QString, QObject, QTest -- Qt's classes are Q followed by a capital.
bool is_qt_class(std::string_view name);

// qDebug, qMax, qFuzzyCompare -- Qt's global functions are q followed by a capital.
bool is_qt_function(std::string_view name);

// size_t, uint8_t, FILE, ptrdiff_t: the types of <cstddef>, <cstdint>, <cstdio>
// and friends, which are in the global namespace without any `using`; and Qt's
// fixed-size typedefs (qint64, quint8, qreal, uchar, uint).
bool is_standard_c_type(std::string_view name);

// string, vector, unique_ptr, runtime_error: the standard library's types, for
// when the file says `using namespace std;`.
bool is_std_type(std::string_view name);

// strlen, memcpy, abs, printf, assert, offsetof: the C library and the few
// keyword-like operators that parse as calls. Available without any `using`.
bool is_c_function(std::string_view name);

// move, swap, make_pair, to_string, sort: the standard library's functions,
// for when the file says `using namespace std;`.
bool is_std_function(std::string_view name);

// SetUp, TearDown, GetParam, RecordProperty: what a GoogleTest fixture
// inherits from ::testing::Test and its relatives.
bool is_framework_member(std::string_view name);

// The namespaces whose contents are never generated and never ask for a
// namespace to be placed in: std, testing, QTest, Qt and the like.
bool is_foreign_namespace(std::string_view name);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_KNOWN_NAMES_H
