// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The command line, as a library. apps/cli/main.cpp only builds an Env and
// calls run(); everything else lives here so the tests drive the real thing
// in-process (design section 3): no subprocesses, no chdir, no globals.
//
// Exit codes (design 13.2):
//   0  success, including "nothing to do", --help and --version
//   1  the input was read but could not be processed (error diagnostics)
//   2  usage error: unknown option, missing argument, value outside its set
//   3  I/O error: every error recorded was an mt-io one
//   4  --check, and generating would change at least one file
//
// Precedence (design D9): defaults, then --config FILE, then what was given on
// the command line. Only an option that was actually passed overrides the
// file; its default never does.

#ifndef MT_CLI_RUN_H
#define MT_CLI_RUN_H

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/writer.h"
#include "mirrortest/ir/model.h"

namespace mt::cli {

// "0.1.0".
std::string version();

// What a run runs in. Relative paths are resolved against `cwd`; they are
// always *shown* as the user wrote them.
struct Env {
    std::filesystem::path cwd;
    std::ostream*         out = nullptr;   // stdout: --help, --version, --dry-run diffs
    std::ostream*         err = nullptr;   // stderr: diagnostics, the summary
};

enum class Command { GenImpl, GenTests };
using QtMain = core::QtMain;

// Everything the command line said, validated.
struct Options {
    Command command = Command::GenImpl;

    // Test files (gen-impl) or headers (gen-tests): the positional arguments
    // first, then those given with --tests / --header, in the order written.
    std::vector<std::string> inputs;

    std::optional<std::string> out;           // --out
    std::optional<std::string> out_include;   // --out-include (gen-impl)
    std::optional<std::string> out_src;       // --out-src (gen-impl)

    std::optional<ir::Framework> framework;   // nullopt: detect (gen-impl only)
    std::optional<std::string>   include_as;  // --include-as (gen-tests)
    QtMain                       qt_main = QtMain::GuiLess;

    std::vector<std::string> include_dirs;    // -I, in order
    std::optional<std::string> compile_commands;   // --compile-commands: its -I come after those
    core::OverwriteMode      overwrite = core::OverwriteMode::Never;
    bool dry_run = false;
    bool check   = false;
    bool strict  = false;
    bool quiet   = false;
    bool verbose = false;

    std::optional<std::string> config_file;     // --config
    std::optional<std::string> templates_dir;   // --templates

    // Defaults, overlaid with --config, overlaid with the command line.
    core::Config config;

    // Where gen-impl puts headers and sources: --out-include / --out-src, each
    // falling back to --out. Both are set after a successful parse.
    std::string header_dir() const;
    std::string source_dir() const;
};

// Resolves a user-supplied path against the working directory of the run.
std::filesystem::path resolve(const Env& env, const std::string& path);

// Parses `args` (without the program name). Returns nullopt when the run is
// over before it starts -- --help, --version, a usage error, a configuration
// file that cannot be loaded -- having printed the reason, with the exit code
// in `exit_code`.
std::optional<Options> parse_options(const std::vector<std::string>& args, const Env& env,
                                     int& exit_code);

// What to do once a command has run; every command ends here.
//
//   * --strict turns warnings into errors first;
//   * diagnostics go to stderr, sorted (--quiet leaves out the notes);
//   * with a `report`: --dry-run prints each file's diff to stdout; unless
//     --quiet or --check, the summary goes to stderr;
//   * the exit code: 3 if every error was an mt-io one, 1 for any other error,
//     4 for --check with changes, else 0.
int finish_run(const Options& options, const Env& env, core::Diagnostics& diagnostics,
               const core::WriteReport* report);

// The exit code for the errors alone: 0 if there are none.
int exit_code_for(const core::Diagnostics& diagnostics);

// The options a command runs with. With --compile-commands, reads that file and
// appends the include directories it names to `include_dirs`, after the ones given
// with -I, so that those are searched first. A file that cannot be read, or is not a
// compilation database, is an error in `diagnostics` and leaves the options as they were.
// Without the option, `options` itself.
Options with_compile_commands(const Options& options, const Env& env, core::Diagnostics& diagnostics);

int run_gen_impl(const Options& options, const Env& env);
int run_gen_tests(const Options& options, const Env& env);

// The whole program.
int run(const std::vector<std::string>& args, const Env& env);

}  // namespace mt::cli

#endif  // MT_CLI_RUN_H
