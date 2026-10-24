// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The markers that fence what mirrortest owns in a generated file
// (docs/design.md, section 11.1):
//
//   // <mt:begin acme::Foo>
//   ...
//   // <mt:end acme::Foo>
//
// The backends write them and the merge (`--overwrite=merge`) looks for them,
// so both take the exact text from here: if the spelling of a marker ever
// differed between the two, a re-run would no longer find its own regions.

#ifndef MT_CORE_REGIONS_H
#define MT_CORE_REGIONS_H

#include <string>
#include <vector>

namespace mt::core {

// "// <mt:begin LABEL>" and "// <mt:end LABEL>".
std::string region_begin(const std::string& label);
std::string region_end(const std::string& label);

// The labels in use: "includes", "functions" for the global namespace and
// "<ns>::functions" for a namespace's free functions, and the qualified name of
// a class. They have to be unique within a file, since the merge finds a region
// by its label.
std::string includes_label();
std::string functions_label(const std::vector<std::string>& ns);
std::string class_label(const std::vector<std::string>& ns, const std::string& name);

}  // namespace mt::core

#endif  // MT_CORE_REGIONS_H
