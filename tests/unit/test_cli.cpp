// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "mirrortest/cli/run.h"
#include "test_support.h"

using namespace mt;
using cli::Command;
using cli::Env;
using cli::Options;
using mt::test::TempDir;
using mt::test::write_file;

namespace {

struct Invocation {
    int         code = -1;
    std::string out;
    std::string err;
};

Invocation invoke(const std::vector<std::string>& args, const std::filesystem::path& cwd = {}) {
    std::ostringstream out;
    std::ostringstream err;
    Env env;
    env.cwd = cwd;
    env.out = &out;
    env.err = &err;
    Invocation r;
    r.code = cli::run(args, env);
    r.out = out.str();
    r.err = err.str();
    return r;
}

struct Parsed {
    std::optional<Options> options;
    int         exit_code = -1;
    std::string out;
    std::string err;
};

Parsed parse(const std::vector<std::string>& args, const std::filesystem::path& cwd = {}) {
    std::ostringstream out;
    std::ostringstream err;
    Env env;
    env.cwd = cwd;
    env.out = &out;
    env.err = &err;
    Parsed p;
    p.options = cli::parse_options(args, env, p.exit_code);
    p.out = out.str();
    p.err = err.str();
    return p;
}

// The options of a parse that is expected to succeed. A failure is reported
// with the tool's own message instead of dereferencing an empty optional.
Options opts(const std::vector<std::string>& args, const std::filesystem::path& cwd = {}) {
    Parsed p = parse(args, cwd);
    if (!p.options) {
        ADD_FAILURE() << "parse failed with exit code " << p.exit_code << ":\n" << p.err;
        return Options();
    }
    return *p.options;
}

bool contains(const std::string& text, const std::string& what) {
    return text.find(what) != std::string::npos;
}

}  // namespace

// ---- --version and --help -------------------------------------------------------------

TEST(Cli, VersionGoesToStdout) {
    const Invocation r = invoke({"--version"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.out, "mirrortest " + cli::version() + "\n");
    EXPECT_EQ(r.err, "");
}

TEST(Cli, HelpListsTheCommandsAndGoesToStdout) {
    const Invocation r = invoke({"--help"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.out, "gen-impl"));
    EXPECT_TRUE(contains(r.out, "gen-tests"));
    EXPECT_EQ(r.err, "");
}

TEST(Cli, EachCommandHasItsOwnHelp) {
    const Invocation impl = invoke({"gen-impl", "--help"});
    EXPECT_EQ(impl.code, 0);
    for (const char* option : {"--out-include", "--out-src", "--stub-policy", "--no-exceptions",
                               "--no-rtti", "--auto-policy", "--tests", "--overwrite", "--dry-run",
                               "--check", "--config", "--templates", "--strict", "--quiet", "--verbose",
                               "--std", "-I"}) {
        EXPECT_TRUE(contains(impl.out, option)) << "gen-impl help lacks " << option;
    }
    EXPECT_FALSE(contains(impl.out, "--qt-main"));

    const Invocation tests = invoke({"gen-tests", "--help"});
    EXPECT_EQ(tests.code, 0);
    for (const char* option : {"--header", "--placeholder", "--include-as", "--qt", "--qt-main",
                               "--framework", "--overwrite"}) {
        EXPECT_TRUE(contains(tests.out, option)) << "gen-tests help lacks " << option;
    }
    EXPECT_FALSE(contains(tests.out, "--stub-policy"));
}

TEST(Cli, HelpShowsTheValidChoicesFromTheConfiguration) {
    const Invocation r = invoke({"gen-impl", "--help"});
    EXPECT_TRUE(contains(r.out, "c++11|c++14|c++17|c++20|c++23"));
    EXPECT_TRUE(contains(r.out, "throw|abort|default"));
    EXPECT_TRUE(contains(r.out, "error|todo"));
}

// ---- usage errors ------------------------------------------------------------------------

TEST(CliUsage, NoCommandIsAUsageError) {
    const Invocation r = invoke({});
    EXPECT_EQ(r.code, 2);
    EXPECT_FALSE(r.err.empty());
}

TEST(CliUsage, UnknownCommandAndOptionAreUsageErrors) {
    EXPECT_EQ(invoke({"gen-everything"}).code, 2);
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--bogus"}).code, 2);
    EXPECT_EQ(invoke({"gen-tests", "h.h", "--out", "o", "--stub-policy", "throw"}).code, 2)
        << "--stub-policy belongs to gen-impl only";
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--qt", "6"}).code, 2)
        << "--qt belongs to gen-tests only";
}

TEST(CliUsage, AnOptionWithoutItsValueIsAUsageError) {
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out"}).code, 2);
}

TEST(CliUsage, NoInputsIsAUsageError) {
    const Parsed impl = parse({"gen-impl", "--out", "o"});
    EXPECT_FALSE(impl.options.has_value());
    EXPECT_EQ(impl.exit_code, 2);
    EXPECT_TRUE(contains(impl.err, "no test files given"));
    EXPECT_TRUE(contains(impl.err, "Try 'mirrortest gen-impl --help'."));

    const Parsed tests = parse({"gen-tests", "--out", "o"});
    EXPECT_EQ(tests.exit_code, 2);
    EXPECT_TRUE(contains(tests.err, "no headers given"));
}

TEST(CliUsage, GenImplNeedsSomewhereToWrite) {
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {"gen-impl", "t.cpp"},
             {"gen-impl", "t.cpp", "--out-include", "inc"},
             {"gen-impl", "t.cpp", "--out-src", "src"}}) {
        const Parsed p = parse(args);
        EXPECT_EQ(p.exit_code, 2);
        EXPECT_TRUE(contains(p.err, "say where to write")) << p.err;
    }
    EXPECT_TRUE(parse({"gen-impl", "t.cpp", "--out-include", "inc", "--out-src", "src"}).options.has_value());
}

