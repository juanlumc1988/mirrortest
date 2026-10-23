// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Tests for the golden runner itself, against a fake command. A golden
// harness that quietly accepts anything is worse than none, so every way it
// can say "this is wrong" is exercised here.

#include <gtest/gtest.h>

#include <atomic>
#include <ostream>
#include <string>
#include <vector>

#include "golden.h"
#include "test_support.h"

using namespace mt::golden;
using mt::test::read_file;
using mt::test::TempDir;
using mt::test::write_file;
namespace fs = std::filesystem;

namespace {

// Writes `files` (relative path -> content) under `root`.
void populate(const fs::path& root, const std::map<std::string, std::string>& files) {
    for (const auto& f : files) {
        write_file(root / f.first, f.second);
    }
}

bool contains(const std::string& text, const std::string& what) {
    return text.find(what) != std::string::npos;
}

bool any_contains(const std::vector<std::string>& problems, const std::string& what) {
    for (const std::string& p : problems) {
        if (contains(p, what)) return true;
    }
    return false;
}

// A command that writes one header, prints one line on each stream, succeeds.
const CliFn kWritesHeader = [](const std::vector<std::string>&, const mt::cli::Env& env) {
    write_file(env.cwd / "out" / "x.h", "// x\n");
    *env.out << "to stdout\n";
    *env.err << "to stderr\n";
    return 0;
};

// A case directory that kWritesHeader satisfies.
fs::path make_passing_case(const TempDir& dir, const std::string& name = "g/passing") {
    const fs::path c = dir.path() / name;
    populate(c, {{"args.txt", "gen-impl\n${IN}/t.cpp\n"},
                 {"input/t.cpp", "// input\n"},
                 {"expected/x.h", "// x\n"},
                 {"stdout.txt", "to stdout\n"},
                 {"stderr.txt", "to stderr\n"}});
    return c;
}

}  // namespace

// ---- discovery ------------------------------------------------------------------------

TEST(GoldenDiscover, FindsCasesSortedByNameAndSkipsEverythingElse) {
    TempDir dir;
    populate(dir.path(), {{"b/two/args.txt", ""},
                          {"a/one/args.txt", ""},
                          {"a/no_args/notes.md", "no args.txt here"},
                          {"README.md", "not a case"}});
    const std::vector<Case> cases = discover(dir.path());
    ASSERT_EQ(cases.size(), 2u);
    EXPECT_EQ(cases[0].name, "a/one");
    EXPECT_EQ(cases[1].name, "b/two");
    EXPECT_EQ(cases[0].dir, dir.path() / "a" / "one");
}

TEST(GoldenDiscover, AnArgsFileInsideACasesTreesIsJustData) {
    TempDir dir;
    populate(dir.path(), {{"g/case/args.txt", ""},
                          {"g/case/input/args.txt", "some input that happens to be named args.txt"},
                          {"g/case/existing/sub/args.txt", ""},
                          {"g/case/expected/args.txt", ""}});
    const std::vector<Case> cases = discover(dir.path());
    ASSERT_EQ(cases.size(), 1u);
    EXPECT_EQ(cases[0].name, "g/case");
}

TEST(GoldenDiscover, AMissingRootIsNoCases) {
    TempDir dir;
    EXPECT_TRUE(discover(dir.path() / "nope").empty());
}

// ---- reading a case ----------------------------------------------------------------------

TEST(GoldenLoad, ReadsArgumentsAndSubstitutesInAndOut) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", "gen-impl\n${IN}/foo_test.cpp\n--out\n${OUT}\n--config=${IN}/c.json\n"},
                          {"exit_code.txt", "2\n"}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems)) << (problems.empty() ? "" : problems[0]);
    EXPECT_EQ(spec.args, (std::vector<std::string>{"gen-impl", "in/foo_test.cpp", "--out", "out",
                                                   "--config=in/c.json"}));
    EXPECT_EQ(spec.expected_code, 2);
}

