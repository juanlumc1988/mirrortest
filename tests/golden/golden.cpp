// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "golden.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

#include "mirrortest/core/diff.h"
#include "test_support.h"

namespace mt::golden {

namespace fs = std::filesystem;

namespace {

const char* const kKnownEntries[] = {"args.txt",   "input",     "existing",      "expected",
                                     "stdout.txt", "stderr.txt", "exit_code.txt", "notes.md"};

std::string read_all(const fs::path& path) { return mt::test::read_file(path); }

std::string read_or_empty(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) ? read_all(path) : std::string();
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

std::map<std::string, std::string> collect_tree(const fs::path& root) {
    std::map<std::string, std::string> tree;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return tree;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            tree[fs::relative(entry.path(), root).generic_string()] = read_all(entry.path());
        }
    }
    return tree;
}

void copy_contents(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::is_directory(from, ec)) {
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
}

std::string diff_message(const std::string& label, const std::string& expected, const std::string& actual) {
    return label + " differs (a = expected, b = actual):\n" +
           core::unified_diff(label, std::optional<std::string>(expected), actual);
}

// Rules for text this tool writes. Streams are exempt from the trailing
// whitespace rule: a unified diff prints an empty context line as " ".
void check_text(const std::string& label, const std::string& text, bool is_file,
                const std::vector<std::string>& forbidden, std::vector<std::string>& problems) {
    if (text.find('\r') != std::string::npos) {
        problems.push_back(label + " contains a carriage return; generated text is LF");
    }
    if (!text.empty() && text.back() != '\n') {
        problems.push_back(label + " does not end with a newline");
    }
    if (is_file) {
        std::istringstream lines(text);
        std::string line;
        for (unsigned n = 1; std::getline(lines, line); ++n) {
            if (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                problems.push_back(label + " line " + std::to_string(n) + " has trailing whitespace");
            }
        }
    }
    for (const std::string& path : forbidden) {
        if (!path.empty() && text.find(path) != std::string::npos) {
            problems.push_back(label + " contains the path '" + path +
                               "'; output must not depend on where it ran");
        }
    }
}

void write_or_remove(const fs::path& path, const std::string& content) {
    std::error_code ec;
    if (content.empty()) {
        fs::remove(path, ec);
    } else {
        mt::test::write_file(path, content);
    }
}

}  // namespace

std::vector<Case> discover(const fs::path& root) {
    std::vector<Case> cases;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return cases;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().filename() != "args.txt") {
            continue;
        }
        const fs::path dir = entry.path().parent_path();
        const std::string name = fs::relative(dir, root).generic_string();

        // An args.txt inside a case's own input is just input.
        bool nested = false;
        for (const auto& part : fs::relative(dir, root)) {
            const std::string p = part.string();
            nested = nested || p == "input" || p == "existing" || p == "expected";
        }
        if (!nested) {
            cases.push_back({name, dir});
        }
    }
    std::sort(cases.begin(), cases.end(),
              [](const Case& a, const Case& b) { return a.name < b.name; });
    return cases;
}

bool load_spec(const fs::path& dir, Spec& spec, std::vector<std::string>& problems) {
    const std::size_t before = problems.size();
    spec = Spec();
    spec.dir = dir;

    const fs::path args_file = dir / "args.txt";
    std::error_code ec;
    if (!fs::is_regular_file(args_file, ec)) {
        problems.push_back("args.txt is missing");
        return false;
    }

    std::string text = read_all(args_file);
    if (text.find('\r') != std::string::npos) {
        problems.push_back("args.txt must use LF line endings");
    }
    // An empty file is "no arguments" (a legitimate case: the bare command).
    // Otherwise every line is one argument, and an empty line is a mistake,
    // not an empty argument.
    if (!text.empty()) {
        if (text.back() == '\n') {
            text.pop_back();
        }
        std::size_t start = 0;
        for (unsigned n = 1;; ++n) {
            const std::size_t eol = text.find('\n', start);
            std::string line = text.substr(start, eol == std::string::npos ? std::string::npos : eol - start);
            if (line.empty()) {
                problems.push_back("args.txt line " + std::to_string(n) + " is empty");
            } else {
                replace_all(line, "${IN}", "in");
                replace_all(line, "${OUT}", "out");
                spec.args.push_back(std::move(line));
            }
            if (eol == std::string::npos) {
                break;
            }
            start = eol + 1;
        }
    }

    const fs::path code_file = dir / "exit_code.txt";
    if (fs::is_regular_file(code_file, ec)) {
        std::string code = read_all(code_file);
        while (!code.empty() && (code.back() == '\n' || code.back() == ' ')) code.pop_back();
        const bool numeric = !code.empty() && code.size() <= 3 &&
                             std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (numeric) {
            spec.expected_code = std::stoi(code);
        } else {
            problems.push_back("exit_code.txt must hold a number (got '" + code + "')");
        }
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (std::find(std::begin(kKnownEntries), std::end(kKnownEntries), name) == std::end(kKnownEntries)) {
            problems.push_back("unknown entry '" + name +
                               "' (known: args.txt, input/, existing/, expected/, stdout.txt, "
                               "stderr.txt, exit_code.txt, notes.md)");
        }
    }
    for (const char* tree : {"input", "existing", "expected"}) {
        if (fs::exists(dir / tree, ec) && !fs::is_directory(dir / tree, ec)) {
            problems.push_back(std::string(tree) + " must be a directory");
        }
    }
    return problems.size() == before;
}