TEST(CliUsage, GenTestsNeedsOut) {
    const Parsed p = parse({"gen-tests", "h.h"});
    EXPECT_EQ(p.exit_code, 2);
    EXPECT_TRUE(contains(p.err, "--out DIR"));
}

TEST(CliUsage, ValuesOutsideTheirSetAreUsageErrorsThatSayWhatIsValid) {
    const Invocation std_bad = invoke({"gen-impl", "t.cpp", "--out", "o", "--std", "c++98"});
    EXPECT_EQ(std_bad.code, 2);
    EXPECT_TRUE(contains(std_bad.err, "c++98"));
    EXPECT_TRUE(contains(std_bad.err, "c++11"));

    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--overwrite", "always"}).code, 2);
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--stub-policy", "hope"}).code, 2);
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--auto-policy", "yes"}).code, 2);
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--framework", "catch2"}).code, 2);
    EXPECT_EQ(invoke({"gen-tests", "h.h", "--out", "o", "--framework", "auto"}).code, 2)
        << "there is nothing to detect in a header";
    EXPECT_EQ(invoke({"gen-tests", "h.h", "--out", "o", "--qt", "4"}).code, 2);
    EXPECT_EQ(invoke({"gen-tests", "h.h", "--out", "o", "--qt-main", "console"}).code, 2);
    EXPECT_EQ(invoke({"gen-tests", "h.h", "--out", "o", "--placeholder", "pass"}).code, 2);
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--std", "C++11"}).code, 2)
        << "spellings are case-sensitive";
}

TEST(CliUsage, QuietAndVerboseExcludeEachOther) {
    EXPECT_EQ(invoke({"gen-impl", "t.cpp", "--out", "o", "--quiet", "--verbose"}).code, 2);
}