TEST(GoldenLoad, AnEmptyArgsFileMeansNoArguments) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", ""}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems));
    EXPECT_TRUE(spec.args.empty());
    EXPECT_EQ(spec.expected_code, 0);
}

TEST(GoldenLoad, ABlankLineIsAMistakeNotAnEmptyArgument) {
    for (const char* text : {"a\n\nb\n", "\n", "a\n\n"}) {
        TempDir dir;
        populate(dir.path(), {{"args.txt", text}});
        Spec spec;
        std::vector<std::string> problems;
        EXPECT_FALSE(load_spec(dir.path(), spec, problems)) << text;
        EXPECT_TRUE(any_contains(problems, "is empty")) << text;
    }
}

TEST(GoldenLoad, TheFinalNewlineIsOptional) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", "a\nb"}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems));
    EXPECT_EQ(spec.args, (std::vector<std::string>{"a", "b"}));
}

TEST(GoldenLoad, ProblemsInTheLayoutAreReportedTogether) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", "a\r\nb\r\n"},
                          {"exit_code.txt", "two\n"},
                          {"expeced/x.h", "typo for expected/"},
                          {"stdout.text", "typo for stdout.txt"}});
    Spec spec;
    std::vector<std::string> problems;
    EXPECT_FALSE(load_spec(dir.path(), spec, problems));
    EXPECT_TRUE(any_contains(problems, "LF line endings"));
    EXPECT_TRUE(any_contains(problems, "exit_code.txt must hold a number"));
    EXPECT_TRUE(any_contains(problems, "unknown entry 'expeced'"));
    EXPECT_TRUE(any_contains(problems, "unknown entry 'stdout.text'"));
}

TEST(GoldenLoad, MissingArgsFile) {
    TempDir dir;
    Spec spec;
    std::vector<std::string> problems;
    EXPECT_FALSE(load_spec(dir.path(), spec, problems));
    EXPECT_TRUE(any_contains(problems, "args.txt is missing"));
}

TEST(GoldenLoad, TheTreesMustBeDirectories) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", ""}, {"expected", "a file, not a directory"}});
    Spec spec;
    std::vector<std::string> problems;
    EXPECT_FALSE(load_spec(dir.path(), spec, problems));
    EXPECT_TRUE(any_contains(problems, "expected must be a directory"));
}

TEST(GoldenLoad, NotesAreIgnored) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", ""}, {"notes.md", "why this case exists"}});
    Spec spec;
    std::vector<std::string> problems;
    EXPECT_TRUE(load_spec(dir.path(), spec, problems));
}

// ---- running -------------------------------------------------------------------------------

TEST(GoldenExecute, RunsInAFreshDirectoryWithInputAndExistingInPlace) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", "go\n"},
                          {"input/sub/a.cpp", "input a\n"},
                          {"existing/old.h", "already there\n"}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems));

    std::string seen_in;
    std::string seen_cwd;
    const Result r = execute(spec, [&](const std::vector<std::string>& args, const mt::cli::Env& env) {
        seen_in = read_file(env.cwd / "in" / "sub" / "a.cpp");
        seen_cwd = env.cwd.string();
        EXPECT_EQ(args, std::vector<std::string>{"go"});
        write_file(env.cwd / "out" / "new.h", "created\n");
        return 7;
    });

    EXPECT_EQ(r.code, 7);
    EXPECT_EQ(seen_in, "input a\n");
    EXPECT_EQ(seen_cwd, r.work_dir);
    EXPECT_EQ(r.tree.size(), 2u);
    EXPECT_EQ(r.tree.at("old.h"), "already there\n");
    EXPECT_EQ(r.tree.at("new.h"), "created\n");
}

TEST(GoldenExecute, EachRunGetsItsOwnDirectory) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", ""}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems));

    const CliFn cli = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "out" / "marker", "x");
        return 0;
    };
    const Result a = execute(spec, cli);
    const Result b = execute(spec, cli);
    EXPECT_NE(a.work_dir, b.work_dir);
    EXPECT_FALSE(fs::exists(a.work_dir)) << "the work directory is removed afterwards";
}

