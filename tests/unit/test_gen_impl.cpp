// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// gen-impl as the user runs it: files in a directory, a command line, what
// lands on disk and what is printed. What the generated text looks like is
// pinned by the golden cases; these are about the wiring.

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "mirrortest/cli/run.h"
#include "test_support.h"

using namespace mt;
using mt::test::read_file;
using mt::test::TempDir;
using mt::test::write_file;

namespace {

namespace fs = std::filesystem;

struct Outcome {
    int         code = -1;
    std::string out;
    std::string err;
};

Outcome gen_impl(const TempDir& dir, std::vector<std::string> args) {
    std::ostringstream out, err;
    cli::Env env;
    env.cwd = dir.path();
    env.out = &out;
    env.err = &err;
    args.insert(args.begin(), "gen-impl");
    Outcome r;
    r.code = cli::run(args, env);
    r.out = out.str();
    r.err = err.str();
    return r;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
bool exists(const TempDir& dir, const std::string& relative) { return fs::exists(dir.path() / relative); }
std::string read(const TempDir& dir, const std::string& relative) { return read_file(dir.path() / relative); }

const char* kFooTest =
    "#include <gtest/gtest.h>\n"
    "#include \"acme/foo.h\"\n"
    "\n"
    "TEST(FooTest, StartsWithThreeItems) {\n"
    "    const acme::Foo foo(3);\n"
    "    EXPECT_EQ(foo.size(), 3u);\n"
    "    EXPECT_FALSE(foo.empty());\n"
    "}\n";

void write_foo_test(const TempDir& dir) { write_file(dir.path() / "tests/foo_test.cpp", kFooTest); }

// ---- the basic run ------------------------------------------------------------------------------------------

TEST(GenImpl, WritesTheHeaderAndTheSourceAndSaysSo) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created src/acme/foo.h\ncreated src/acme/foo.cpp\n");
    EXPECT_EQ(r.out, "");
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "unsigned int size() const;"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "Foo::Foo(int /*arg1*/) {}"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "from tests/foo_test.cpp."));
}

TEST(GenImpl, ASecondRunChangesNothingAndLeavesTheFilesAlone) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    const auto before = fs::last_write_time(dir.path() / "src/acme/foo.h");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "unchanged src/acme/foo.h\nunchanged src/acme/foo.cpp\n");
    EXPECT_EQ(fs::last_write_time(dir.path() / "src/acme/foo.h"), before);
}

TEST(GenImpl, ADifferingFileGetsADotNewBesideItAndIsNotTouched) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    write_file(dir.path() / "src/acme/foo.h", "// mine\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.err, "wrote src/acme/foo.h.new (src/acme/foo.h exists and differs)")) << r.err;
    EXPECT_EQ(read(dir, "src/acme/foo.h"), "// mine\n");
    EXPECT_TRUE(exists(dir, "src/acme/foo.h.new"));
}

TEST(GenImpl, ForceOverwrites) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    write_file(dir.path() / "src/acme/foo.h", "// mine\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "force"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.err, "overwrote src/acme/foo.h")) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "unsigned int size() const;"));
}

TEST(GenImpl, DryRunWritesNothingAndPrintsTheDiffs) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--dry-run"});
    EXPECT_EQ(r.code, 0);
    EXPECT_FALSE(exists(dir, "src"));
    EXPECT_TRUE(contains(r.out, "+++ b/src/acme/foo.h")) << r.out;
    EXPECT_TRUE(contains(r.err, "would create src/acme/foo.h")) << r.err;
}

TEST(GenImpl, CheckSaysWhetherAnythingWouldChange) {
    const TempDir dir;
    write_foo_test(dir);
    EXPECT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--check"}).code, 4);
    EXPECT_FALSE(exists(dir, "src"));
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    const Outcome clean = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--check"});
    EXPECT_EQ(clean.code, 0);
    EXPECT_EQ(clean.err, "");
}

TEST(GenImpl, HeadersAndSourcesCanGoToDifferentDirectories) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out-include", "include", "--out-src", "lib"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "include/acme/foo.h"));
    EXPECT_TRUE(exists(dir, "lib/acme/foo.cpp"));
    EXPECT_FALSE(exists(dir, "include/acme/foo.cpp"));
}

