// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The pinned header-only dependencies compile under our warning flags and
// behave as the emitters will assume. Cheap, and it turns a bad dependency
// bump into one obvious failure instead of a hundred confusing ones.

#include <gtest/gtest.h>

#include <string>

#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

TEST(Dependencies, InjaRendersLoopsWithoutStrayWhitespace) {
    inja::Environment env;
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);

    const nlohmann::json data = {
        {"name", "Foo"},
        {"methods", {"size", "empty"}},
    };
    const std::string out = env.render(
        "class {{ name }} {\n"
        "{% for m in methods %}\n"
        "    void {{ m }}();\n"
        "{% endfor %}\n"
        "};\n",
        data);

    EXPECT_EQ(out,
              "class Foo {\n"
              "    void size();\n"
              "    void empty();\n"
              "};\n");
}

TEST(Dependencies, InjaReportsUndefinedVariables) {
    // A typo in a template must fail loudly, not render an empty string.
    inja::Environment env;
    EXPECT_THROW(env.render("{{ missing }}", nlohmann::json::object()), inja::RenderError);
}

TEST(Dependencies, JsonPreservesKeyOrderWhenAsked) {
    // Configuration and view models are emitted in a deterministic order.
    nlohmann::ordered_json j;
    j["zeta"] = 1;
    j["alpha"] = 2;
    EXPECT_EQ(j.dump(), R"({"zeta":1,"alpha":2})");
}
