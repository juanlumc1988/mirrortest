// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Runs every case under tests/golden/ against the real command line.
// MT_UPDATE_GOLDEN=1 rewrites the expectations instead of checking them; see
// golden.h and tests/golden/README.md.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "golden.h"
#include "mirrortest/cli/run.h"
#include "mirrortest/core/config.h"
#include "mirrortest/frontend/clang_frontend.h"

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

namespace {

// The optional features a case lists in requires.txt (golden.h): "yaml", "libclang". A name nobody
// knows is a mistake in the case and fails it.
bool feature_available(const std::string& name, std::string& problem) {
    if (name == "yaml") return mt::core::yaml_supported();
    if (name == "libclang") return mt::frontend::libclang_supported();
    problem = "requires.txt names '" + name + "', which is not a feature of the build (yaml, libclang)";
    return false;
}

}  // namespace

// Skips (or fails) the running test for a case whose requires.txt names a feature this build lacks (or nobody knows).
#define MT_REQUIRE_THE_FEATURES_OF(test_case)                                              \
    {                                                                                      \
        std::ifstream requires_file((test_case).dir / "requires.txt");                     \
        for (std::string feature; std::getline(requires_file, feature);) {                 \
            while (!feature.empty() && (feature.back() == '\r' || feature.back() == ' ')) feature.pop_back(); \
            if (feature.empty()) continue;                                                 \
            std::string problem;                                                           \
            if (!feature_available(feature, problem)) {                                    \
                if (!problem.empty()) FAIL() << (test_case).name << ": " << problem;       \
                GTEST_SKIP() << "needs " << feature;                                       \
            }                                                                              \
        }                                                                                  \
    }

TEST_P(GoldenCase, Matches) {
    MT_REQUIRE_THE_FEATURES_OF(GetParam());
    const bool update = update_requested();
    const std::vector<std::string> problems = mt::golden::check_case(GetParam(), mt::cli::run, update);
    for (const std::string& problem : problems) {
        ADD_FAILURE() << GetParam().name << ": " << problem;
    }
    if (update && problems.empty()) {
        std::cout << "[  UPDATED ] " << GetParam().name << "\n";
    }
}

// The libclang frontend, held to the goldens of the other one (plan 8.5): what gen-tests writes from a header, and what
// gen-impl does with the headers it finds, must be the same bytes whichever reader read them. The cases it is not held to
// are the ones where libclang is meant to differ, and each says why.
namespace {

bool libclang_reads_it_differently(const std::string& name, std::string& why) {
    static const struct {
        const char* name;
        const char* why;
    } kDifferent[] = {
        {"gen-tests/ifdef_branches", "tree-sitter keeps both branches of an #if, libclang the one that builds"},
    };
    for (const auto& d : kDifferent) {
        if (name == d.name) {
            why = d.why;
            return true;
        }
    }
    return false;
}

std::vector<mt::golden::Case> cases_with_headers() {
    std::vector<mt::golden::Case> out;
    for (const mt::golden::Case& c : mt::golden::discover(MT_GOLDEN_DIR)) {
        if (c.name.rfind("gen-tests/", 0) == 0 || c.name.rfind("gen-impl/", 0) == 0) out.push_back(c);
    }
    return out;
}

class GoldenWithLibclang : public ::testing::TestWithParam<mt::golden::Case> {};

}  // namespace

TEST_P(GoldenWithLibclang, Matches) {
    if (!mt::frontend::libclang_supported()) GTEST_SKIP() << "built without libclang";
    std::string why;
    if (libclang_reads_it_differently(GetParam().name, why)) GTEST_SKIP() << why;
    if (std::ifstream((GetParam().dir / "requires.txt")).good()) GTEST_SKIP() << "it asks for the frontend itself, in its arguments";
    MT_REQUIRE_THE_FEATURES_OF(GetParam());
    // What libclang has to say about a header it cannot resolve (a type nobody declared, Qt's headers when they are not on
    // the include path) is its own: every other line of stderr, and everything written, must be the other reader's.
    const mt::golden::CliFn with_libclang = [](const std::vector<std::string>& args, const mt::cli::Env& env) {
        std::vector<std::string> more = args;
        more.push_back("--frontend");
        more.push_back("libclang");
        std::ostringstream err;
        mt::cli::Env quiet = env;
        quiet.err = &err;
        const int code = mt::cli::run(more, quiet);
        std::istringstream lines(err.str());
        for (std::string line; std::getline(lines, line);) {
            if (line.find("libclang: ") == std::string::npos && line.find("more libclang errors") == std::string::npos) *env.err << line << "\n";
        }
        return code;
    };
    for (const std::string& problem : mt::golden::check_case(GetParam(), with_libclang, false)) {
        ADD_FAILURE() << GetParam().name << " (libclang): " << problem;
    }
}

INSTANTIATE_TEST_SUITE_P(All, GoldenWithLibclang, ::testing::ValuesIn(cases_with_headers()), test_name);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GoldenWithLibclang);

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