TEST(GenImpl, AbsolutePathsWork) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {dir.file("tests/foo_test.cpp"), "--out", dir.file("out")});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "out/acme/foo.h"));
    EXPECT_TRUE(contains(read(dir, "out/acme/foo.h"), "from " + dir.file("tests/foo_test.cpp") + "."))
        << "the path is shown as the user gave it";
}

// ---- what stops a run -------------------------------------------------------------------------------------

TEST(GenImpl, AMissingTestFileIsAnIoError) {
    const TempDir dir;
    const Outcome r = gen_impl(dir, {"tests/nope_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 3);
    EXPECT_EQ(r.err, "tests/nope_test.cpp: error: cannot read file: no such file [mt-io]\n");
    EXPECT_FALSE(exists(dir, "src"));
}

TEST(GenImpl, AFileThatNamesNoFrameworkIsAnErrorUnlessTheFlagSaysWhich) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "// not a test file\nvoid helper() { Foo foo(1); foo.size(); }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "cannot tell which test framework")) << r.err;
    EXPECT_TRUE(contains(r.err, "[mt-framework]"));
    EXPECT_FALSE(exists(dir, "src"));

    const Outcome forced = gen_impl(dir, {"t.cpp", "--out", "src", "--framework", "gtest"});
    EXPECT_EQ(forced.code, 0) << forced.err;
}

TEST(GenImpl, AFileWithBothFrameworksIsAmbiguous) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp",
               "#include <gtest/gtest.h>\n#include <QtTest>\nTEST(S, N) { Foo foo(1); }\nclass T : public QObject { Q_OBJECT private slots: void a(); };\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "uses both GoogleTest and QTest")) << r.err;
}

// ---- Qt Test -----------------------------------------------------------------------------------------------

const char* kQtFooTest =
    "#include <QtTest>\n"
    "#include \"acme/foo.h\"\n"
    "\n"
    "class TestFoo : public QObject {\n"
    "    Q_OBJECT\n"
    "private slots:\n"
    "    void startsWithThreeItems();\n"
    "    void names_data();\n"
    "    void names();\n"
    "};\n"
    "\n"
    "void TestFoo::startsWithThreeItems() {\n"
    "    const acme::Foo foo(3);\n"
    "    QCOMPARE(foo.size(), 3u);\n"
    "    QVERIFY(!foo.empty());\n"
    "    QCOMPARE(foo.name(), \"foo\");\n"
    "}\n"
    "\n"
    "void TestFoo::names_data() {\n"
    "    QTest::addColumn<int>(\"count\");\n"
    "    QTest::newRow(\"three\") << 3;\n"
    "}\n"
    "\n"
    "void TestFoo::names() {\n"
    "    QFETCH(int, count);\n"
    "    acme::Foo foo(count);\n"
    "    QBENCHMARK { foo.touch(); }\n"
    "}\n"
    "\n"
    "QTEST_MAIN(TestFoo)\n"
    "#include \"tst_foo.moc\"\n";

TEST(GenImplQt, ATestFileGeneratesTheClassItTestsAndNotTheMocFileOrTheTestClass) {
    const TempDir dir;
    write_file(dir.path() / "tests/tst_foo.cpp", kQtFooTest);
    const Outcome r = gen_impl(dir, {"tests/tst_foo.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "created src/acme/foo.h\ncreated src/acme/foo.cpp\n")) << r.err;
    EXPECT_TRUE(contains(r.err, "'acme::Foo::touch' is never used")) << "a call inside QBENCHMARK is a statement like another: " << r.err;
    EXPECT_FALSE(exists(dir, "src/tst_foo.moc"));
    EXPECT_FALSE(exists(dir, "src/tst_foo.h"));
    const std::string header = read(dir, "src/acme/foo.h");
    EXPECT_TRUE(contains(header, "explicit Foo(int count);")) << "named after the QFETCH variable: " << header;
    EXPECT_TRUE(contains(header, "unsigned int size()")) << header;
    EXPECT_TRUE(contains(header, "bool empty()")) << header;
    EXPECT_TRUE(contains(header, "QString name()")) << "a string literal is a QString to QCOMPARE: " << header;
    EXPECT_TRUE(contains(header, "#include <QString>"));
    EXPECT_TRUE(contains(header, "void touch();"));
    EXPECT_FALSE(contains(header, "TestFoo"));
}

TEST(GenImplQt, TheFrameworkCanBeSaidInsteadOfDetected) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include \"foo.h\"\nvoid run() { Foo foo(1); QCOMPARE(foo.size(), 2u); }\n");
    const Outcome unsure = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(unsure.code, 1) << "no include, no slot, no macro: nothing says which framework";
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src", "--framework", "qtest"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/foo.h"), "unsigned int size()"));
}

