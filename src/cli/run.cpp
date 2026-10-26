// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/cli/run.h"

#include <ostream>
#include <utility>

#include <CLI/CLI.hpp>

#include "mirrortest/core/compile_commands.h"
#include "mirrortest/core/source.h"

#ifndef MT_VERSION
#define MT_VERSION "0.0.0-unknown"
#endif

namespace mt::cli {

namespace {

constexpr int kExitUsage = 2;

// What CLI11 fills in. Kept apart from Options so that "was this option
// given?" -- which decides whether it overrides the configuration file -- can
// be asked of the option itself, not guessed from its value.
struct Raw {
    std::vector<std::string> files;
    std::vector<std::string> tests;     // --tests (gen-impl)
    std::vector<std::string> headers;   // --header (gen-tests)
    std::vector<std::string> include_dirs;

    std::string out, out_include, out_src;
    std::string framework, include_as, qt_main = "guiless";
    std::string cxx_std, overwrite = "never", config_file, templates_dir, compile_commands;
    std::string stub_policy, placeholder, auto_policy;
    int         qt = 0;

    bool no_exceptions = false, no_rtti = false;
    bool dry_run = false, check = false, strict = false, quiet = false, verbose = false;
};

// The options the two commands share.
struct CommonOptions {
    CLI::Option* cxx_std = nullptr;
    CLI::Option* config_file = nullptr;
    CLI::Option* templates_dir = nullptr;
};

// An option that may be repeated, each occurrence taking exactly one value.
//
// A CLI11 container option is greedy: `-I inc test.cpp` would hand test.cpp to
// -I as a second directory and leave the command without inputs. Three
// settings together fix it, and none of them is enough alone (checked against
// this CLI11 version): expected(1) says one value per occurrence,
// allow_extra_args(false) stops it taking more, and TakeAll keeps the values
// of every occurrence instead of throwing on the second.
CLI::Option* repeatable(CLI::Option* option) {
    return option->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
}

CLI::Validator one_of(const std::vector<std::string>& choices) {
    return CLI::IsMember(choices);
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& i : items) {
        if (!out.empty()) out += '|';
        out += i;
    }
    return out;
}

CommonOptions add_common(CLI::App& cmd, Raw& raw) {
    CommonOptions given;
    cmd.add_option("--out", raw.out, "Output directory")->type_name("DIR");
    repeatable(cmd.add_option("-I", raw.include_dirs, "Include directory searched for headers (repeatable)")
                   ->type_name("DIR"));
    given.cxx_std = cmd.add_option("--std", raw.cxx_std,
                                   "C++ standard of the code to generate (default " +
                                       std::string(core::to_string(core::Config().cxx_std)) + ")")
                        ->type_name(join(core::std_names()))
                        ->check(one_of(core::std_names()));
    cmd.add_option("--overwrite", raw.overwrite,
                   "What to do with a file that exists and differs (default never)")
        ->type_name("never|merge|force")
        ->check(one_of({"never", "merge", "force"}));
    cmd.add_flag("--dry-run", raw.dry_run, "Print what would change as a diff; write nothing");
    cmd.add_flag("--check", raw.check, "Write nothing; exit 4 if generating would change a file");
    given.config_file = cmd.add_option("--config", raw.config_file, "Configuration file (JSON)")
                            ->type_name("FILE");
    given.templates_dir = cmd.add_option("--templates", raw.templates_dir,
                                         "Directory with templates that replace the built-in ones")
                              ->type_name("DIR");
    cmd.add_option("--compile-commands", raw.compile_commands,
                   "compile_commands.json; its -I, -isystem and -iquote directories are added after the -I given")
        ->type_name("FILE");
    cmd.add_flag("--strict", raw.strict, "Treat warnings as errors");
    CLI::Option* quiet = cmd.add_flag("--quiet", raw.quiet, "Print only errors and warnings");
    CLI::Option* verbose = cmd.add_flag("--verbose", raw.verbose, "Print more about what is being done");
    quiet->excludes(verbose);
    return given;
}

int usage_error(const Env& env, const std::string& command, const std::string& message) {
    *env.err << "mirrortest " << command << ": error: " << message << "\n"
             << "Try 'mirrortest " << command << " --help'.\n";
    return kExitUsage;
}

void print_diagnostics(const Env& env, const core::Diagnostics& diagnostics, bool quiet) {
    for (const core::Diagnostic& d : diagnostics.sorted()) {
        if (quiet && d.severity == core::Severity::Note) {
            continue;
        }
        *env.err << core::format_diagnostic(d);
    }
}

}  // namespace

std::string version() { return MT_VERSION; }

std::string Options::header_dir() const {
    return out_include ? *out_include : out.value_or("");
}

std::string Options::source_dir() const {
    return out_src ? *out_src : out.value_or("");
}

std::filesystem::path resolve(const Env& env, const std::string& path) {
    const std::filesystem::path p(path);
    return p.is_absolute() || env.cwd.empty() ? p : env.cwd / p;
}

