// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Command-line entry point. Arguments, options, exit codes and every
// behaviour live in mt_core (mirrortest/cli/run.h) so the tests can drive
// them in-process; this only supplies the real process environment.

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mirrortest/cli/run.h"

int main(int argc, char** argv) {
    mt::cli::Env env;
    env.cwd = std::filesystem::current_path();
    env.out = &std::cout;
    env.err = &std::cerr;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return mt::cli::run(args, env);
}
