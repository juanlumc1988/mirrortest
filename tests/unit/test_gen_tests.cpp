// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// gen-tests as the user runs it: headers in a directory, a command line, what
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

Outcome gen_tests(const TempDir& dir, std::vector<std::string> args) {
    std::ostringstream out, err;
    cli::Env env;
    env.cwd = dir.path();
    env.out = &out;
    env.err = &err;
    args.insert(args.begin(), "gen-tests");
    Outcome r;
    r.code = cli::run(args, env);
    r.out = out.str();
    r.err = err.str();
    return r;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
bool exists(const TempDir& dir, const std::string& relative) { return fs::exists(dir.path() / relative); }
std::string read(const TempDir& dir, const std::string& relative) { return read_file(dir.path() / relative); }

const char* kFooHeader =
    "namespace acme {\n"
    "class Foo {\n"
    "public:\n"
    "    unsigned size() const;\n"
    "};\n"
    "}\n";

void write_foo(const TempDir& dir, const std::string& where = "include/acme/foo.h") {
    write_file(dir.path() / where, kFooHeader);
}

// ---- the basic run ------------------------------------------------------------------------------------------

TEST(GenTests, WritesOneTestFilePerClassAndSaysSo) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created tests/foo_test.cpp\n");
    EXPECT_EQ(r.out, "");
    const std::string text = read(dir, "tests/foo_test.cpp");
    EXPECT_TRUE(contains(text, "from include/acme/foo.h.")) << text;
    EXPECT_TRUE(contains(text, "TEST(FooTest, size) {"));
    EXPECT_TRUE(contains(text, "namespace acme {"));
}

TEST(GenTests, TheHeaderAlsoComesWithDashDashHeader) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"--header", "include/acme/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "tests/foo_test.cpp"));
}

TEST(GenTests, ASecondRunChangesNothingAndLeavesTheFilesAlone) {
    const TempDir dir;
    write_foo(dir);
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests"}).code, 0);
    const auto before = fs::last_write_time(dir.path() / "tests/foo_test.cpp");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "unchanged tests/foo_test.cpp\n");
    EXPECT_EQ(fs::last_write_time(dir.path() / "tests/foo_test.cpp"), before);
}

TEST(GenTests, TestsTheUserHasWrittenAreNeverTouched) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tests/foo_test.cpp", "// my own tests\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.err, "wrote tests/foo_test.cpp.new (tests/foo_test.cpp exists and differs)")) << r.err;
    EXPECT_EQ(read(dir, "tests/foo_test.cpp"), "// my own tests\n");
    EXPECT_TRUE(exists(dir, "tests/foo_test.cpp.new"));
}

TEST(GenTests, ForceOverwrites) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tests/foo_test.cpp", "// mine\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--overwrite", "force"});
    EXPECT_EQ(r.code, 0);
    EXPECT_TRUE(contains(r.err, "overwrote tests/foo_test.cpp")) << r.err;
    EXPECT_TRUE(contains(read(dir, "tests/foo_test.cpp"), "TEST(FooTest, size)"));
}

TEST(GenTests, MergeIsNotSupportedForTestsYetAndFallsBackToDotNew) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tests/foo_test.cpp", "// mine\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--overwrite", "merge"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "[mt-unsupported]")) << r.err;
    EXPECT_EQ(read(dir, "tests/foo_test.cpp"), "// mine\n");
    EXPECT_TRUE(exists(dir, "tests/foo_test.cpp.new"));
}

TEST(GenTests, DryRunPrintsTheDiffAndWritesNothing) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--dry-run"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "+TEST(FooTest, size) {")) << r.out;
    EXPECT_TRUE(contains(r.err, "would create tests/foo_test.cpp"));
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTests, CheckSaysWhetherAnythingWouldChange) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--check"}).code, 4);
    EXPECT_FALSE(exists(dir, "tests"));
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests"}).code, 0);
    EXPECT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--check"}).code, 0);
}

// ---- how the test includes the header -------------------------------------------------------------------------

