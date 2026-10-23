// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Names: file stems, include guards, test case names, and matching a class to
// the header that should hold it. Pure string functions; no I/O.
//
// Everything here is ASCII and locale-independent on purpose. <cctype>
// follows the C locale in use, and a generator whose file names depend on
// LC_ALL is a generator whose goldens fail on someone else's machine. Bytes
// >= 0x80 (UTF-8 identifiers) are treated as lower-case letters: they are
// never split and never case-folded.

#ifndef MT_CORE_NAMING_H
#define MT_CORE_NAMING_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mt::core {

// [A-Za-z_][A-Za-z0-9_]*
bool is_identifier(std::string_view text);

// Identifiers joined by "::", at least `min_parts` of them. No leading "::".
bool is_qualified_name(std::string_view text, std::size_t min_parts);

// "HTTPServer" -> "http_server". Word boundaries:
//   lower or digit followed by Upper   getHTTP -> get_http, Vec3D -> vec3_d
//   Upper run followed by Upper+lower  HTTPServer -> http_server
//   any other character (_ - : < ...)  Foo_Bar -> foo_bar
// Digits stay with the word before them (Base64Encoder -> base64_encoder).
// Separators are collapsed and trimmed, so "_Foo__Bar_" -> "foo_bar".
// A name with nothing left (empty, or only separators) gives "unnamed".
std::string snake_case(std::string_view name);

// The file name without directories or its last extension:
// "acme/foo_bar.h" -> "foo_bar", "C:\\x\\Foo.hpp" -> "Foo", "a.b.h" -> "a.b".
std::string file_stem(std::string_view path);

// The key two names are compared by when pairing a class with a header:
// lower-case, without '_' and '-'. FooBar, foo_bar and foo-bar all give
// "foobar" (design 8.1).
std::string match_key(std::string_view name);

// Include guard for a header, from the path it is included by:
// "acme/foo.h" -> "ACME_FOO_H". Anything that is not a letter or digit
// becomes '_', runs collapse and the ends are trimmed (a leading or doubled
// underscore would be a reserved identifier), and a leading digit gets an
// "H_" prefix. Two paths that differ only in '_' versus '/' share a guard;
// that is the usual convention and the cost of readable guards.
std::string include_guard(std::string_view include_path);

// Replaces {stem} and {Class} in a naming pattern (core/config.h validates
// that only these appear). Everything else is copied.
std::string expand_pattern(std::string_view pattern, std::string_view stem,
                           std::string_view class_name);

// ---- test case names ------------------------------------------------------------

// The base of a test name for a method:
//   size              -> size
//   operator==        -> OperatorEq        operator[]   -> OperatorIndex
//   operator()        -> OperatorCall      operator bool -> ConversionToBool
//   operator const char* -> ConversionToConstCharPtr
// Methods that are not operators come back unchanged.
std::string test_base_name(std::string_view method_name);

// Lower-case words describing a parameter list, for telling overloads apart:
// {"int", "const std::string&", "Foo*"} -> "int_std_string_foo_ptr". Top-level
// const/volatile and references are dropped (the suffix names the types, not
// how they are passed), pointers become "ptr". Empty for no parameters.
std::string type_suffix(const std::vector<std::string>& param_types);

struct TestNameInput {
    std::string              base;          // from test_base_name
    std::vector<std::string> param_types;   // as spelled in the IR
};

// One valid, unique identifier per input, in the same order.
//   * A base used by several inputs is told apart by its types:
//     set(int), set(const std::string&) -> set_int, set_std_string.
//   * Anything still equal gets _2, _3, ... in input order (the first keeps
//     the plain name), also against names already taken by other inputs.
std::vector<std::string> unique_test_names(const std::vector<TestNameInput>& inputs);

}  // namespace mt::core

#endif  // MT_CORE_NAMING_H
