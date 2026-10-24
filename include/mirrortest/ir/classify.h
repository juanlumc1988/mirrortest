// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What the type names in a signature stand for: which header provides them,
// and whether a value of the type can be produced from nothing. The backends
// need both. A header that declares `std::vector<Item> items()` has to include
// <vector> and whatever declares Item; a stub that is to `return {};` has to
// know the braces are valid.
//
// Everything here is a table or a rule over spellings. None of it parses C++:
// a spelling the rules do not recognise is "unknown", and unknown means the
// conservative answer (no include, not default-constructible) -- a missing
// include is a compile error the user sees at once, a wrong one is a header
// they did not ask for.

#ifndef MT_IR_CLASSIFY_H
#define MT_IR_CLASSIFY_H

#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/ir/model.h"

namespace mt::ir {

// The header that provides a name of the standard library, the C library
// types or Qt, spelled as it goes between the brackets of an #include:
//
//   std::string -> "string"      std::uint8_t, uint8_t -> "cstdint"
//   std::size_t, size_t -> "cstddef"   std::chrono::seconds -> "chrono"
//   QString -> "QString"         qint64 -> "QtGlobal"
//
// Empty when the name needs none or is not known. The standard library is
// recognised only with its `std::` prefix (a test with `using namespace std`
// has it put back before it reaches a backend); the C types in either form.
std::string system_include_for(std::string_view qualified_name);

// The names a type spelling mentions, as written, in order of appearance and
// without repeats: the qualified identifiers, not the keywords, builtin types or
// numbers.
//
//   "std::map<std::string, Foo::Item>" -> {"std::map", "std::string", "Foo::Item"}
//   "const unsigned int*"              -> {}
//   "std::array<Item, 3u>"             -> {"std::array", "Item"}
std::vector<std::string> mentioned_names(std::string_view spelling);

// Can `return {};` produce a value of this type, and is the result a sensible
// "empty" one? True for fundamental types and their standard typedefs, enums,
// pointers, the standard containers, strings and smart pointers, Qt's value
// classes, `std::pair`/`tuple`/`array` of such types, the names in `user_types`
// (the configuration's default_constructible_types) and the classes of `models`
// that a default constructor can build without `explicit`. References are never
// returnable this way, and neither is a type nobody has heard of.
bool is_default_returnable(const Type& type, const std::vector<const Model*>& models,
                           const std::vector<std::string>& user_types);

}  // namespace mt::ir

#endif  // MT_IR_CLASSIFY_H