TEST(GenImplQt, ASecondRunChangesNothing) {
    const TempDir dir;
    write_file(dir.path() / "tests/tst_foo.cpp", kQtFooTest);
    ASSERT_EQ(gen_impl(dir, {"tests/tst_foo.cpp", "--out", "src"}).code, 0);
    const Outcome r = gen_impl(dir, {"tests/tst_foo.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.err, "unchanged src/acme/foo.h\nunchanged src/acme/foo.cpp\n")) << r.err;
}

TEST(GenImplQt, NoParseWarningIsMadeOfTheQtMacros) {
    const TempDir dir;
    write_file(dir.path() / "tests/tst_foo.cpp", kQtFooTest);
    EXPECT_FALSE(contains(gen_impl(dir, {"tests/tst_foo.cpp", "--out", "src"}).err, "mt-parse"));
}

TEST(GenImpl, AnErrorInTheInputMeansNoFileIsWritten) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp",
               "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); auto x = foo.get(); (void)x; }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "[mt-auto]")) << r.err;
    EXPECT_FALSE(exists(dir, "src"));
    EXPECT_EQ(r.out, "");
}

TEST(GenImpl, TheTodoPolicyTurnsThoseErrorsIntoWarningsAndWritesTheFiles) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp",
               "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); auto x = foo.get(); (void)x; }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src", "--auto-policy", "todo"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "warning")) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/foo.h"), "int get();"));
}

TEST(GenImpl, StrictTurnsAWarningIntoAnErrorAndWritesNothing) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); foo.run(); }\n");
    const Outcome lax = gen_impl(dir, {"t.cpp", "--out", "a"});
    EXPECT_EQ(lax.code, 0) << lax.err;
    EXPECT_TRUE(contains(lax.err, "[mt-unused-result]"));

    const Outcome strict = gen_impl(dir, {"t.cpp", "--out", "b", "--strict"});
    EXPECT_EQ(strict.code, 1);
    EXPECT_TRUE(contains(strict.err, "error: ")) << strict.err;
    EXPECT_FALSE(exists(dir, "b"));
}

TEST(GenImpl, AnErrorInOneFileStopsTheWholeRun) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "tests/bad_test.cpp", "#include <gtest/gtest.h>\n#include \"bad.h\"\nTEST(S, N) { Bad b(1); auto x = b.get(); (void)x; }\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "tests/bad_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1);
    EXPECT_FALSE(exists(dir, "src")) << "all or nothing";
}

// ---- what exists already -----------------------------------------------------------------------------------

TEST(GenImpl, AHeaderThatExistsOnAnIncludeDirectoryIsTheApiNotSomethingToGenerate) {
    const TempDir dir;
    write_file(dir.path() / "inc/acme/foo.h", "namespace acme { class Foo { public: explicit Foo(int); unsigned size() const; }; }\n");
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "-I", "inc"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "src/acme/foo.h"));
    // Foo is known, so only what the header does not have could be generated: the class is not.
    EXPECT_FALSE(exists(dir, "src/acme/foo.cpp"));
}

TEST(GenImpl, YourOwnOutputOnTheIncludePathIsNotMistakenForTheApi) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "-I", "src"}).code, 0);
    const Outcome again = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "-I", "src"});
    EXPECT_EQ(again.code, 0) << again.err;
    EXPECT_EQ(again.err, "unchanged src/acme/foo.h\nunchanged src/acme/foo.cpp\n") << "found in the output directory: still a target";
}

// ---- --compile-commands ------------------------------------------------------------------------------------

// The compiler ran in build/, so '-I../inc' is the inc/ next to it.
const char* kDatabase = R"([{"directory":"build","command":"c++ -I../inc -c ../tests/foo_test.cpp","file":"../tests/foo_test.cpp"}])";

void write_existing_foo(const TempDir& dir) {
    write_file(dir.path() / "inc/acme/foo.h", "namespace acme { class Foo { public: explicit Foo(int); unsigned size() const; }; }\n");
}

