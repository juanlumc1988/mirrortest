// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Development tool (-DMT_BUILD_TOOLS=ON): reads one header with the chosen frontend and writes the model it makes
// to stdout, in the form the parity tests compare (tests/unit/model_dump.h), and the diagnostics to stderr. It
// is what tools/corpus.sh uses to say whether the two frontends read a header the same way.
//
//   mt_dump_model [--frontend tree-sitter|libclang] [--std c++17] [-I DIR]... [--clang-arg ARG]... [--config FILE]
//                 [--no-positions] [--tree] FILE
//
// --tree writes the syntax tree tree-sitter makes of the file (after the preprocessing that blanks the Qt macros and the
// configured ones) instead of a model: what to look at when the syntax-tree reader reads something wrong.
//
// --config reads the `ignore_macros` (and the rest) of a configuration file, as the commands do.

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/source.h"
#include "mirrortest/frontend/clang_frontend.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/syntax/preprocess.h"
#include "mirrortest/syntax/tree.h"
#include "model_dump.h"

namespace {

int usage() {
    std::cerr << "usage: mt_dump_model [--frontend tree-sitter|libclang] [--std c++17] [-I DIR]... [--clang-arg ARG]... "
                 "[--config FILE] [--no-positions] [--tree] FILE\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::string frontend_name = "tree-sitter";
    std::string standard = "c++17";
    std::vector<std::string> include_dirs;
    std::vector<std::string> clang_args;
    bool positions = true;
    bool tree = false;
    std::string file;
    std::string config_file;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "--frontend" && has_value) frontend_name = argv[++i];
        else if (a == "--std" && has_value) standard = argv[++i];
        else if (a == "-I" && has_value) include_dirs.push_back(argv[++i]);
        else if (a == "--clang-arg" && has_value) clang_args.push_back(argv[++i]);
        else if (a == "--config" && has_value) config_file = argv[++i];
        else if (a == "--no-positions") positions = false;
        else if (a == "--tree") tree = true;
        else if (!a.empty() && a[0] == '-') return usage();
        else if (file.empty()) file = a;
        else return usage();
    }
    if (file.empty() || (frontend_name != "tree-sitter" && frontend_name != "libclang")) return usage();

    mt::core::Diagnostics diagnostics;
    mt::core::Config config;
    if (!config_file.empty() && !mt::core::load_config_file(config, config_file, diagnostics)) {
        diagnostics.format(std::cerr);
        return 2;
    }
    mt::syntax::MacroTable macros;
    macros.ignore = config.ignore_macros;
    const auto source = mt::core::read_source(file, diagnostics);
    if (!source) {
        diagnostics.format(std::cerr);
        return 3;
    }
    const std::string include_path = fs::path(file).filename().string();

    if (tree) {
        const mt::syntax::Preprocessed pre = mt::syntax::preprocess(source->text, macros);
        std::cout << mt::syntax::parse_cpp(pre.text).root().sexp() << "\n";
        return 0;
    }

    mt::ir::Model model;
    if (frontend_name == "tree-sitter") {
        model = mt::frontend::read_header(*source, include_path, macros, diagnostics);
    } else {
        mt::frontend::ClangOptions options;
        options.arguments.push_back("-std=" + standard);
        for (const std::string& dir : include_dirs) options.arguments.push_back("-I" + fs::absolute(dir).lexically_normal().string());
        options.arguments.insert(options.arguments.end(), clang_args.begin(), clang_args.end());
        options.absolute_path = fs::absolute(file).lexically_normal().string();
        model = mt::frontend::read_header_clang(*source, include_path, macros, options, diagnostics);
    }
    diagnostics.format(std::cerr);
    std::cout << mt::test::dump(model, positions);
    return 0;
}
