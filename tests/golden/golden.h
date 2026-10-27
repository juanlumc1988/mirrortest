// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The golden-file runner (docs/design.md, section 16.2).
//
// A generator is only as trustworthy as its golden files: each case pins the
// exact bytes of what a command produces, so any change in output is a
// visible diff in review and never a silent drift. A case is a directory:
//
//   tests/golden/<group>/<case>/
//       args.txt         one command-line argument per line; ${IN} and ${OUT}
//                        become the relative paths "in" and "out"
//       input/           copied to <work>/in
//       existing/        optional: copied to <work>/out before the run, for
//                        cases about files that are already there
//       expected/        the tree that must exist under <work>/out afterwards
//       stdout.txt       expected stdout       (absent = must be empty)
//       stderr.txt       expected stderr       (absent = must be empty)
//       exit_code.txt    expected exit code    (absent = 0)
//       notes.md         free text: why the case exists. Ignored.
//       requires.txt     optional: the optional features of the build the case needs, one per line
//                        (yaml, libclang). Where one is missing the case is skipped, not failed.
//
// The command runs in-process with a fresh temporary directory as its working
// directory, so every path it prints is the relative one from args.txt and the
// goldens are identical wherever the repository is checked out.
//
// Each case is run twice, in two separate directories, and the results must
// be identical: that is the determinism guarantee. Then the output must obey
// the conventions every generated file obeys (LF, final newline, no trailing
// whitespace, no absolute paths), and then it must match the expectations.
//
// Goldens are updated by running with MT_UPDATE_GOLDEN=1 (or the
// `update-golden` target), never by hand. Updating refuses to bless output
// that breaks a convention.

#ifndef MT_TESTS_GOLDEN_H
#define MT_TESTS_GOLDEN_H

#include <filesystem>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "mirrortest/cli/run.h"

namespace mt::golden {

struct Case {
    std::string           name;   // "group/case", with '/' on every platform
    std::filesystem::path dir;
};

// How GoogleTest prints a Case in a test's name. Without it ctest lists every
// case as "GetParam() = 72-byte object <B0-B3 50-7E ...>".
inline void PrintTo(const Case& test_case, std::ostream* out) { *out << test_case.name; }

// Every directory under `root` that contains an args.txt, sorted by name.
std::vector<Case> discover(const std::filesystem::path& root);

// The command under test: mt::cli::run in production, a fake in the runner's
// own tests.
using CliFn = std::function<int(const std::vector<std::string>&, const cli::Env&)>;

struct Spec {
    std::filesystem::path    dir;
    std::vector<std::string> args;   // ${IN}/${OUT} already substituted
    int                      expected_code = 0;
};

struct Result {
    int         code = 0;
    std::string out;     // stdout
    std::string err;     // stderr
    std::map<std::string, std::string> tree;   // files under <work>/out, by relative path
    std::string work_dir;                      // where it ran (gone by the time you look)
};

// Reads and validates a case directory. Returns false, with the reasons in
// `problems`, if the layout is wrong: no args.txt, an entry the runner does
// not know (almost always a typo), an unparsable exit_code.txt, an empty
// argument.
bool load_spec(const std::filesystem::path& dir, Spec& spec, std::vector<std::string>& problems);

// Runs the case once, in a fresh directory.
Result execute(const Spec& spec, const CliFn& cli);

// Conventions the output must obey; one message per violation. `forbidden` are
// path strings that must appear nowhere in it.
std::vector<std::string> check_conventions(const Result& result,
                                           const std::vector<std::string>& forbidden);

// Differences between `result` and the case's expectations, as readable
// messages that include a unified diff. Empty = it matches.
std::vector<std::string> compare(const Spec& spec, const Result& result);

// Rewrites the case's expectation files from `result`. Files that are empty or
// zero are removed rather than written, so the layout stays minimal.
void update(const Spec& spec, const Result& result);

// The whole check for one case. Returns what is wrong, empty if the case
// passes (or, in update mode, was updated).
std::vector<std::string> check_case(const Case& test_case, const CliFn& cli, bool update_mode);

}  // namespace mt::golden

#endif  // MT_TESTS_GOLDEN_H