TEST(GenImplCompileCommands, TheIncludeDirectoriesOfTheDatabaseFindTheHeadersThatExist) {
    const TempDir dir;
    write_existing_foo(dir);
    write_foo_test(dir);
    write_file(dir.path() / "build/compile_commands.json", kDatabase);

    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "without"}).code, 0);
    EXPECT_TRUE(exists(dir, "without/acme/foo.h")) << "nothing says where the header is: it is generated";

    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "with", "--compile-commands", "build/compile_commands.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "with/acme/foo.h")) << "found on the database's -I: it is the API";
    EXPECT_FALSE(exists(dir, "with/acme/foo.cpp"));
}

TEST(GenImplCompileCommands, AnIncludeDirectoryGivenOnTheCommandLineStillWorksBesideIt) {
    const TempDir dir;
    write_existing_foo(dir);
    write_foo_test(dir);
    write_file(dir.path() / "build/compile_commands.json", R"([{"command":"c++ -Inowhere"}])");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "-I", "inc", "--compile-commands", "build/compile_commands.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "src/acme/foo.h"));
}

TEST(GenImplCompileCommands, AFileThatIsNotThereIsAnIoError) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--compile-commands", "build/missing.json"});
    EXPECT_EQ(r.code, 3) << r.err;
    EXPECT_TRUE(contains(r.err, "build/missing.json")) << r.err;
    EXPECT_TRUE(contains(r.err, "[mt-io]")) << r.err;
    EXPECT_FALSE(exists(dir, "src"));
}

TEST(GenImplCompileCommands, AFileThatIsNotADatabaseIsAnErrorAndNothingIsWritten) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "cc.json", "{}");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--compile-commands", "cc.json"});
    EXPECT_EQ(r.code, 1) << r.err;
    EXPECT_TRUE(contains(r.err, "cc.json: error: not a compilation database: a JSON array of entries is expected [mt-config]")) << r.err;
    EXPECT_FALSE(exists(dir, "src"));

    write_file(dir.path() / "cc.json", "[{");
    const Outcome broken = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--compile-commands", "cc.json"});
    EXPECT_EQ(broken.code, 1) << broken.err;
    EXPECT_TRUE(contains(broken.err, "cc.json: error: invalid JSON: ")) << broken.err;
}

TEST(GenImplCompileCommands, AnEntryItCannotReadIsAWarningAndStrictMakesItAnError) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "cc.json", R"([{"directory":"."}])");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--compile-commands", "cc.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "cc.json: warning: entry 1 of the compilation database has neither 'arguments' nor 'command'; it is skipped [mt-config]")) << r.err;
    EXPECT_TRUE(exists(dir, "src/acme/foo.h"));

    const Outcome strict = gen_impl(dir, {"tests/foo_test.cpp", "--out", "strict", "--strict", "--compile-commands", "cc.json"});
    EXPECT_EQ(strict.code, 1) << strict.err;
    EXPECT_FALSE(exists(dir, "strict"));
}

TEST(GenImplCompileCommands, VerboseSaysWhichDirectoriesCameFromTheFile) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "build/compile_commands.json", kDatabase);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--verbose", "--compile-commands", "build/compile_commands.json"});
    EXPECT_TRUE(contains(r.err, "mirrortest: -I from 'build/compile_commands.json': inc\n")) << r.err;

    write_file(dir.path() / "empty.json", "[]");
    const Outcome none = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src2", "--verbose", "--compile-commands", "empty.json"});
    EXPECT_TRUE(contains(none.err, "mirrortest: -I from 'empty.json': none\n")) << none.err;

    const Outcome quiet = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src3", "--compile-commands", "build/compile_commands.json"});
    EXPECT_FALSE(contains(quiet.err, "-I from")) << "only with --verbose";
}

// ---- --stub-policy hook ------------------------------------------------------------------------------------

TEST(GenImplHook, TheStubsCallTheHookAndItsHeaderIsWrittenWithTheOthers) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created src/acme/foo.h\ncreated src/acme/foo.cpp\ncreated src/mt_not_implemented.h\n");
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "#include \"mt_not_implemented.h\"\n"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "    mt_not_implemented(\"acme::Foo::size() const\");\n    return {};\n"));
    EXPECT_FALSE(contains(read(dir, "src/acme/foo.cpp"), "throw"));
    EXPECT_TRUE(contains(read(dir, "src/mt_not_implemented.h"), "inline void mt_not_implemented(const char* signature) {"));
    EXPECT_TRUE(contains(read(dir, "src/mt_not_implemented.h"), "from tests/foo_test.cpp."));
}

