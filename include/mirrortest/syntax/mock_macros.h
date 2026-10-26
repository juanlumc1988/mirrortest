// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What gmock's method macros declare. A mock class says what its interface is
// in macros:
//
//   MOCK_METHOD(int, read, (), (override));
//   MOCK_METHOD(void, set, (double, const std::string&), (const, noexcept, override));
//   MOCK_METHOD1(write, void(int));            // the macros before gmock 1.10
//   MOCK_CONST_METHOD0(ok, bool());
//
// A parser that does not expand macros sees none of it, so the preprocessing
// replaces each by the member it declares ("int read() override"), and the code
// frontend reads the mock like any class with virtual members. That is also how
// gen-impl finds out what the interface a mock stands for must look like.
//
// Pure text in, text out: no parsing of C++, only of the macros' own argument
// lists, which have a regular shape.

#ifndef MT_SYNTAX_MOCK_MACROS_H
#define MT_SYNTAX_MOCK_MACROS_H

#include <string>
#include <string_view>

namespace mt::syntax {

// MOCK_METHOD, MOCK_METHODn, MOCK_CONST_METHODn, and the _T and _WITH_CALLTYPE
// forms of the last two (n from 0 to 20).
bool is_mock_method_macro(std::string_view word);

// The member `word(arguments)` declares, without the final ';':
//
//   MOCK_METHOD(const std::string&, name, (int index), (const, override))
//       -> "const std::string& name(int index) const override"
//   MOCK_METHOD1(set, void(int))  -> "void set(int) override"
//
// A return type or an argument in parentheses ("(std::map<int, int>)", gmock's
// way of protecting its commas) loses them. The old macros declare an override
// as well, though they do not say so: a mock method is there to override one.
// `arguments` is the text between the macro's parentheses. Empty if it is not
// what the macro takes.
std::string mock_method_declaration(std::string_view word, std::string_view arguments);

}  // namespace mt::syntax

#endif  // MT_SYNTAX_MOCK_MACROS_H
