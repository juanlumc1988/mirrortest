// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Canonical spelling of types and qualified names.
//
// Spellings are compared -- "does the header already declare this?" -- and
// emitted, so two spellings of the same type must normalise to one string,
// and that string must be what a person would write. normalize_spelling()
// only touches whitespace; it never reorders tokens, so "T const&" stays
// east-const if that is how the user wrote it.
//
//   "const  std::string &"           -> "const std::string&"
//   "std::map< int,std::string >"    -> "std::map<int, std::string>"
//   "std::vector<std::vector<int> >" -> "std::vector<std::vector<int>>"
//   "int * const"                    -> "int* const"
//   "unsigned   long"                -> "unsigned long"

#ifndef MT_IR_TYPE_H
#define MT_IR_TYPE_H

#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/ir/model.h"

namespace mt::ir {

// Whitespace-canonical form of a type spelling. Pure, total and idempotent:
// normalize_spelling(normalize_spelling(s)) == normalize_spelling(s).
std::string normalize_spelling(std::string_view spelling);

// Builds a Type from its parts with a canonical spelling:
//   make_type("std::string", true, RefKind::LValue)  -> "const std::string&"
//   make_type("char", true, RefKind::None, 1)        -> "const char*"
// `base` is normalised first. origin/reason/where are left at their defaults
// for the caller to fill in.
Type make_type(std::string_view base,
               bool is_const = false,
               RefKind ref = RefKind::None,
               unsigned pointer_depth = 0);

// A Type from a spelling written by a person -- in an annotation, in the
// configuration -- rather than read off a declaration: the spelling is kept
// (normalised, exactly what was written), and the decomposed fields are worked
// out from its end.
//
//   "const Item*"          -> base "Item", is_const, pointer_depth 1
//   "Item const&"          -> base "Item", is_const, LValue
//   "std::vector<int>"     -> base "std::vector<int>"
//   "char* const"          -> base "char", pointer_depth 1 (a const pointer is not represented)
//   "void (*)(int)", "int[3]" -> base is the whole spelling: nothing finer is claimed
//
// origin/reason/where are left for the caller.
Type type_from_spelling(std::string_view spelling);

// "acme::core::Widget" from {"acme", "core"} and "Widget"; just the name
// when there is no namespace.
std::string qualified_name(const std::vector<std::string>& ns, std::string_view name);

}  // namespace mt::ir

#endif  // MT_IR_TYPE_H