std::string include_line(const TempDir& dir, const std::vector<std::string>& args) {
    fs::remove_all(dir.path() / "tests");   // each call starts from nothing: an existing file would only get a .new
    std::vector<std::string> a = args;
    a.insert(a.end(), {"--out", "tests"});
    const Outcome r = gen_tests(dir, a);
    EXPECT_EQ(r.code, 0) << r.err;
    const std::string text = read(dir, "tests/foo_test.cpp");
    const std::size_t at = text.find("#include \"");
    return at == std::string::npos ? "<none>" : text.substr(at, text.find('\n', at) - at);
}

TEST(GenTestsInclude, WithoutAnIncludeDirectoryItIsTheFileName) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h"}), "#include \"foo.h\"");
}

TEST(GenTestsInclude, ItIsThePathBelowTheIncludeDirectoryThatHoldsTheHeader) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "include"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, TheFirstIncludeDirectoryThatContainsItWins) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "elsewhere", "-I", "include/acme", "-I", "include"}), "#include \"foo.h\"");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "elsewhere", "-I", "include", "-I", "include/acme"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, ADirectoryThatDoesNotContainTheHeaderIsNoHelp) {
    const TempDir dir;
    write_foo(dir);
    fs::create_directories(dir.path() / "other");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "other"}), "#include \"foo.h\"");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "include/acme/missing"}), "#include \"foo.h\"");
}

TEST(GenTestsInclude, TheIncludeDirectoriesOfACompilationDatabaseCountAsIfGivenWithI) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "build/compile_commands.json", R"([{"directory":"build","command":"c++ -I../include -c x.cpp"}])");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "--compile-commands", "build/compile_commands.json"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, TheOnesGivenOnTheCommandLineAreSearchedBeforeThoseOfTheDatabase) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "cc.json", R"([{"command":"c++ -Iinclude"}])");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "include/acme", "--compile-commands", "cc.json"}), "#include \"foo.h\"");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "--compile-commands", "cc.json", "-I", "include/acme"}), "#include \"foo.h\"")
        << "wherever the options are written";
}

TEST(GenTestsInclude, InsideTheDatabaseTheOrderOfTheCompilerCommandHolds) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "a.json", R"([{"command":"c++ -Iinclude/acme -Iinclude"}])");
    write_file(dir.path() / "b.json", R"([{"command":"c++ -Iinclude -Iinclude/acme"}])");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "--compile-commands", "a.json"}), "#include \"foo.h\"");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "--compile-commands", "b.json"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, ADatabaseThatCannotBeUsedStopsTheRun) {
    const TempDir dir;
    write_foo(dir);
    const Outcome missing = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--compile-commands", "nope.json"});
    EXPECT_EQ(missing.code, 3) << missing.err;
    write_file(dir.path() / "cc.json", "[3]x");
    const Outcome broken = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--compile-commands", "cc.json"});
    EXPECT_EQ(broken.code, 1) << broken.err;
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTestsInclude, ASpellingOfTheDirectoryWithDotsOrASlashIsTheSameDirectory) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "./include/"}), "#include \"acme/foo.h\"");
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "src/../include"}), "#include \"acme/foo.h\"");
    EXPECT_EQ(include_line(dir, {"./include/acme/../acme/foo.h", "-I", "include"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, AnAbsoluteIncludeDirectoryWorksToo) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", (dir.path() / "include").string()}), "#include \"acme/foo.h\"");
}

// A symbolic link is the directory it points to: the compiler finds the header by the real path as well.
bool link_to(const TempDir& dir, const std::string& target, const std::string& link) {
    std::error_code ec;
    fs::create_directory_symlink(dir.path() / target, dir.path() / link, ec);
    return !ec;
}

TEST(GenTestsInclude, AnIncludeDirectoryThatIsALinkIsTheDirectoryItPointsTo) {
    const TempDir dir;
    write_foo(dir);
    if (!link_to(dir, "include", "include_link")) GTEST_SKIP() << "no symbolic links here";
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "include_link"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, AHeaderReachedThroughALinkIsTheHeaderItPointsTo) {
    const TempDir dir;
    write_foo(dir);
    if (!link_to(dir, "include", "include_link")) GTEST_SKIP() << "no symbolic links here";
    EXPECT_EQ(include_line(dir, {"include_link/acme/foo.h", "-I", "include"}), "#include \"acme/foo.h\"");
}

TEST(GenTestsInclude, IncludeAsSaysItOutright) {
    const TempDir dir;
    write_foo(dir);
    EXPECT_EQ(include_line(dir, {"include/acme/foo.h", "-I", "include", "--include-as", "vendor/foo.h"}), "#include \"vendor/foo.h\"");
}

// ---- configuration and options ----------------------------------------------------------------------------------

TEST(GenTestsOptions, ThePlaceholderCanBeASkip) {
    const TempDir dir;
    write_foo(dir);
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--placeholder", "skip"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "tests/foo_test.cpp"), "GTEST_SKIP() << \"TODO(mt)"));
    EXPECT_FALSE(contains(read(dir, "tests/foo_test.cpp"), "FAIL()"));
}