TEST(GoldenExecute, TheInputIsNeverModifiedInTheRepository) {
    TempDir dir;
    populate(dir.path(), {{"args.txt", ""}, {"input/a.cpp", "original\n"}});
    Spec spec;
    std::vector<std::string> problems;
    ASSERT_TRUE(load_spec(dir.path(), spec, problems));
    execute(spec, [](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "in" / "a.cpp", "scribbled on\n");
        return 0;
    });
    EXPECT_EQ(read_file(dir.path() / "input" / "a.cpp"), "original\n");
}

// ---- comparing --------------------------------------------------------------------------------

TEST(GoldenCheck, AMatchingCasePasses) {
    TempDir dir;
    const fs::path c = make_passing_case(dir);
    EXPECT_TRUE(check_case({"g/passing", c}, kWritesHeader, false).empty());
}

TEST(GoldenCheck, EachKindOfDifferenceIsReportedWithADiff) {
    TempDir dir;
    const fs::path c = make_passing_case(dir);
    populate(c, {{"expected/x.h", "// something else\n"},
                 {"expected/missing.h", "never produced\n"},
                 {"stdout.txt", "different stdout\n"},
                 {"stderr.txt", ""},
                 {"exit_code.txt", "3\n"}});
    fs::remove(c / "stderr.txt");

    const std::vector<std::string> problems = check_case({"g/passing", c}, kWritesHeader, false);
    EXPECT_TRUE(any_contains(problems, "exit code: expected 3, got 0"));
    EXPECT_TRUE(any_contains(problems, "stdout differs"));
    EXPECT_TRUE(any_contains(problems, "-different stdout"));
    EXPECT_TRUE(any_contains(problems, "+to stdout"));
    EXPECT_TRUE(any_contains(problems, "stderr differs"));
    EXPECT_TRUE(any_contains(problems, "output file 'x.h' differs"));
    EXPECT_TRUE(any_contains(problems, "-// something else"));
    EXPECT_TRUE(any_contains(problems, "missing output file 'missing.h'"));
}

TEST(GoldenCheck, AnUnexpectedOutputFileFails) {
    TempDir dir;
    const fs::path c = make_passing_case(dir);
    fs::remove(c / "expected" / "x.h");
    const std::vector<std::string> problems = check_case({"g/passing", c}, kWritesHeader, false);
    EXPECT_TRUE(any_contains(problems, "unexpected output file 'x.h'"));
}

TEST(GoldenCheck, AMissingExpectationMeansEmptyOrZero) {
    TempDir dir;
    populate(dir.path(), {{"g/quiet/args.txt", ""}});
    const CliFn silent = [](const std::vector<std::string>&, const mt::cli::Env&) { return 0; };
    EXPECT_TRUE(check_case({"g/quiet", dir.path() / "g" / "quiet"}, silent, false).empty());

    const CliFn noisy = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        *env.err << "unexpected\n";
        return 1;
    };
    const auto problems = check_case({"g/quiet", dir.path() / "g" / "quiet"}, noisy, false);
    EXPECT_TRUE(any_contains(problems, "exit code: expected 0, got 1"));
    EXPECT_TRUE(any_contains(problems, "stderr differs"));
}

TEST(GoldenCheck, ALayoutProblemFailsTheCaseWithoutRunningIt) {
    TempDir dir;
    populate(dir.path(), {{"g/bad/args.txt", ""}, {"g/bad/typo", "x"}});
    bool ran = false;
    const CliFn cli = [&](const std::vector<std::string>&, const mt::cli::Env&) { ran = true; return 0; };
    const auto problems = check_case({"g/bad", dir.path() / "g" / "bad"}, cli, false);
    EXPECT_TRUE(any_contains(problems, "unknown entry 'typo'"));
    EXPECT_FALSE(ran);
}

// ---- determinism and conventions ------------------------------------------------------------------