TEST(CliUsage, IncludeAsNeedsExactlyOneHeader) {
    const Parsed p = parse({"gen-tests", "a.h", "b.h", "--out", "o", "--include-as", "x/a.h"});
    EXPECT_EQ(p.exit_code, 2);
    EXPECT_TRUE(contains(p.err, "--include-as"));
    EXPECT_TRUE(parse({"gen-tests", "a.h", "--out", "o", "--include-as", "x/a.h"}).options.has_value());
}

// ---- what gets parsed -----------------------------------------------------------------------

TEST(CliParse, MinimalGenImpl) {
    const Parsed p = parse({"gen-impl", "tests/foo_test.cpp", "--out", "src"});
    ASSERT_TRUE(p.options.has_value()) << p.err;
    const Options& o = *p.options;
    EXPECT_EQ(o.command, Command::GenImpl);
    EXPECT_EQ(o.inputs, std::vector<std::string>{"tests/foo_test.cpp"});
    EXPECT_EQ(o.out, std::optional<std::string>("src"));
    EXPECT_EQ(o.header_dir(), "src");
    EXPECT_EQ(o.source_dir(), "src");
    EXPECT_FALSE(o.framework.has_value()) << "gen-impl detects the framework by default";
    EXPECT_EQ(o.overwrite, core::OverwriteMode::Never);
    EXPECT_FALSE(o.dry_run || o.check || o.strict || o.quiet || o.verbose);
    EXPECT_TRUE(o.include_dirs.empty());
    EXPECT_FALSE(o.config_file || o.templates_dir || o.include_as);
    EXPECT_EQ(o.config.cxx_std, core::CxxStd::Cxx17);
    EXPECT_EQ(p.err, "");
}

TEST(CliParse, MinimalGenTests) {
    const Parsed p = parse({"gen-tests", "include/foo.h", "--out", "tests"});
    ASSERT_TRUE(p.options.has_value()) << p.err;
    const Options& o = *p.options;
    EXPECT_EQ(o.command, Command::GenTests);
    EXPECT_EQ(o.inputs, std::vector<std::string>{"include/foo.h"});
    EXPECT_EQ(o.framework, std::optional<ir::Framework>(ir::Framework::GTest));
    EXPECT_EQ(o.qt_main, cli::QtMain::GuiLess);
    EXPECT_EQ(o.config.placeholder, core::Placeholder::Fail);
    EXPECT_EQ(o.config.qt, 6);
}

TEST(CliParse, OutIncludeAndOutSrcFallBackToOutIndependently) {
    const Options a = opts({"gen-impl", "t.cpp", "--out", "o", "--out-include", "inc"});
    EXPECT_EQ(a.header_dir(), "inc");
    EXPECT_EQ(a.source_dir(), "o");

    const Options b = opts({"gen-impl", "t.cpp", "--out", "o", "--out-src", "src"});
    EXPECT_EQ(b.header_dir(), "o");
    EXPECT_EQ(b.source_dir(), "src");

    const Options c = opts({"gen-impl", "t.cpp", "--out-include", "inc", "--out-src", "src"});
    EXPECT_EQ(c.header_dir(), "inc");
    EXPECT_EQ(c.source_dir(), "src");
}

TEST(CliParse, PositionalFilesComeBeforeThoseGivenWithTheOption) {
    const Options impl =
        opts({"gen-impl", "a.cpp", "--out", "o", "--tests", "x.cpp", "--tests", "y.cpp", "b.cpp"});
    EXPECT_EQ(impl.inputs, (std::vector<std::string>{"a.cpp", "b.cpp", "x.cpp", "y.cpp"}));

    const Options tests =
        opts({"gen-tests", "a.h", "--out", "o", "--header", "x.h"});
    EXPECT_EQ(tests.inputs, (std::vector<std::string>{"a.h", "x.h"}));
}