TEST(GenTestsOptions, TheConfigurationNamesTheFilesAndTheSuites) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json",
               R"({"naming": {"gtest_file": "test_{stem}.cc", "suite": "{Class}Spec"}, "file_header": "// (c) acme"})");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    const std::string text = read(dir, "tests/test_foo.cc");
    EXPECT_TRUE(contains(text, "TEST(FooSpec, size)")) << text;
    EXPECT_EQ(text.rfind("// (c) acme\n", 0), 0u) << text;
}

TEST(GenTestsOptions, ATemplateOfTheUserReplacesTheDefault) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tpl/gtest/test_file.inja", "tests of {{ source }}\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--templates", "tpl"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(read(dir, "tests/foo_test.cpp"), "tests of include/acme/foo.h\n");
}

TEST(GenTestsOptions, VerboseSaysWhichTemplatesAreTheUsers) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tpl/gtest/test_file.inja", "x\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--templates", "tpl", "--verbose"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(contains(r.err, "mirrortest: using template 'gtest/test_file.inja' from 'tpl'")) << r.err;
    EXPECT_FALSE(contains(gen_tests(dir, {"include/acme/foo.h", "--out", "tests2", "--templates", "tpl"}).err, "using template"))
        << "only with --verbose";
}

TEST(GenTestsOptions, ABrokenTemplateIsAnErrorAndNothingIsWritten) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "tpl/gtest/test_file.inja", "{{ nope }}\n");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--templates", "tpl"});
    EXPECT_EQ(r.code, 1);
    EXPECT_FALSE(exists(dir, "tests"));
    EXPECT_FALSE(r.err.empty());
}

// The templates are checked before any header is read: a template that cannot work is the first thing the
// user hears, not the last of a list that also has the warnings of every header.
TEST(GenTestsOptions, ATemplateThatCannotWorkIsReportedBeforeTheHeadersAreRead) {
    const TempDir dir;
    write_file(dir.path() / "box.h", "template <typename T> class Box { public: void put(T); };\n");
    write_file(dir.path() / "tpl/gtest/test_file.inja", "{% if unclosed %}\n");
    const Outcome r = gen_tests(dir, {"box.h", "--out", "tests", "--templates", "tpl"});
    EXPECT_EQ(r.code, 1);
    EXPECT_FALSE(contains(r.err, "is skipped")) << "box.h was never read: " << r.err;
}

TEST(GenTestsOptions, ATemplatesDirectoryThatIsNotThereStopsTheRunBeforeTheHeadersAreRead) {
    const TempDir dir;
    write_file(dir.path() / "box.h", "template <typename T> class Box { public: void put(T); };\n");
    const Outcome r = gen_tests(dir, {"box.h", "--out", "tests", "--templates", "no_such_dir"});
    EXPECT_NE(r.code, 0);
    EXPECT_TRUE(contains(r.err, "no_such_dir")) << r.err;
    EXPECT_FALSE(contains(r.err, "is skipped")) << "box.h was never read: " << r.err;
}