Result execute(const Spec& spec, const CliFn& cli) {
    mt::test::TempDir work;
    fs::create_directories(work.path() / "in");
    fs::create_directories(work.path() / "out");
    copy_contents(spec.dir / "input", work.path() / "in");
    copy_contents(spec.dir / "existing", work.path() / "out");

    std::ostringstream out;
    std::ostringstream err;
    cli::Env env;
    env.cwd = work.path();
    env.out = &out;
    env.err = &err;

    Result result;
    result.code = cli(spec.args, env);
    result.out = out.str();
    result.err = err.str();
    result.tree = collect_tree(work.path() / "out");
    result.work_dir = work.path().string();
    return result;
}

std::vector<std::string> check_conventions(const Result& result, const std::vector<std::string>& forbidden) {
    std::vector<std::string> problems;
    check_text("stdout", result.out, false, forbidden, problems);
    check_text("stderr", result.err, false, forbidden, problems);
    for (const auto& file : result.tree) {
        check_text("output file '" + file.first + "'", file.second, true, forbidden, problems);
    }
    return problems;
}

std::vector<std::string> compare(const Spec& spec, const Result& result) {
    std::vector<std::string> problems;

    if (result.code != spec.expected_code) {
        problems.push_back("exit code: expected " + std::to_string(spec.expected_code) + ", got " +
                           std::to_string(result.code));
    }
    const std::string want_out = read_or_empty(spec.dir / "stdout.txt");
    if (want_out != result.out) {
        problems.push_back(diff_message("stdout", want_out, result.out));
    }
    const std::string want_err = read_or_empty(spec.dir / "stderr.txt");
    if (want_err != result.err) {
        problems.push_back(diff_message("stderr", want_err, result.err));
    }

    const std::map<std::string, std::string> expected = collect_tree(spec.dir / "expected");
    for (const auto& file : expected) {
        const auto found = result.tree.find(file.first);
        if (found == result.tree.end()) {
            problems.push_back("missing output file '" + file.first + "'");
        } else if (found->second != file.second) {
            problems.push_back(diff_message("output file '" + file.first + "'", file.second, found->second));
        }
    }
    for (const auto& file : result.tree) {
        if (expected.count(file.first) == 0) {
            problems.push_back("unexpected output file '" + file.first + "'");
        }
    }
    return problems;
}

void update(const Spec& spec, const Result& result) {
    write_or_remove(spec.dir / "stdout.txt", result.out);
    write_or_remove(spec.dir / "stderr.txt", result.err);
    write_or_remove(spec.dir / "exit_code.txt",
                    result.code == 0 ? std::string() : std::to_string(result.code) + "\n");

    std::error_code ec;
    fs::remove_all(spec.dir / "expected", ec);
    for (const auto& file : result.tree) {
        mt::test::write_file(spec.dir / "expected" / file.first, file.second);
    }
}

std::vector<std::string> check_case(const Case& test_case, const CliFn& cli, bool update_mode) {
    Spec spec;
    std::vector<std::string> problems;
    if (!load_spec(test_case.dir, spec, problems)) {
        return problems;
    }

    const Result first = execute(spec, cli);
    const Result second = execute(spec, cli);

    // Same input, same bytes, whichever directory it ran in.
    if (first.code != second.code) problems.push_back("not deterministic: the exit code changed between runs");
    if (first.out != second.out) problems.push_back("not deterministic: stdout changed between runs");
    if (first.err != second.err) problems.push_back("not deterministic: stderr changed between runs");
    if (first.tree != second.tree) problems.push_back("not deterministic: the output files changed between runs");

    const std::vector<std::string> forbidden = {first.work_dir, second.work_dir, test_case.dir.string()};
    for (const std::string& p : check_conventions(first, forbidden)) {
        problems.push_back(p);
    }

    if (update_mode) {
        if (problems.empty()) {
            update(spec, first);
        }
        return problems;
    }
    for (const std::string& p : compare(spec, first)) {
        problems.push_back(p);
    }
    return problems;
}

}  // namespace mt::golden