TEST(CliParse, IncludeDirectoriesAccumulateAndDoNotEatTheInputs) {
    // `-I inc test.cpp` must leave test.cpp as an input, not swallow it as a
    // second include directory.
    const Options o = opts({"gen-impl", "-I", "inc", "test.cpp", "-I", "other", "-Ilast", "--out", "o"});
    EXPECT_EQ(o.include_dirs, (std::vector<std::string>{"inc", "other", "last"}));
    EXPECT_EQ(o.inputs, std::vector<std::string>{"test.cpp"});
}

TEST(CliParse, TheTestsOptionDoesNotEatFollowingPositionals) {
    const Options o = opts({"gen-impl", "--tests", "a.cpp", "b.cpp", "--out", "o"});
    EXPECT_EQ(o.inputs, (std::vector<std::string>{"b.cpp", "a.cpp"}));
}

TEST(CliParse, GenImplOptionsReachTheOptionsAndTheConfig) {
    TempDir dir;
    write_file(dir.path() / "none.json", "{}");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--framework", "qtest",
                            "--stub-policy", "default", "--no-exceptions", "--no-rtti",
                            "--auto-policy", "todo", "--std", "c++11", "--overwrite", "force",
                            "--dry-run", "--check", "--strict", "--verbose",
                            "--templates", "tpl", "--config", "none.json"},
                           dir.path());
    ASSERT_TRUE(p.options.has_value()) << p.err;
    const Options& o = *p.options;
    EXPECT_EQ(o.framework, std::optional<ir::Framework>(ir::Framework::QTest));
    EXPECT_EQ(o.config.stub_policy, core::StubPolicy::Default);
    EXPECT_TRUE(o.config.no_exceptions);
    EXPECT_TRUE(o.config.no_rtti);
    EXPECT_EQ(o.config.auto_policy, core::AutoPolicy::Todo);
    EXPECT_EQ(o.config.cxx_std, core::CxxStd::Cxx11);
    EXPECT_EQ(o.overwrite, core::OverwriteMode::Force);
    EXPECT_TRUE(o.dry_run);
    EXPECT_TRUE(o.check);
    EXPECT_TRUE(o.strict);
    EXPECT_TRUE(o.verbose);
    EXPECT_FALSE(o.quiet);
    EXPECT_EQ(o.templates_dir, std::optional<std::string>("tpl"));
    EXPECT_EQ(o.config_file, std::optional<std::string>("none.json"));
}

TEST(CliParse, EveryGenTestsOption) {
    const Options o = opts({"gen-tests", "foo.h", "--out", "t", "--framework", "qtest",
                              "--placeholder", "skip", "--include-as", "acme/foo.h", "--qt", "5",
                              "--qt-main", "gui", "--quiet", "--overwrite", "merge"});
    EXPECT_EQ(o.framework, std::optional<ir::Framework>(ir::Framework::QTest));
    EXPECT_EQ(o.config.placeholder, core::Placeholder::Skip);
    EXPECT_EQ(o.include_as, std::optional<std::string>("acme/foo.h"));
    EXPECT_EQ(o.config.qt, 5);
    EXPECT_EQ(o.qt_main, cli::QtMain::Gui);
    EXPECT_TRUE(o.quiet);
    EXPECT_EQ(o.overwrite, core::OverwriteMode::Merge);

    EXPECT_EQ(parse({"gen-tests", "foo.h", "--out", "t", "--qt-main", "appless"}).options->qt_main,
              cli::QtMain::Appless);
}

// ---- configuration precedence --------------------------------------------------------------------

TEST(CliConfig, TheFileOverridesTheDefaults) {
    TempDir dir;
    write_file(dir.path() / "mt.json", R"({"std":"c++11","guard":"pragma-once","stub_policy":"abort"})");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "mt.json"}, dir.path());
    ASSERT_TRUE(p.options.has_value()) << p.err;
    EXPECT_EQ(p.options->config.cxx_std, core::CxxStd::Cxx11);
    EXPECT_EQ(p.options->config.guard, core::GuardStyle::PragmaOnce);
    EXPECT_EQ(p.options->config.stub_policy, core::StubPolicy::Abort);
}