TEST(GenTestsOptions, TheIgnoreMacrosListReachesTheFrontend) {
    const TempDir dir;
    write_file(dir.path() / "api.h", "class Foo { public: void f() MY_ATTR(a); void g(); };\n");
    write_file(dir.path() / "mt.json", R"({"ignore_macros": ["MY_ATTR"]})");

    const Outcome plain = gen_tests(dir, {"api.h", "--out", "plain"});
    EXPECT_TRUE(contains(plain.err, "[mt-parse]")) << "a macro nobody told us about confuses the parser: " << plain.err;
    EXPECT_TRUE(contains(read(dir, "plain/foo_test.cpp"), "TEST(FooTest, MY_ATTR)"));

    const Outcome r = gen_tests(dir, {"api.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.err, "created tests/foo_test.cpp\n");
    const std::string text = read(dir, "tests/foo_test.cpp");
    EXPECT_TRUE(contains(text, "TEST(FooTest, f)"));
    EXPECT_FALSE(contains(text, "MY_ATTR"));
}

// ---- a fixture ----------------------------------------------------------------------------------------------

TEST(GenTestsFixture, TheConfigurationAsksForOne) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json", R"({"gtest": {"fixture": true}})");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    const std::string text = read(dir, "tests/foo_test.cpp");
    EXPECT_TRUE(contains(text, "class FooTest : public ::testing::Test {")) << text;
    EXPECT_TRUE(contains(text, "    Foo sut;\n};")) << text;
    EXPECT_TRUE(contains(text, "TEST_F(FooTest, size) {"));
    EXPECT_FALSE(contains(text, "TEST(FooTest"));
}

TEST(GenTestsFixture, WithoutItNothingChanges) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json", R"({"gtest": {"fixture": false}})");
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--config", "mt.json"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "tests/foo_test.cpp"), "TEST(FooTest, size) {"));
    EXPECT_FALSE(contains(read(dir, "tests/foo_test.cpp"), "TEST_F"));
}

TEST(GenTestsFixture, QtTestHasNoneYetAndSaysNothingAboutIt) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json", R"({"gtest": {"fixture": true}})");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--config", "mt.json", "--framework", "qtest"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created tests/tst_foo.cpp\n");
    EXPECT_FALSE(contains(read(dir, "tests/tst_foo.cpp"), "::testing::Test"));
}

TEST(GenTestsFixture, ABadKeyIsAConfigurationError) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json", R"({"gtest": {"fixtures": true}})");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "unknown configuration key 'gtest.fixtures'")) << r.err;
    EXPECT_FALSE(exists(dir, "tests"));
}

// ---- mocks --------------------------------------------------------------------------------------------------

namespace {

const char* kSensorHeader =
    "namespace acme {\n"
    "class Sensor {\n"
    "public:\n"
    "    virtual ~Sensor() = default;\n"
    "    virtual int read() = 0;\n"
    "    virtual std::map<int, int> table() const = 0;\n"
    "};\n"
    "class Controller {\n"
    "public:\n"
    "    int value() const;\n"
    "};\n"
    "}\n";

}  // namespace