TEST(GenImplHook, ASecondRunChangesNothingAndNeitherDoesMerge) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook"}).code, 0);
    const Outcome again = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook"});
    EXPECT_EQ(again.err, "unchanged src/acme/foo.h\nunchanged src/acme/foo.cpp\nunchanged src/mt_not_implemented.h\n");
    EXPECT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook", "--check"}).code, 0);
    const Outcome merged = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook", "--overwrite", "merge"});
    EXPECT_EQ(merged.code, 0) << merged.err;
    EXPECT_TRUE(contains(merged.err, "unchanged src/mt_not_implemented.h\n")) << merged.err;
}

TEST(GenImplHook, TheHeaderGoesWhereTheHeadersGoAndTheSourceFindsItThere) {
    const TempDir dir;
    write_foo_test(dir);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out-include", "inc", "--out-src", "src", "--stub-policy", "hook"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "inc/mt_not_implemented.h"));
    EXPECT_FALSE(exists(dir, "src/mt_not_implemented.h"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "#include \"mt_not_implemented.h\"\n"));
}

TEST(GenImplHook, NoOtherPolicyWritesIt) {
    const TempDir dir;
    write_foo_test(dir);
    for (const char* policy : {"throw", "abort", "default"}) {
        const std::string out = std::string("out_") + policy;
        ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", out, "--stub-policy", policy}).code, 0) << policy;
        EXPECT_FALSE(exists(dir, out + "/mt_not_implemented.h")) << policy;
        EXPECT_FALSE(contains(read(dir, out + "/acme/foo.cpp"), "mt_not_implemented")) << policy;
    }
}

TEST(GenImplHook, TheConfigurationFileCanChooseItAndNoExceptionsChangesTheDefaultHandlerOnly) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "mt.json", R"({"stub_policy":"hook","no_exceptions":true})");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "mt_not_implemented(\"acme::Foo::size() const\");"));
    EXPECT_TRUE(contains(read(dir, "src/mt_not_implemented.h"), "std::abort();"));
    EXPECT_FALSE(contains(read(dir, "src/mt_not_implemented.h"), "throw"));
}

TEST(GenImplHook, AHookHeaderThatDiffersGetsANewFileBesideItLikeAnyOther) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook"}).code, 0);
    const std::string before = read(dir, "src/mt_not_implemented.h");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--stub-policy", "hook", "--no-exceptions"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "src/mt_not_implemented.h.new")) << r.err;
    EXPECT_EQ(read(dir, "src/mt_not_implemented.h"), before);
    EXPECT_TRUE(contains(read(dir, "src/mt_not_implemented.h.new"), "std::abort();"));
}

TEST(GenImpl, FrameworkHeadersAreNeverGenerated) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include \"gtest/gtest.h\"\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); EXPECT_EQ(foo.size(), 2u); }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "src/gtest"));
    EXPECT_TRUE(exists(dir, "src/foo.h"));
}

TEST(GenImpl, AnIncludeNobodyUsesIsStillWrittenSoTheTestCompiles) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\n#include \"constants.h\"\nTEST(S, N) { Foo foo(1); EXPECT_EQ(foo.size(), 2u); }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/constants.h"), "#ifndef CONSTANTS_H"));
    EXPECT_FALSE(exists(dir, "src/constants.cpp"));
}

// ---- --overwrite merge -----------------------------------------------------------------------------------------

const char* kFooTestMore =
    "#include <gtest/gtest.h>\n"
    "#include \"acme/foo.h\"\n"
    "\n"
    "TEST(FooTest, StartsWithThreeItems) {\n"
    "    const acme::Foo foo(3);\n"
    "    EXPECT_EQ(foo.size(), 3u);\n"
    "    EXPECT_FALSE(foo.empty());\n"
    "    EXPECT_EQ(foo.name(), std::string(\"x\"));\n"
    "}\n";

TEST(GenImplMerge, AddsTheNewMembersToTheFilesOfAnEarlierRun) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    write_file(dir.path() / "tests/foo_test.cpp", kFooTestMore);

    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "merged 1 member into src/acme/foo.h\nmerged 1 member into src/acme/foo.cpp\n");
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "    std::string name() const;\n"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "#include <string>\n"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "std::string Foo::name() const {"));
    EXPECT_FALSE(exists(dir, "src/acme/foo.h.new"));

    // And the next run has nothing to add.
    const Outcome again = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge"});
    EXPECT_EQ(again.code, 0);
    EXPECT_EQ(again.err, "unchanged src/acme/foo.h\nunchanged src/acme/foo.cpp\n");
}

