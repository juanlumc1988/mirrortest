// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Command-line entry point. Only argument handling lives here; everything
// the tool does is in mt_core so the tests can drive it in-process.
//
// Exit codes (docs/design.md, "CLI"):
//   0  success, including "nothing to do"
//   1  the input was read but could not be processed (error diagnostics)
//   2  usage error
//   3  I/O error
//   4  --check: generated output differs from what is on disk

#include <iostream>
#include <string>

#include <CLI/CLI.hpp>

namespace {

constexpr int kExitUsage = 2;

int not_implemented(const std::string& command) {
    std::cerr << "mirrortest: '" << command << "' is not implemented yet (see docs/plan.md)\n";
    return kExitUsage;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"mirrortest: derive C++ stubs from GoogleTest/QTest tests, and test skeletons "
                 "from C++ headers.",
                 "mirrortest"};
    app.set_version_flag("--version", std::string("mirrortest ") + MT_VERSION);
    app.require_subcommand(1);

    CLI::App* gen_impl = app.add_subcommand("gen-impl", "tests -> headers and failing stubs (TDD red phase)");
    CLI::App* gen_tests = app.add_subcommand("gen-tests", "headers -> test skeletons");

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForAllHelp& e) {
        return app.exit(e);
    } catch (const CLI::CallForVersion& e) {
        return app.exit(e);
    } catch (const CLI::ParseError& e) {
        app.exit(e);
        return kExitUsage;
    }

    if (gen_impl->parsed()) {
        return not_implemented("gen-impl");
    }
    if (gen_tests->parsed()) {
        return not_implemented("gen-tests");
    }
    return kExitUsage;
}