TEST(CliConfig, TheCommandLineOverridesTheFile) {
    TempDir dir;
    write_file(dir.path() / "mt.json", R"({"std":"c++11","stub_policy":"abort","auto_policy":"todo"})");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "mt.json", "--std", "c++14",
                            "--stub-policy", "throw"},
                           dir.path());
    ASSERT_TRUE(p.options.has_value()) << p.err;
    EXPECT_EQ(p.options->config.cxx_std, core::CxxStd::Cxx14);
    EXPECT_EQ(p.options->config.stub_policy, core::StubPolicy::Throw);
    EXPECT_EQ(p.options->config.auto_policy, core::AutoPolicy::Todo) << "not given on the command line";
}

TEST(CliConfig, AnOptionNotGivenNeverOverridesTheFileWithItsDefault) {
    // --std defaults to c++17; a file that says c++11 must win when --std is absent.
    TempDir dir;
    write_file(dir.path() / "mt.json", R"({"std":"c++11","qt":5,"placeholder":"skip"})");
    const Parsed impl = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "mt.json"}, dir.path());
    EXPECT_EQ(impl.options->config.cxx_std, core::CxxStd::Cxx11);
    const Parsed tests = parse({"gen-tests", "h.h", "--out", "o", "--config", "mt.json"}, dir.path());
    EXPECT_EQ(tests.options->config.qt, 5);
    EXPECT_EQ(tests.options->config.placeholder, core::Placeholder::Skip);
}

TEST(CliConfig, NoExceptionsOnTheCommandLineWinsOverAFileThatSaysFalse) {
    TempDir dir;
    write_file(dir.path() / "mt.json", R"({"no_exceptions":false})");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "mt.json", "--no-exceptions"}, dir.path());
    EXPECT_TRUE(p.options->config.no_exceptions);
}

TEST(CliConfig, AFileTurningOnNoExceptionsIsKeptWithoutTheFlag) {
    TempDir dir;
    write_file(dir.path() / "mt.json", R"({"no_exceptions":true})");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "mt.json"}, dir.path());
    EXPECT_TRUE(p.options->config.no_exceptions);
}

TEST(CliConfig, ARelativeConfigPathIsResolvedAgainstTheWorkingDirectoryOfTheRun) {
    TempDir dir;
    write_file(dir.path() / "conf" / "mt.json", R"({"std":"c++20"})");
    EXPECT_EQ(parse({"gen-impl", "t.cpp", "--out", "o", "--config", "conf/mt.json"}, dir.path())
                  .options->config.cxx_std,
              core::CxxStd::Cxx20);
}

TEST(CliConfig, AMissingConfigFileIsAnIoErrorShownAsWritten) {
    TempDir dir;
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "missing.json"}, dir.path());
    EXPECT_FALSE(p.options.has_value());
    EXPECT_EQ(p.exit_code, 3);
    EXPECT_EQ(p.err, "missing.json: error: cannot read file: no such file [mt-io]\n");
}

TEST(CliConfig, ABadConfigFileIsAConfigErrorNamingTheFile) {
    TempDir dir;
    write_file(dir.path() / "bad.json", R"({"colour":"red"})");
    const Parsed p = parse({"gen-impl", "t.cpp", "--out", "o", "--config", "bad.json"}, dir.path());
    EXPECT_FALSE(p.options.has_value());
    EXPECT_EQ(p.exit_code, 1);
    EXPECT_EQ(p.err, "bad.json: error: unknown configuration key 'colour' [mt-config]\n");
}

// ---- running ---------------------------------------------------------------------------------------