int exit_code_for(const core::Diagnostics& diagnostics) {
    bool any_error = false;
    bool only_io = true;
    for (const core::Diagnostic& d : diagnostics.all()) {
        if (d.severity == core::Severity::Error) {
            any_error = true;
            if (d.code != core::code::io) {
                only_io = false;
            }
        }
    }
    if (!any_error) {
        return 0;
    }
    return only_io ? 3 : 1;
}

int finish_run(const Options& options, const Env& env, core::Diagnostics& diagnostics,
               const core::WriteReport* report) {
    if (options.strict) {
        diagnostics.promote_warnings();
    }
    print_diagnostics(env, diagnostics, options.quiet);

    const int failure = exit_code_for(diagnostics);
    if (failure != 0) {
        return failure;
    }
    if (report == nullptr) {
        return 0;
    }

    if (options.dry_run) {
        for (const core::FileResult& f : report->files) {
            *env.out << f.diff;
        }
    }
    if (!options.quiet && !options.check) {
        *env.err << core::format_summary(*report, options.dry_run);
    }
    return options.check && report->has_changes() ? 4 : 0;
}

std::optional<Options> parse_options(const std::vector<std::string>& args, const Env& env,
                                     int& exit_code) {
    Raw raw;
    CLI::App app{"mirrortest: derive C++ stubs from GoogleTest/QTest tests, and test skeletons "
                 "from C++ headers.",
                 "mirrortest"};
    app.set_version_flag("--version", "mirrortest " + version());
    app.require_subcommand(1);

    // ---- gen-impl -------------------------------------------------------------
    CLI::App* impl = app.add_subcommand("gen-impl", "Tests -> headers and failing stubs (the TDD red phase)");
    impl->add_option("files", raw.files, "Test files (.cpp)")->type_name("FILE");
    repeatable(impl->add_option("--tests", raw.tests, "A test file (same as a positional one)")
                   ->type_name("FILE"));
    const CommonOptions impl_common = add_common(*impl, raw);
    impl->add_option("--out-include", raw.out_include, "Where headers go (default: --out)")->type_name("DIR");
    impl->add_option("--out-src", raw.out_src, "Where sources go (default: --out)")->type_name("DIR");
    impl->add_option("--framework", raw.framework, "Test framework (default auto: detect)")
        ->type_name("auto|gtest|qtest")
        ->check(one_of({"auto", "gtest", "qtest"}));
    CLI::Option* impl_stub = impl->add_option("--stub-policy", raw.stub_policy,
                                              "What a stub does (default throw)")
                                 ->type_name(join(core::stub_policy_names()))
                                 ->check(one_of(core::stub_policy_names()));
    CLI::Option* impl_no_exc = impl->add_flag("--no-exceptions", raw.no_exceptions,
                                              "The target has no exceptions: stubs abort instead of throwing");
    CLI::Option* impl_no_rtti = impl->add_flag("--no-rtti", raw.no_rtti,
                                               "The target has no RTTI: never emit typeid/dynamic_cast");
    CLI::Option* impl_auto = impl->add_option("--auto-policy", raw.auto_policy,
                                              "What to do with an 'auto' holding a result (default error)")
                                 ->type_name(join(core::auto_policy_names()))
                                 ->check(one_of(core::auto_policy_names()));

    // ---- gen-tests -------------------------------------------------------------
    CLI::App* tests = app.add_subcommand("gen-tests", "Headers -> test skeletons");
    tests->add_option("files", raw.files, "Headers (.h/.hpp)")->type_name("FILE");
    repeatable(tests->add_option("--header", raw.headers, "A header (same as a positional one)")
                   ->type_name("FILE"));
    const CommonOptions tests_common = add_common(*tests, raw);
    tests->add_option("--framework", raw.framework, "Test framework (default gtest)")
        ->type_name("gtest|qtest")
        ->check(one_of({"gtest", "qtest"}));
    CLI::Option* tests_placeholder = tests->add_option("--placeholder", raw.placeholder,
                                                       "What an unwritten test does (default fail)")
                                          ->type_name(join(core::placeholder_names()))
                                          ->check(one_of(core::placeholder_names()));
    tests->add_option("--include-as", raw.include_as,
                      "The #include to emit for the header under test (one header only)")
        ->type_name("PATH");
    CLI::Option* tests_qt = tests->add_option("--qt", raw.qt, "Qt major version of generated QTest code (default 6)")
                                ->type_name("5|6")
                                ->check(CLI::IsMember(std::vector<int>{5, 6}));
    tests->add_option("--qt-main", raw.qt_main, "QTest entry point (default guiless)")
        ->type_name("appless|guiless|gui")
        ->check(one_of({"appless", "guiless", "gui"}));

    // ---- parse ------------------------------------------------------------------
    try {
        std::vector<std::string> reversed(args.rbegin(), args.rend());   // CLI11 takes them last-first
        app.parse(reversed);
    } catch (const CLI::ParseError& e) {
        const int code = app.exit(e, *env.out, *env.err);
        exit_code = code == 0 ? 0 : kExitUsage;
        return std::nullopt;
    }

    Options options;
    const bool is_impl = impl->parsed();
    const std::string command = is_impl ? "gen-impl" : "gen-tests";
    options.command = is_impl ? Command::GenImpl : Command::GenTests;
    const CommonOptions& common = is_impl ? impl_common : tests_common;

    options.inputs = raw.files;
    for (const std::string& f : is_impl ? raw.tests : raw.headers) {
        options.inputs.push_back(f);
    }
    if (options.inputs.empty()) {
        exit_code = usage_error(env, command,
                                std::string("no ") + (is_impl ? "test files" : "headers") + " given");
        return std::nullopt;
    }

    if (!raw.out.empty()) options.out = raw.out;
    if (!raw.out_include.empty()) options.out_include = raw.out_include;
    if (!raw.out_src.empty()) options.out_src = raw.out_src;
    if (is_impl) {
        if (options.header_dir().empty() || options.source_dir().empty()) {
            exit_code = usage_error(env, command,
                                    "say where to write: --out DIR, or both --out-include and --out-src");
            return std::nullopt;
        }
    } else if (!options.out) {
        exit_code = usage_error(env, command, "say where to write: --out DIR");
        return std::nullopt;
    }

    if (!raw.include_as.empty()) {
        if (options.inputs.size() != 1) {
            exit_code = usage_error(env, command, "--include-as names one header, but several were given");
            return std::nullopt;
        }
        options.include_as = raw.include_as;
    }

    if (is_impl) {
        if (raw.framework == "gtest") options.framework = ir::Framework::GTest;
        if (raw.framework == "qtest") options.framework = ir::Framework::QTest;
    } else {
        options.framework = raw.framework == "qtest" ? ir::Framework::QTest : ir::Framework::GTest;
    }
    options.qt_main = raw.qt_main == "appless" ? QtMain::Appless
                      : raw.qt_main == "gui"   ? QtMain::Gui
                                               : QtMain::GuiLess;

    options.include_dirs = raw.include_dirs;
    options.overwrite = *core::parse_overwrite_mode(raw.overwrite);
    options.dry_run = raw.dry_run;
    options.check = raw.check;
    options.strict = raw.strict;
    options.quiet = raw.quiet;
    options.verbose = raw.verbose;
    if (!raw.config_file.empty()) options.config_file = raw.config_file;
    if (!raw.templates_dir.empty()) options.templates_dir = raw.templates_dir;
    if (!raw.compile_commands.empty()) options.compile_commands = raw.compile_commands;

    // ---- configuration: defaults < file < command line --------------------------
    if (options.config_file) {
        core::Diagnostics diagnostics;
        if (!core::load_config_file(options.config, *options.config_file, diagnostics, env.cwd)) {
            print_diagnostics(env, diagnostics, options.quiet);
            exit_code = exit_code_for(diagnostics);
            return std::nullopt;
        }
    }
    if (common.cxx_std->count() > 0) options.config.cxx_std = *core::parse_std(raw.cxx_std);
    if (is_impl) {
        if (impl_stub->count() > 0) options.config.stub_policy = *core::parse_stub_policy(raw.stub_policy);
        if (impl_no_exc->count() > 0) options.config.no_exceptions = true;
        if (impl_no_rtti->count() > 0) options.config.no_rtti = true;
        if (impl_auto->count() > 0) options.config.auto_policy = *core::parse_auto_policy(raw.auto_policy);
    } else {
        if (tests_placeholder->count() > 0) {
            options.config.placeholder = *core::parse_placeholder(raw.placeholder);
        }
        if (tests_qt->count() > 0) options.config.qt = raw.qt;
    }
    return options;
}

Options with_compile_commands(const Options& options, const Env& env, core::Diagnostics& diagnostics) {
    if (!options.compile_commands) return options;
    const std::optional<core::SourceFile> file = core::read_source(*options.compile_commands, diagnostics, env.cwd);
    if (!file) return options;
    const std::optional<std::vector<std::string>> dirs =
        core::include_dirs_of_compile_commands(file->text, *options.compile_commands, diagnostics);
    if (!dirs) return options;

    Options out = options;
    out.include_dirs.insert(out.include_dirs.end(), dirs->begin(), dirs->end());
    if (options.verbose) {
        *env.err << "mirrortest: -I from '" << *options.compile_commands << "':";
        for (const std::string& dir : *dirs) *env.err << ' ' << dir;
        *env.err << (dirs->empty() ? " none\n" : "\n");
    }
    return out;
}

int run(const std::vector<std::string>& args, const Env& env) {
    int exit_code = 0;
    const std::optional<Options> options = parse_options(args, env, exit_code);
    if (!options) {
        return exit_code;
    }
    return options->command == Command::GenImpl ? run_gen_impl(*options, env)
                                                : run_gen_tests(*options, env);
}

}  // namespace mt::cli
