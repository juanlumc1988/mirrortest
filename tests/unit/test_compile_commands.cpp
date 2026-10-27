// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The include directories of a compilation database, and the splitting of a command line they depend on.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/compile_commands.h"

using namespace mt;
using Words = std::vector<std::string>;

namespace {

// ---- split_command_line ------------------------------------------------------------------------------------

TEST(CompileCommandsSplit, BlanksSeparateWords) {
    EXPECT_EQ(core::split_command_line("c++ -c  foo.cpp\t-o\nfoo.o"), (Words{"c++", "-c", "foo.cpp", "-o", "foo.o"}));
    EXPECT_EQ(core::split_command_line("  a  "), Words{"a"});
    EXPECT_TRUE(core::split_command_line("").empty());
    EXPECT_TRUE(core::split_command_line(" \t\n").empty());
}

TEST(CompileCommandsSplit, SingleQuotesKeepEverything) {
    EXPECT_EQ(core::split_command_line(R"('a b' 'c\d' '"' '$x')"), (Words{"a b", "c\\d", "\"", "$x"}));
}

TEST(CompileCommandsSplit, DoubleQuotesEscapeOnlyFourCharacters) {
    EXPECT_EQ(core::split_command_line(R"("a b")"), Words{"a b"});
    EXPECT_EQ(core::split_command_line(R"("say \"hi\"")"), Words{"say \"hi\""});
    EXPECT_EQ(core::split_command_line(R"("c:\\dir")"), Words{"c:\\dir"});
    EXPECT_EQ(core::split_command_line(R"("\$HOME \`x\`")"), Words{"$HOME `x`"});
    EXPECT_EQ(core::split_command_line(R"("a\nb")"), Words{"a\\nb"}) << "any other backslash stays";
    EXPECT_EQ(core::split_command_line(R"("a'b")"), Words{"a'b"}) << "a single quote means nothing inside double ones";
}

TEST(CompileCommandsSplit, OutsideQuotesABackslashEscapesAnyCharacter) {
    EXPECT_EQ(core::split_command_line(R"(a\ b)"), Words{"a b"});
    EXPECT_EQ(core::split_command_line(R"(-DX=\"y\")"), Words{"-DX=\"y\""});
    EXPECT_EQ(core::split_command_line(R"(\'x\')"), Words{"'x'"});
    EXPECT_EQ(core::split_command_line(R"(a\\b)"), Words{"a\\b"});
}

TEST(CompileCommandsSplit, AnEscapedCharacterAloneIsAWord) {
    EXPECT_EQ(core::split_command_line(R"(a \  b)"), (Words{"a", " ", "b"}));
    EXPECT_EQ(core::split_command_line(R"(\\)"), Words{"\\"});
    EXPECT_EQ(core::split_command_line(R"(\  \ )"), (Words{" ", " "})) << "an escaped blank, a blank, an escaped blank";
    EXPECT_EQ(core::split_command_line(R"(\ \ )"), Words{"  "}) << "two escaped blanks are one word";
}

TEST(CompileCommandsSplit, QuotedPartsJoinTheWordTheyAreIn) {
    EXPECT_EQ(core::split_command_line(R"(a"b c"d'e f'g)"), Words{"ab cde fg"});
    EXPECT_EQ(core::split_command_line(R"(-I"my dir"/inc)"), Words{"-Imy dir/inc"});
}

TEST(CompileCommandsSplit, EmptyQuotesAreAnEmptyWord) {
    EXPECT_EQ(core::split_command_line(R"("" '')"), (Words{"", ""}));
    EXPECT_EQ(core::split_command_line(R"(a "" b)"), (Words{"a", "", "b"}));
}

TEST(CompileCommandsSplit, ABrokenLineStillGivesWhatIsThere) {
    EXPECT_EQ(core::split_command_line(R"(a "b c)"), (Words{"a", "b c"})) << "an unterminated quote runs to the end";
    EXPECT_EQ(core::split_command_line("'x y"), Words{"x y"});
    EXPECT_EQ(core::split_command_line("a\\"), Words{"a\\"}) << "a backslash with nothing after it stays";
    EXPECT_EQ(core::split_command_line("a \\"), (Words{"a", "\\"}));
    EXPECT_EQ(core::split_command_line(R"("a\)"), Words{"a\\"}) << "also inside double quotes";
}

// ---- include_dirs_of_compile_commands ----------------------------------------------------------------------

struct Read {
    std::optional<Words> dirs;
    core::Diagnostics    diagnostics;
};

Read read(const std::string& json) {
    Read r;
    r.dirs = core::include_dirs_of_compile_commands(json, "cc.json", r.diagnostics);
    return r;
}

TEST(CompileCommandsDirs, TheThreeFlagsAndBothSpellingsOfI) {
    const Read r = read(R"([{"directory":"/p/build","file":"x.cpp","arguments":
        ["c++","-I","/p/inc","-I/p/inc2","-isystem","/opt/sys","-iquote","/p/q","-c","x.cpp"]}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, (Words{"/p/inc", "/p/inc2", "/opt/sys", "/p/q"}));
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(CompileCommandsDirs, ACommandIsSplitLikeAShellWould) {
    const Read r = read(R"([{"directory":"/p","file":"x.cpp","command":"c++ -I/p/a -I \"/p/b c\" -isystem '/p/d e' -c x.cpp"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, (Words{"/p/a", "/p/b c", "/p/d e"}));
}

TEST(CompileCommandsDirs, ArgumentsWinOverCommand) {
    const Read r = read(R"([{"directory":"/p","arguments":["c++","-Ia"],"command":"c++ -Ib"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, Words{"/p/a"});
}

TEST(CompileCommandsDirs, ARelativeDirectoryIsJoinedToTheEntrysAndNormalised) {
    const Read r = read(R"([{"directory":"/p/build","command":"c++ -I../inc -I. -Isub/./x/ -I/abs/../abs2"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, (Words{"/p/inc", "/p/build", "/p/build/sub/x", "/abs2"}));
}

TEST(CompileCommandsDirs, WithoutAnAbsoluteDirectoryTheResultStaysRelativeForTheCallerToResolve) {
    EXPECT_EQ(*read(R"([{"command":"c++ -Iinc -I./other/"}])").dirs, (Words{"inc", "other"}));
    EXPECT_EQ(*read(R"([{"directory":".","command":"c++ -I in/include"}])").dirs, Words{"in/include"});
    EXPECT_EQ(*read(R"([{"directory":"build","command":"c++ -I../inc"}])").dirs, Words{"inc"});
}

TEST(CompileCommandsDirs, TheRootDirectoryKeepsItsSlash) {
    EXPECT_EQ(*read(R"([{"command":"c++ -I/"}])").dirs, Words{"/"});
    EXPECT_EQ(*read(R"([{"directory":"/p","command":"c++ -I/ -I../.."}])").dirs, (Words{"/"})) << "and so does what normalises to it";
}

TEST(CompileCommandsDirs, EachDirectoryOnceInTheOrderItFirstAppears) {
    const Read r = read(R"([{"command":"c++ -Ia -Ib"},
                           {"command":"c++ -Ib -Ic -Ia"},
                           {"directory":"/p","command":"c++ -I/p/x -I/p/y/../x -Id"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, (Words{"a", "b", "c", "/p/x", "/p/d"}));
}

TEST(CompileCommandsDirs, OnlyTheFlagsAreRead) {
    // The first word is the program, whatever it looks like; -DNAME=-Ix is a definition; a header given to
    // -include is not a directory; a flag with nothing after it, or with an empty word, names nothing; the
    // joined spellings of -isystem and -iquote are not read.
    const Read r = read(R"([{"arguments":["-Inot-a-dir","-DNAME=-Ix","-Wall","-include","inc.h","-isystem/x","-iquote/y","-I","","-Ireal"]},
                           {"arguments":["c++","-isystem"]},
                           {"arguments":["c++","-I"]}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, Words{"real"});
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(CompileCommandsDirs, NothingToTakeIsNotAnError) {
    for (const char* json : {"[]", R"([{"arguments":[]}])", R"([{"command":""}])", R"([{"command":"c++ -c x.cpp"}])"}) {
        const Read r = read(json);
        ASSERT_TRUE(r.dirs) << json;
        EXPECT_TRUE(r.dirs->empty()) << json;
        EXPECT_TRUE(r.diagnostics.empty()) << json;
    }
}

TEST(CompileCommandsDirs, TheFileOfAnEntryIsNotLookedAt) {
    const Read r = read(R"([{"directory":"/p","file":"/elsewhere/other.cpp","command":"c++ -Ia"},
                           {"directory":"/q","command":"c++ -Ib"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, (Words{"/p/a", "/q/b"}));
}

// ---- what is not a database --------------------------------------------------------------------------------

TEST(CompileCommandsErrors, InvalidJsonIsOneLocatedError) {
    const Read r = read(R"([{"command": )");
    EXPECT_FALSE(r.dirs);
    ASSERT_EQ(r.diagnostics.all().size(), 1u);
    const core::Diagnostic& d = r.diagnostics.all()[0];
    EXPECT_EQ(d.severity, core::Severity::Error);
    EXPECT_EQ(d.code, core::code::config);
    EXPECT_EQ(d.message.rfind("invalid JSON: ", 0), 0u) << d.message;
    EXPECT_EQ(d.where.file, "cc.json");
}

TEST(CompileCommandsErrors, CommentsAreNotJson) {
    EXPECT_FALSE(read("[ // none\n]").dirs);
}

TEST(CompileCommandsErrors, ADatabaseIsAnArray) {
    for (const char* json : {"{}", R"({"command":"c++ -Ia"})", "\"x\"", "3", "null"}) {
        const Read r = read(json);
        EXPECT_FALSE(r.dirs) << json;
        ASSERT_EQ(r.diagnostics.all().size(), 1u) << json;
        EXPECT_EQ(r.diagnostics.all()[0].severity, core::Severity::Error);
        EXPECT_EQ(r.diagnostics.all()[0].message, "not a compilation database: a JSON array of entries is expected") << json;
        EXPECT_EQ(r.diagnostics.all()[0].where.file, "cc.json");
    }
}

TEST(CompileCommandsErrors, AnEntryThatCannotBeReadIsSkippedWithAWarningAndTheRestIsUsed) {
    const Read r = read(R"([1,
                           {"directory":"/p"},
                           {"directory":5,"arguments":["c++","-Ino"]},
                           {"arguments":"c++ -Ino"},
                           {"arguments":["c++",1,"-Ino"]},
                           {"command":7},
                           {"directory":"/p","command":"c++ -Iyes"}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, Words{"/p/yes"});
    EXPECT_FALSE(r.diagnostics.has_errors());

    const std::vector<std::string> why = {
        "is not an object",
        "has neither 'arguments' nor 'command'",
        "has a 'directory' that is not a string",
        "has an 'arguments' that is not an array",
        "has an argument that is not a string",
        "has a 'command' that is not a string",
    };
    ASSERT_EQ(r.diagnostics.all().size(), why.size());
    for (std::size_t i = 0; i < why.size(); ++i) {
        const core::Diagnostic& d = r.diagnostics.all()[i];
        EXPECT_EQ(d.severity, core::Severity::Warning) << i;
        EXPECT_EQ(d.code, core::code::config) << i;
        EXPECT_EQ(d.message, "entry " + std::to_string(i + 1) + " of the compilation database " + why[i] + "; it is skipped") << i;
        EXPECT_EQ(d.where.file, "cc.json") << i;
    }
}

TEST(CompileCommandsErrors, WhenThereAreArgumentsTheCommandIsNotLookedAt) {
    const Read r = read(R"([{"arguments":["c++","-Ia"],"command":7}])");
    ASSERT_TRUE(r.dirs);
    EXPECT_EQ(*r.dirs, Words{"a"});
    EXPECT_TRUE(r.diagnostics.empty());
}

}  // namespace