TEST(CliRun, TheCommandsReportThatTheyAreNotImplementedYet) {
    const Invocation impl = invoke({"gen-impl", "t.cpp", "--out", "o"});
    EXPECT_EQ(impl.code, 1);
    EXPECT_EQ(impl.err, "mirrortest: error: gen-impl is not implemented yet (see docs/plan.md) [mt-unsupported]\n");
    EXPECT_EQ(impl.out, "");

    const Invocation tests = invoke({"gen-tests", "h.h", "--out", "o"});
    EXPECT_EQ(tests.code, 1);
    EXPECT_TRUE(contains(tests.err, "gen-tests is not implemented yet"));
}

// ---- finishing a run ----------------------------------------------------------------------------------

namespace {

struct Finished {
    int         code;
    std::string out;
    std::string err;
};

Finished finish(const Options& options, core::Diagnostics diagnostics,
                const core::WriteReport* report = nullptr) {
    std::ostringstream out;
    std::ostringstream err;
    Env env;
    env.out = &out;
    env.err = &err;
    Finished f;
    f.code = cli::finish_run(options, env, diagnostics, report);
    f.out = out.str();
    f.err = err.str();
    return f;
}

core::WriteReport sample_report() {
    core::WriteReport r;
    core::FileResult created;
    created.path = "src/foo.h";
    created.action = core::WriteAction::Create;
    created.target = "src/foo.h";
    created.diff = "--- /dev/null\n+++ b/src/foo.h\n@@ -0,0 +1 @@\n+x\n";
    core::FileResult same;
    same.path = "src/foo.cpp";
    same.action = core::WriteAction::Unchanged;
    same.target = "src/foo.cpp";
    r.files = {created, same};
    return r;
}

core::WriteReport nothing_changes() {
    core::WriteReport r = sample_report();
    r.files.erase(r.files.begin());
    return r;
}

}  // namespace

TEST(CliFinish, SuccessPrintsTheSummaryToStderrOnly) {
    const core::WriteReport report = sample_report();
    const Finished f = finish(Options(), {}, &report);
    EXPECT_EQ(f.code, 0);
    EXPECT_EQ(f.out, "");
    EXPECT_EQ(f.err, "created src/foo.h\nunchanged src/foo.cpp\n");
}

TEST(CliFinish, QuietSuppressesTheSummary) {
    Options o;
    o.quiet = true;
    const core::WriteReport report = sample_report();
    const Finished f = finish(o, {}, &report);
    EXPECT_EQ(f.code, 0);
    EXPECT_EQ(f.err, "");
}

TEST(CliFinish, DryRunPrintsTheDiffsToStdoutAndAWouldSummaryToStderr) {
    Options o;
    o.dry_run = true;
    const core::WriteReport report = sample_report();
    const Finished f = finish(o, {}, &report);
    EXPECT_EQ(f.code, 0);
    EXPECT_EQ(f.out, "--- /dev/null\n+++ b/src/foo.h\n@@ -0,0 +1 @@\n+x\n");
    EXPECT_EQ(f.err, "would create src/foo.h\nunchanged src/foo.cpp\n");
}

TEST(CliFinish, CheckIsSilentAndExitsFourWhenSomethingWouldChange) {
    Options o;
    o.check = true;
    const core::WriteReport changed = sample_report();
    const Finished f = finish(o, {}, &changed);
    EXPECT_EQ(f.code, 4);
    EXPECT_EQ(f.out, "");
    EXPECT_EQ(f.err, "");

    const core::WriteReport clean = nothing_changes();
    EXPECT_EQ(finish(o, {}, &clean).code, 0);
}

TEST(CliFinish, CheckWithDryRunPrintsTheDiffAndStillExitsFour) {
    Options o;
    o.check = true;
    o.dry_run = true;
    const core::WriteReport report = sample_report();
    const Finished f = finish(o, {}, &report);
    EXPECT_EQ(f.code, 4);
    EXPECT_EQ(f.out, "--- /dev/null\n+++ b/src/foo.h\n@@ -0,0 +1 @@\n+x\n");
    EXPECT_EQ(f.err, "") << "--check stays quiet apart from diagnostics";
}