TEST(GenImplMerge, WhatTheUserWroteIsKept) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);

    // The user implements size() and adds a member of their own.
    std::string source = read(dir, "src/acme/foo.cpp");
    const std::string stub = "    throw std::logic_error(\"mirrortest: not implemented: acme::Foo::size() const\");";
    ASSERT_TRUE(contains(source, stub));
    source.replace(source.find(stub), stub.size(), "    return 3;");
    write_file(dir.path() / "src/acme/foo.cpp", source);
    std::string header = read(dir, "src/acme/foo.h");
    header.insert(header.find("    // <mt:end acme::Foo>"), "    int mine() const;\n");
    write_file(dir.path() / "src/acme/foo.h", header);

    write_file(dir.path() / "tests/foo_test.cpp", kFooTestMore);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "    return 3;\n"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "    int mine() const;\n"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "    std::string name() const;\n"));
}

TEST(GenImplMerge, AClassWithoutARegionGetsADotNewAndAWarning) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "src/acme/foo.h", "namespace acme { class Foo { public: explicit Foo(int); }; }\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "[mt-merge-no-region]")) << r.err;
    EXPECT_TRUE(contains(r.err, "wrote src/acme/foo.h.new")) << r.err;
    EXPECT_EQ(read(dir, "src/acme/foo.h"), "namespace acme { class Foo { public: explicit Foo(int); }; }\n");
}

TEST(GenImplMerge, MalformedRegionsAreAnErrorAndNothingIsWritten) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    std::string header = read(dir, "src/acme/foo.h");
    header.insert(0, "// <mt:begin dangling>\n");
    write_file(dir.path() / "src/acme/foo.h", header);
    write_file(dir.path() / "tests/foo_test.cpp", kFooTestMore);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "src/acme/foo.h:1:1: error: region 'dangling' is never closed [mt-merge-region]")) << r.err;
    EXPECT_FALSE(contains(read(dir, "src/acme/foo.cpp"), "name() const"));
}

TEST(GenImplMerge, DryRunShowsTheMergeWithoutDoingIt) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src"}).code, 0);
    write_file(dir.path() / "tests/foo_test.cpp", kFooTestMore);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge", "--dry-run"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "+    std::string name() const;")) << r.out;
    EXPECT_TRUE(contains(r.err, "would merge 1 member into src/acme/foo.h")) << r.err;
    EXPECT_FALSE(contains(read(dir, "src/acme/foo.h"), "name() const"));
    EXPECT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--overwrite", "merge", "--check"}).code, 4);
}

TEST(GenImplMerge, WorksWithSeparateIncludeAndSourceDirectories) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out-include", "include", "--out-src", "lib"}).code, 0);
    write_file(dir.path() / "tests/foo_test.cpp", kFooTestMore);
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out-include", "include", "--out-src", "lib", "--overwrite", "merge"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(read(dir, "include/acme/foo.h"), "std::string name() const;"));
    EXPECT_TRUE(contains(read(dir, "lib/acme/foo.cpp"), "std::string Foo::name() const {")) << "the header was found under --out-include";
}

// ---- several test files -----------------------------------------------------------------------------------------

TEST(GenImpl, SeveralTestFilesForDifferentClassesAreOneRun) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "tests/bar_test.cpp", "#include <gtest/gtest.h>\n#include \"bar.h\"\nTEST(S, N) { Bar bar; EXPECT_TRUE(bar.ok()); }\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "tests/bar_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "src/acme/foo.h"));
    EXPECT_TRUE(exists(dir, "src/bar.h"));
    EXPECT_TRUE(contains(read(dir, "src/bar.h"), "from tests/bar_test.cpp."));
}