TEST(GenTestsMocks, TheConfigurationAsksForOnePerInterface) {
    const TempDir dir;
    write_file(dir.path() / "include/acme/sensor.h", kSensorHeader);
    write_file(dir.path() / "mt.json", R"({"gtest": {"mocks": true}})");
    const Outcome r = gen_tests(dir, {"include/acme/sensor.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "tests/sensor_test.cpp"));
    EXPECT_TRUE(exists(dir, "tests/controller_test.cpp"));
    ASSERT_TRUE(exists(dir, "tests/mock_sensor.h")) << r.err;
    EXPECT_FALSE(exists(dir, "tests/mock_controller.h")) << "Controller is not an interface";
    const std::string text = read(dir, "tests/mock_sensor.h");
    EXPECT_TRUE(contains(text, "#include <gmock/gmock.h>\n#include \"sensor.h\"\n")) << text;
    EXPECT_TRUE(contains(text, "class MockSensor : public Sensor {")) << text;
    EXPECT_TRUE(contains(text, "    MOCK_METHOD(int, read, (), (override));")) << text;
    EXPECT_TRUE(contains(text, "    MOCK_METHOD((std::map<int, int>), table, (), (const, override));")) << text;
}

TEST(GenTestsMocks, TheNamesAreConfigurable) {
    const TempDir dir;
    write_file(dir.path() / "include/acme/sensor.h", kSensorHeader);
    write_file(dir.path() / "mt.json",
               R"({"gtest": {"mocks": true}, "naming": {"mock_file": "{stem}_mock.h", "mock_class": "{Class}Mock"}})");
    const Outcome r = gen_tests(dir, {"include/acme/sensor.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    ASSERT_TRUE(exists(dir, "tests/sensor_mock.h")) << r.err;
    EXPECT_TRUE(contains(read(dir, "tests/sensor_mock.h"), "class SensorMock : public Sensor {"));
}

TEST(GenTestsMocks, WithoutTheOptionThereAreNone) {
    const TempDir dir;
    write_file(dir.path() / "include/acme/sensor.h", kSensorHeader);
    const Outcome r = gen_tests(dir, {"include/acme/sensor.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "tests/mock_sensor.h"));
}

TEST(GenTestsMocks, QtTestHasNoMocksAndSaysNothingAboutIt) {
    const TempDir dir;
    write_file(dir.path() / "include/acme/sensor.h", kSensorHeader);
    write_file(dir.path() / "mt.json", R"({"gtest": {"mocks": true}})");
    const Outcome r = gen_tests(dir, {"include/acme/sensor.h", "--out", "tests", "--config", "mt.json", "--framework", "qtest"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_FALSE(exists(dir, "tests/mock_sensor.h"));
}

TEST(GenTestsMocks, ASecondRunChangesNothing) {
    const TempDir dir;
    write_file(dir.path() / "include/acme/sensor.h", kSensorHeader);
    write_file(dir.path() / "mt.json", R"({"gtest": {"mocks": true}})");
    const std::vector<std::string> args = {"include/acme/sensor.h", "--out", "tests", "--config", "mt.json"};
    ASSERT_EQ(gen_tests(dir, args).code, 0);
    std::vector<std::string> again = args;
    again.push_back("--check");
    EXPECT_EQ(gen_tests(dir, again).code, 0);
}

TEST(GenTestsMocks, TwoHeadersThatShareAnInterfaceNameAreRefused) {
    const TempDir dir;
    write_file(dir.path() / "include/a/sensor.h", "class Sensor { public: virtual int a() = 0; };\n");
    write_file(dir.path() / "include/b/sensor.h", "class Sensor { public: virtual int b() = 0; };\n");
    write_file(dir.path() / "mt.json", R"({"gtest": {"mocks": true}})");
    const Outcome r = gen_tests(dir, {"include/a/sensor.h", "include/b/sensor.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_NE(r.code, 0);
    EXPECT_TRUE(contains(r.err, "both generate")) << r.err;
}

// ---- class templates ---------------------------------------------------------------------------------------

TEST(GenTestsTemplates, AFileForEachListedInstantiation) {
    const TempDir dir;
    write_file(dir.path() / "include/box.h",
               "namespace acme {\ntemplate <typename T, int N = 4> class Box { public: T get(int) const; };\n"
               "template <typename T> class Other { public: T get() const; };\n}\n");
    write_file(dir.path() / "mt.json", R"({"template_instantiations": {"acme::Box": ["int", "std::string, 8"]}})");
    const Outcome r = gen_tests(dir, {"include/box.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    ASSERT_TRUE(exists(dir, "tests/box_int_test.cpp")) << r.err;
    ASSERT_TRUE(exists(dir, "tests/box_std_string_8_test.cpp")) << r.err;
    EXPECT_FALSE(exists(dir, "tests/other_test.cpp"));
    EXPECT_TRUE(contains(read(dir, "tests/box_int_test.cpp"), "TEST(Box_intTest, get) {")) << read(dir, "tests/box_int_test.cpp");
    EXPECT_TRUE(contains(read(dir, "tests/box_std_string_8_test.cpp"), "Box<std::string, 8> sut;"));
    EXPECT_TRUE(contains(r.err, "the class template 'acme::Other' is skipped: list the instantiations to test in template_instantiations")) << r.err;
}

TEST(GenTestsTemplates, AnInstantiationThatCannotBeMadeIsAWarningNotAnError) {
    const TempDir dir;
    write_file(dir.path() / "include/box.h", "template <typename T> class Box { public: T get() const; };\n");
    write_file(dir.path() / "mt.json", R"({"template_instantiations": {"Box": ["int, 3", "char"]}})");
    const Outcome r = gen_tests(dir, {"include/box.h", "--out", "tests", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_TRUE(exists(dir, "tests/box_char_test.cpp"));
    EXPECT_TRUE(contains(r.err, "'Box<int, 3>' gives 2 arguments and the template has 1 [mt-config]")) << r.err;
}

// ---- Qt Test ------------------------------------------------------------------------------------------------

TEST(GenTestsQt, WritesOneTestClassPerClassNamedAfterTheConfiguredPattern) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--framework", "qtest"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created tests/tst_foo.cpp\n");
    const std::string text = read(dir, "tests/tst_foo.cpp");
    EXPECT_TRUE(contains(text, "from include/acme/foo.h.")) << "the header comment names the source: " << text;
    EXPECT_TRUE(contains(text, "class TestFoo : public QObject {")) << text;
    EXPECT_TRUE(contains(text, "void TestFoo::size() {"));
    EXPECT_TRUE(contains(text, "QFAIL(\"TODO(mt): write the test for acme::Foo::size() const\");"));
    EXPECT_TRUE(contains(text, "QTEST_GUILESS_MAIN(acme::TestFoo)\n// AUTOMOC: the name of the moc file must follow the name of this file.\n#include \"tst_foo.moc\"\n"));
    EXPECT_FALSE(exists(dir, "tests/foo_test.cpp"));
}

TEST(GenTestsQt, TheEntryPointIsChosenWithQtMain) {
    const TempDir dir;
    write_foo(dir);
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "a", "--framework", "qtest", "--qt-main", "appless"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "a/tst_foo.cpp"), "QTEST_APPLESS_MAIN(acme::TestFoo)"));
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "b", "--framework", "qtest", "--qt-main", "gui"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "b/tst_foo.cpp"), "QTEST_MAIN(acme::TestFoo)"));
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "c", "--framework", "qtest", "--qt-main", "guiless"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "c/tst_foo.cpp"), "QTEST_GUILESS_MAIN(acme::TestFoo)"));
}

TEST(GenTestsQt, ASkipPlaceholderIsQSkip) {
    const TempDir dir;
    write_foo(dir);
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--framework", "qtest", "--placeholder", "skip"}).code, 0);
    EXPECT_TRUE(contains(read(dir, "tests/tst_foo.cpp"), "QSKIP(\"TODO(mt)"));
    EXPECT_FALSE(contains(read(dir, "tests/tst_foo.cpp"), "QFAIL"));
}

TEST(GenTestsQt, TheNamingConfigurationReachesTheClassAndTheFile) {
    const TempDir dir;
    write_foo(dir);
    write_file(dir.path() / "mt.json", R"({"naming": {"qtest_file": "{stem}_qt.cpp", "qtest_class": "{Class}Spec"}})");
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--framework", "qtest", "--config", "mt.json"});
    EXPECT_EQ(r.code, 0) << r.err;
    const std::string text = read(dir, "tests/foo_qt.cpp");
    EXPECT_TRUE(contains(text, "class FooSpec : public QObject {")) << text;
    EXPECT_TRUE(contains(text, "#include \"foo_qt.moc\"")) << "the moc file follows the file: " << text;
}

TEST(GenTestsQt, ASecondRunChangesNothing) {
    const TempDir dir;
    write_foo(dir);
    ASSERT_EQ(gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--framework", "qtest"}).code, 0);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "--out", "tests", "--framework", "qtest"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "unchanged tests/tst_foo.cpp\n");
}

// ---- what goes wrong -------------------------------------------------------------------------------------------

TEST(GenTestsFailures, AHeaderThatIsNotThereIsAnIoError) {
    const TempDir dir;
    const Outcome r = gen_tests(dir, {"missing.h", "--out", "tests"});
    EXPECT_EQ(r.code, 3);
    EXPECT_TRUE(contains(r.err, "missing.h")) << r.err;
    EXPECT_TRUE(contains(r.err, "[mt-io]"));
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTestsFailures, OneBadHeaderStopsTheWholeRunBeforeAnythingIsWritten) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "missing.h", "--out", "tests"});
    EXPECT_EQ(r.code, 3);
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTestsFailures, StrictMakesTheWarningsErrors) {
    const TempDir dir;
    write_file(dir.path() / "box.h", "template <typename T> class Box { public: void put(T); };\nclass Ok { public: void go(); };\n");
    const Outcome lax = gen_tests(dir, {"box.h", "--out", "tests"});
    EXPECT_EQ(lax.code, 0) << lax.err;
    EXPECT_TRUE(contains(lax.err, "warning: the class template 'Box' is skipped"));
    EXPECT_TRUE(exists(dir, "tests/ok_test.cpp"));

    const TempDir strict_dir;
    write_file(strict_dir.path() / "box.h", "template <typename T> class Box { public: void put(T); };\nclass Ok { public: void go(); };\n");
    const Outcome strict = gen_tests(strict_dir, {"box.h", "--out", "tests", "--strict"});
    EXPECT_EQ(strict.code, 1);
    EXPECT_FALSE(exists(strict_dir, "tests"));
}

TEST(GenTestsFailures, TwoHeadersThatWantTheSameFileWithDifferentContentAreRefused) {
    const TempDir dir;
    write_file(dir.path() / "one/foo.h", "class Foo { public: void a(); };\n");
    write_file(dir.path() / "two/foo.h", "class Foo { public: void b(); };\n");
    const Outcome r = gen_tests(dir, {"one/foo.h", "two/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 1);
    EXPECT_TRUE(contains(r.err, "'one/foo.h' and 'two/foo.h' both generate 'tests/foo_test.cpp' with different content; "
                               "give the headers different output directories, or run them one at a time"))
        << r.err;
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTestsFailures, TheSameHeaderTwiceIsTheSameFileOnce) {
    const TempDir dir;
    write_foo(dir);
    const Outcome r = gen_tests(dir, {"include/acme/foo.h", "include/acme/foo.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created tests/foo_test.cpp\n");
}

TEST(GenTestsFailures, AHeaderWithNothingToTestIsNotAnError) {
    const TempDir dir;
    write_file(dir.path() / "empty.h", "// nothing here\n");
    const Outcome r = gen_tests(dir, {"empty.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "");
    EXPECT_FALSE(exists(dir, "tests"));
}

TEST(GenTestsFailures, FreeFunctionsAreSaidToGetNoTestsAndQuietHidesTheNote) {
    const TempDir dir;
    write_file(dir.path() / "math.h", "int add(int a, int b);\nint sub(int a, int b);\n");
    const Outcome r = gen_tests(dir, {"math.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0);
    EXPECT_EQ(r.err, "math.h:1:5: note: 2 free function(s) in this header get no tests: gen-tests covers classes (docs/design.md, section 9.1) [mt-unsupported]\n");
    EXPECT_EQ(gen_tests(dir, {"math.h", "--out", "tests", "--quiet"}).err, "");
}

TEST(GenTestsFailures, OneFreeFunctionIsCountedToo) {
    const TempDir dir;
    write_file(dir.path() / "math.h", "int add(int a, int b);\n");
    EXPECT_TRUE(contains(gen_tests(dir, {"math.h", "--out", "tests"}).err, "1 free function(s) in this header"));
}

TEST(GenTestsFailures, TheModelIsReadFromTheHeaderEvenWhenItIsBigAndTheFilesAreSortedByClass) {
    const TempDir dir;
    write_file(dir.path() / "two.h", "class Zeta { public: void z(); };\nclass Alpha { public: void a(); };\n");
    const Outcome r = gen_tests(dir, {"two.h", "--out", "tests"});
    EXPECT_EQ(r.code, 0) << r.err;
    EXPECT_EQ(r.err, "created tests/zeta_test.cpp\ncreated tests/alpha_test.cpp\n") << "in the order of the header";
}

}  // namespace