TEST(CliFinish, WithoutAReportThereIsNothingToSummarise) {
    EXPECT_EQ(finish(Options(), {}).code, 0);
}

TEST(CliFinish, DiagnosticsGoToStderrSorted) {
    core::Diagnostics d;
    ir::SourceLoc late;
    late.file = "b.cpp";
    late.line = 3;
    ir::SourceLoc early;
    early.file = "a.cpp";
    early.line = 9;
    d.warning(core::code::literal, "second", late);
    d.warning(core::code::literal, "first", early);

    const Finished f = finish(Options(), d);
    EXPECT_EQ(f.code, 0) << "warnings alone do not fail the run";
    EXPECT_EQ(f.err,
              "a.cpp:9: warning: first [mt-literal]\n"
              "b.cpp:3: warning: second [mt-literal]\n");
}

TEST(CliFinish, QuietHidesNotesButKeepsWarnings) {
    Options o;
    o.quiet = true;
    core::Diagnostics d;
    d.note(core::code::conditional, "a note");
    d.warning(core::code::literal, "a warning");
    const Finished f = finish(o, d);
    EXPECT_EQ(f.err, "mirrortest: warning: a warning [mt-literal]\n");

    EXPECT_NE(finish(Options(), d).err.find("a note"), std::string::npos);
}

TEST(CliFinish, StrictTurnsWarningsIntoFailures) {
    Options o;
    o.strict = true;
    core::Diagnostics d;
    d.warning(core::code::literal, "w");
    const Finished f = finish(o, d);
    EXPECT_EQ(f.code, 1);
    EXPECT_EQ(f.err, "mirrortest: error: w [mt-literal]\n");
}

TEST(CliFinish, AnErrorSuppressesTheSummaryAndTheDiffs) {
    Options o;
    o.dry_run = true;
    core::Diagnostics d;
    d.error(core::code::conflict, "e");
    const core::WriteReport report = sample_report();
    const Finished f = finish(o, d, &report);
    EXPECT_EQ(f.code, 1);
    EXPECT_EQ(f.out, "");
    EXPECT_EQ(f.err, "mirrortest: error: e [mt-conflict]\n");
}

TEST(CliExitCode, ThreeOnlyWhenEveryErrorIsAnIoError) {
    core::Diagnostics none;
    EXPECT_EQ(cli::exit_code_for(none), 0);

    core::Diagnostics warnings;
    warnings.warning(core::code::literal, "w");
    EXPECT_EQ(cli::exit_code_for(warnings), 0);

    core::Diagnostics io;
    io.error(core::code::io, "a");
    io.error(core::code::io, "b");
    EXPECT_EQ(cli::exit_code_for(io), 3);

    core::Diagnostics input;
    input.error(core::code::auto_deduced, "a");
    EXPECT_EQ(cli::exit_code_for(input), 1);

    core::Diagnostics mixed;
    mixed.error(core::code::io, "a");
    mixed.error(core::code::config, "b");
    EXPECT_EQ(cli::exit_code_for(mixed), 1);

    core::Diagnostics io_and_warning;
    io_and_warning.error(core::code::io, "a");
    io_and_warning.warning(core::code::literal, "w");
    EXPECT_EQ(cli::exit_code_for(io_and_warning), 3);
}

TEST(CliResolve, RelativePathsJoinTheWorkingDirectoryAbsoluteOnesDoNot) {
    Env env;
    env.cwd = "/work/dir";
    EXPECT_EQ(cli::resolve(env, "a/b.h"), std::filesystem::path("/work/dir/a/b.h"));
    EXPECT_EQ(cli::resolve(env, "/abs/b.h"), std::filesystem::path("/abs/b.h"));

    Env no_cwd;
    EXPECT_EQ(cli::resolve(no_cwd, "a/b.h"), std::filesystem::path("a/b.h"));
}