TEST(GoldenCheck, ADifferenceBetweenTwoRunsIsFlagged) {
    TempDir dir;
    populate(dir.path(), {{"g/flaky/args.txt", ""}});
    std::atomic<int> counter{0};
    const CliFn flaky = [&](const std::vector<std::string>&, const mt::cli::Env& env) {
        *env.out << "run " << ++counter << "\n";
        return 0;
    };
    const auto problems = check_case({"g/flaky", dir.path() / "g" / "flaky"}, flaky, false);
    EXPECT_TRUE(any_contains(problems, "not deterministic: stdout changed between runs"));
}

TEST(GoldenCheck, OutputFilesThatChangeBetweenRunsAreFlagged) {
    TempDir dir;
    populate(dir.path(), {{"g/flaky/args.txt", ""}});
    std::atomic<int> counter{0};
    const CliFn flaky = [&](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "out" / "x.h", "// " + std::to_string(++counter) + "\n");
        return 0;
    };
    EXPECT_TRUE(any_contains(check_case({"g/flaky", dir.path() / "g" / "flaky"}, flaky, false),
                             "the output files changed between runs"));
}

TEST(GoldenConventions, CatchWhatTheyAreMeantTo) {
    Result r;
    r.out = "ok\n";
    r.err = "ok\n";
    r.tree["good.h"] = "// fine\n";
    EXPECT_TRUE(check_conventions(r, {}).empty());

    Result bad;
    bad.out = "no final newline";
    bad.err = "has\r\ncarriage return\n";
    bad.tree["crlf.h"] = "a\r\nb\r\n";
    bad.tree["trailing.h"] = "int x; \nint y;\t\n";
    bad.tree["noeol.h"] = "int z;";
    const auto problems = check_conventions(bad, {});
    EXPECT_TRUE(any_contains(problems, "stdout does not end with a newline"));
    EXPECT_TRUE(any_contains(problems, "stderr contains a carriage return"));
    EXPECT_TRUE(any_contains(problems, "output file 'crlf.h' contains a carriage return"));
    EXPECT_TRUE(any_contains(problems, "output file 'trailing.h' line 1 has trailing whitespace"));
    EXPECT_TRUE(any_contains(problems, "output file 'trailing.h' line 2 has trailing whitespace"));
    EXPECT_TRUE(any_contains(problems, "output file 'noeol.h' does not end with a newline"));
}

TEST(GoldenConventions, StreamsMayHaveTrailingWhitespaceFilesMayNot) {
    // A unified diff prints an empty context line as a single space.
    Result r;
    r.out = "@@ -1,2 +1,2 @@\n \n-a\n+b\n";
    EXPECT_TRUE(check_conventions(r, {}).empty());

    Result file;
    file.tree["x.h"] = "int x; \n";
    EXPECT_FALSE(check_conventions(file, {}).empty());
}

TEST(GoldenConventions, ForbiddenPathsMustNotAppearAnywhere) {
    Result r;
    r.out = "wrote /tmp/work/out/x.h\n";
    r.tree["x.h"] = "// from /tmp/work/in/t.cpp\n";
    const auto problems = check_conventions(r, {"/tmp/work"});
    EXPECT_TRUE(any_contains(problems, "stdout contains the path '/tmp/work'"));
    EXPECT_TRUE(any_contains(problems, "output file 'x.h' contains the path '/tmp/work'"));
    EXPECT_TRUE(check_conventions(r, {}).empty());
}

TEST(GoldenCheck, OutputThatLeaksTheWorkDirectoryFails) {
    TempDir dir;
    populate(dir.path(), {{"g/leaky/args.txt", ""}});
    const CliFn leaky = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        *env.err << "reading " << env.cwd.string() << "/in/t.cpp\n";
        return 0;
    };
    EXPECT_TRUE(any_contains(check_case({"g/leaky", dir.path() / "g" / "leaky"}, leaky, false),
                             "must not depend on where it ran"));
}

// ---- updating -------------------------------------------------------------------------------------

