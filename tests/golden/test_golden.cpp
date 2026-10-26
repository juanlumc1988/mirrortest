// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Runs every case under tests/golden/ against the real command line.
// MT_UPDATE_GOLDEN=1 rewrites the expectations instead of checking them; see
// golden.h and tests/golden/README.md.

#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "golden.h"
#include "mirrortest/cli/run.h"

namespace {

bool update_requested() {
    const char* value = std::getenv("MT_UPDATE_GOLDEN");
    return value != nullptr && std::string(value) == "1";
}

// "gen-impl/basic_class" -> "gen_impl_basic_class": test names are identifiers.
std::string test_name(const ::testing::TestParamInfo<mt::golden::Case>& info) {
    std::string name = info.param.name;
    for (char& c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) c = '_';
    }
    return name;
}

class GoldenCase : public ::testing::TestWithParam<mt::golden::Case> {};

}  // namespace

TEST_P(GoldenCase, Matches) {
    const bool update = update_requested();
    const std::vector<std::string> problems = mt::golden::check_case(GetParam(), mt::cli::run, update);
    for (const std::string& problem : problems) {
        ADD_FAILURE() << GetParam().name << ": " << problem;
    }
    if (update && problems.empty()) {
        std::cout << "[  UPDATED ] " << GetParam().name << "\n";
    }
}

INSTANTIATE_TEST_SUITE_P(All, GoldenCase,
                         ::testing::ValuesIn(mt::golden::discover(MT_GOLDEN_DIR)), test_name);

// Cases are optional until there are some; the suite must not be reported as
// uninstantiated meanwhile.
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GoldenCase);

TEST(GoldenSuite, EveryCaseNameIsUniqueAfterSanitising) {
    std::set<std::string> seen;
    for (const mt::golden::Case& c : mt::golden::discover(MT_GOLDEN_DIR)) {
        std::string name = c.name;
        for (char& ch : name) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
            if (!ok) ch = '_';
        }
        EXPECT_TRUE(seen.insert(name).second) << "two cases collide as test names: " << c.name;
    }
}