TEST(GenImpl, TwoFilesThatWouldWriteTheSameThingDifferentlyAreAnError) {
    const TempDir dir;
    write_file(dir.path() / "a_test.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); EXPECT_EQ(foo.size(), 2u); }\n");
    write_file(dir.path() / "b_test.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); EXPECT_TRUE(foo.ok()); }\n");
    const Outcome r = gen_impl(dir, {"a_test.cpp", "b_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "b_test.cpp: error: 'a_test.cpp' and 'b_test.cpp' both generate 'src/foo.h'")) << r.err;
    EXPECT_FALSE(exists(dir, "src"));
}

TEST(GenImpl, TwoFilesThatWriteTheSameThingAreFine) {
    const TempDir dir;
    write_file(dir.path() / "a_test.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); EXPECT_EQ(foo.size(), 2u); }\n");
    write_file(dir.path() / "b_test.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); EXPECT_EQ(foo.size(), 2u); }\n");
    // The comments say which file they came from, so the contents differ in that one line...
    const Outcome r = gen_impl(dir, {"a_test.cpp", "b_test.cpp", "--out", "src"});
    EXPECT_EQ(r.code, 1) << "...and that is enough to refuse: the files are not interchangeable";
    // ...but the same file twice is the same input twice.
    const Outcome same = gen_impl(dir, {"a_test.cpp", "a_test.cpp", "--out", "src2"});
    EXPECT_EQ(same.code, 0) << same.err;
    EXPECT_EQ(same.err, "created src2/foo.h\ncreated src2/foo.cpp\n");
}

// ---- templates and configuration ----------------------------------------------------------------------------

TEST(GenImpl, AnOverriddenTemplateChangesTheLayoutAndVerboseSaysSo) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "tpl/common/file_header.inja", "// MY HEADER for {{ source }}\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--templates", "tpl", "--verbose"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.h"), "// MY HEADER for tests/foo_test.cpp\n"));
    EXPECT_TRUE(contains(r.err, "mirrortest: using template 'common/file_header.inja' from 'tpl'")) << r.err;
}

TEST(GenImpl, ABrokenTemplateIsAnErrorAndNothingIsWritten) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "tpl/impl/header.inja", "{{ nope }}\n");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--templates", "tpl"});
    EXPECT_EQ(r.code, 1);
    EXPECT_FALSE(exists(dir, "src"));
    EXPECT_FALSE(r.err.empty());
}

TEST(GenImpl, TheConfigurationFileAndTheFlagsAreBothHonoured) {
    const TempDir dir;
    write_foo_test(dir);
    write_file(dir.path() / "mt.json", R"({"stub_policy": "default", "guard": "pragma-once", "file_header": "// (c) ACME"})");
    const Outcome r = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    const std::string header = read(dir, "src/acme/foo.h");
    EXPECT_TRUE(contains(header, "// (c) ACME\n// Generated by mirrortest")) << header;
    EXPECT_TRUE(contains(header, "#pragma once"));
    EXPECT_TRUE(contains(read(dir, "src/acme/foo.cpp"), "return {};"));
    EXPECT_TRUE(contains(r.err, "[mt-stub-may-pass]"));

    const Outcome flag = gen_impl(dir, {"tests/foo_test.cpp", "--out", "src2", "--config", "mt.json", "--stub-policy", "abort"});
    EXPECT_EQ(flag.code, 0) << flag.err;
    EXPECT_TRUE(contains(read(dir, "src2/acme/foo.cpp"), "std::abort();")) << "the flag wins over the file";
}

TEST(GenImpl, NoExceptionsMeansAbort) {
    const TempDir dir;
    write_foo_test(dir);
    ASSERT_EQ(gen_impl(dir, {"tests/foo_test.cpp", "--out", "src", "--no-exceptions"}).code, 0);
    const std::string source = read(dir, "src/acme/foo.cpp");
    EXPECT_TRUE(contains(source, "std::abort();")) << source;
    EXPECT_TRUE(contains(source, "#include <cstdlib>"));
    EXPECT_FALSE(contains(source, "throw"));
}

TEST(GenImpl, TheIgnoreMacrosListReachesTheFrontend) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); MY_CHECK(foo.size() > 0); }\n");
    write_file(dir.path() / "mt.json", R"({"ignore_macros": ["MY_CHECK"]})");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(contains(read(dir, "src/foo.h"), "size")) << "the macro's argument was not read as a call";
}

TEST(GenImpl, WarningsAreSortedAndQuietDropsTheSummary) {
    const TempDir dir;
    write_file(dir.path() / "t.cpp", "#include <gtest/gtest.h>\n#include \"foo.h\"\nTEST(S, N) { Foo foo(1); foo.run(); }\n");
    const Outcome r = gen_impl(dir, {"t.cpp", "--out", "src", "--quiet"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "t.cpp:3:30: warning: the result of 'Foo::run' is never used, so the method is assumed to return void [mt-unused-result]\n");
}

}  // namespace
