// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/regions.h"

#include "mirrortest/ir/type.h"

namespace mt::core {

std::string region_begin(const std::string& label) { return "// <mt:begin " + label + ">"; }
std::string region_end(const std::string& label) { return "// <mt:end " + label + ">"; }

std::string includes_label() { return "includes"; }

std::string functions_label(const std::vector<std::string>& ns) { return ir::qualified_name(ns, "functions"); }

std::string class_label(const std::vector<std::string>& ns, const std::string& name) { return ir::qualified_name(ns, name); }

}  // namespace mt::core