TEST(GoldenUpdate, WritesTheExpectationsAndTheCasePassesAfterwards) {
    TempDir dir;
    populate(dir.path(), {{"g/new/args.txt", "x\n"}});
    const Case c{"g/new", dir.path() / "g" / "new"};
    const CliFn cli = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "out" / "sub" / "x.h", "// x\n");
        *env.out << "out line\n";
        *env.err << "err line\n";
        return 4;
    };

    EXPECT_FALSE(check_case(c, cli, false).empty()) << "nothing recorded yet";
    EXPECT_TRUE(check_case(c, cli, true).empty()) << "update mode reports nothing when it succeeds";

    EXPECT_EQ(read_file(c.dir / "expected" / "sub" / "x.h"), "// x\n");
    EXPECT_EQ(read_file(c.dir / "stdout.txt"), "out line\n");
    EXPECT_EQ(read_file(c.dir / "stderr.txt"), "err line\n");
    EXPECT_EQ(read_file(c.dir / "exit_code.txt"), "4\n");
    EXPECT_TRUE(check_case(c, cli, false).empty());
}

TEST(GoldenUpdate, RemovesWhatIsNoLongerProduced) {
    TempDir dir;
    const fs::path c = make_passing_case(dir);
    populate(c, {{"expected/stale.h", "no longer generated\n"}, {"exit_code.txt", "9\n"}});

    const CliFn quiet = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "out" / "x.h", "// x\n");
        return 0;
    };
    ASSERT_TRUE(check_case({"g/passing", c}, quiet, true).empty());

    EXPECT_FALSE(fs::exists(c / "expected" / "stale.h"));
    EXPECT_TRUE(fs::exists(c / "expected" / "x.h"));
    EXPECT_FALSE(fs::exists(c / "stdout.txt")) << "empty output leaves no file";
    EXPECT_FALSE(fs::exists(c / "stderr.txt"));
    EXPECT_FALSE(fs::exists(c / "exit_code.txt")) << "a zero exit code leaves no file";
    EXPECT_TRUE(fs::exists(c / "args.txt"));
    EXPECT_TRUE(fs::exists(c / "input" / "t.cpp"));
}

TEST(GoldenUpdate, AnEmptyOutputTreeLeavesNoExpectedDirectory) {
    TempDir dir;
    populate(dir.path(), {{"g/none/args.txt", ""}, {"g/none/expected/old.h", "stale\n"}});
    const Case c{"g/none", dir.path() / "g" / "none"};
    const CliFn silent = [](const std::vector<std::string>&, const mt::cli::Env&) { return 0; };
    ASSERT_TRUE(check_case(c, silent, true).empty());
    EXPECT_FALSE(fs::exists(c.dir / "expected"));
}

TEST(GoldenUpdate, RefusesToBlessOutputThatBreaksAConvention) {
    TempDir dir;
    populate(dir.path(), {{"g/bad/args.txt", ""}});
    const Case c{"g/bad", dir.path() / "g" / "bad"};
    const CliFn crlf = [](const std::vector<std::string>&, const mt::cli::Env& env) {
        write_file(env.cwd / "out" / "x.h", "int x;\r\n");
        return 0;
    };
    const auto problems = check_case(c, crlf, true);
    EXPECT_TRUE(any_contains(problems, "carriage return"));
    EXPECT_FALSE(fs::exists(c.dir / "expected")) << "nothing may be written";
}

TEST(GoldenUpdate, RefusesToBlessNonDeterministicOutput) {
    TempDir dir;
    populate(dir.path(), {{"g/flaky/args.txt", ""}});
    const Case c{"g/flaky", dir.path() / "g" / "flaky"};
    std::atomic<int> counter{0};
    const CliFn flaky = [&](const std::vector<std::string>&, const mt::cli::Env& env) {
        *env.out << ++counter << "\n";
        return 0;
    };
    EXPECT_FALSE(check_case(c, flaky, true).empty());
    EXPECT_FALSE(fs::exists(c.dir / "stdout.txt"));
}
