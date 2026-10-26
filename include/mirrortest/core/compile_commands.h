// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The include directories of a compilation database (`compile_commands.json`,
// the file CMake writes with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON). Only they are
// taken: mirrortest does not compile anything, it needs to know where the
// headers a test includes are (docs/design.md, section 13.3).
//
// Pure text in, text out: reading the file is the caller's business.

#ifndef MT_CORE_COMPILE_COMMANDS_H
#define MT_CORE_COMPILE_COMMANDS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/core/diagnostics.h"

namespace mt::core {

// The words of a command line as a POSIX shell would split them. Blanks
// separate words; 'single quotes' keep everything; inside "double quotes" a
// backslash escapes only `"`, `\`, `$` and the backtick; outside quotes it
// escapes any character. Nothing is expanded. An unterminated quote runs to the
// end, and a backslash at the very end stays. `a"b c"d` is one word, `""` an
// empty one.
//
//   (R"(c++ -I "my dir" -DX=\"y\")")  -> {"c++", "-I", "my dir", "-DX=\"y\""}
std::vector<std::string> split_command_line(std::string_view command);

// The directories named by -I, -isystem and -iquote in the entries of a
// compilation database, in the order they first appear, each once.
//
//   * An entry lists its `arguments`, or else its `command` (split as above);
//     when it has both, `arguments` is used, as the clang tools do.
//   * `-I dir` and `-Idir` are both read; `-isystem dir` and `-iquote dir` only
//     with the space (the joined spellings are not the same flag everywhere).
//     Nothing else is looked at, so a `-DNAME=-Ix` is not an include directory.
//   * A relative directory is joined to the entry's `directory` (the directory
//     the compiler ran in) and the result is normalised lexically -- `..` and `.`
//     removed, no trailing slash. Without a `directory` it stays as written, and
//     so does a `directory` that is itself relative: the caller resolves it
//     against its own working directory.
//
// The entry's `file` is not looked at: a header is never in the database, and a
// test that is about to be written is not either. Every entry counts.
//
// `origin` names the file in diagnostics. A text that is not a JSON array of
// entries is an `mt-config` error (nullopt); an entry that cannot be read is an
// `mt-config` warning naming its position (1 is the first), and is skipped.
std::optional<std::vector<std::string>> include_dirs_of_compile_commands(std::string_view json, const std::string& origin,
                                                                         Diagnostics& diagnostics);

}  // namespace mt::core

#endif  // MT_CORE_COMPILE_COMMANDS_H
