// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/compile_commands.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

namespace mt::core {

namespace {

using json = nlohmann::json;

bool is_blank(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// `directory` + `dir`, normalised, as a string with '/' and no trailing one.
std::string joined(const std::string& directory, const std::string& dir) {
    std::string out = (std::filesystem::path(directory) / dir).lexically_normal().generic_string();
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

// The directories one command line names, in order. The first word is the
// compiler and is skipped.
std::vector<std::string> directories_of(const std::vector<std::string>& words) {
    std::vector<std::string> dirs;
    for (std::size_t i = 1; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w == "-I" || w == "-isystem" || w == "-iquote") {
            if (i + 1 < words.size()) dirs.push_back(words[++i]);
        } else if (w.size() > 2 && w.compare(0, 2, "-I") == 0) {
            dirs.push_back(w.substr(2));
        }
    }
    return dirs;
}

class Reader {
public:
    Reader(const std::string& origin, Diagnostics& diagnostics) : origin_(origin), diagnostics_(diagnostics) {}

    void fail(const std::string& message) { diagnostics_.error(code::config, message, where()); }

    // Entry `n` (1-based) is left out.
    void skip(std::size_t n, const std::string& why) {
        diagnostics_.warning(code::config, "entry " + std::to_string(n) + " of the compilation database " + why + "; it is skipped",
                             where());
    }

    // The words of an entry, or nothing if it has no usable command (already reported).
    std::optional<std::vector<std::string>> words_of(const json& entry, std::size_t n) {
        const auto arguments = entry.find("arguments");
        if (arguments != entry.end()) {
            if (!arguments->is_array()) {
                skip(n, "has an 'arguments' that is not an array");
                return std::nullopt;
            }
            std::vector<std::string> words;
            for (const json& word : *arguments) {
                if (!word.is_string()) {
                    skip(n, "has an argument that is not a string");
                    return std::nullopt;
                }
                words.push_back(word.get<std::string>());
            }
            return words;
        }
        const auto command = entry.find("command");
        if (command == entry.end()) {
            skip(n, "has neither 'arguments' nor 'command'");
            return std::nullopt;
        }
        if (!command->is_string()) {
            skip(n, "has a 'command' that is not a string");
            return std::nullopt;
        }
        return split_command_line(command->get<std::string>());
    }

private:
    ir::SourceLoc where() const {
        ir::SourceLoc loc;
        loc.file = origin_;
        return loc;
    }

    const std::string& origin_;
    Diagnostics&       diagnostics_;
};

}  // namespace

std::vector<std::string> split_command_line(std::string_view command) {
    std::vector<std::string> words;
    std::string word;
    bool in_word = false;   // `""` is a word although nothing was added to it
    char quote = '\0';
    for (std::size_t i = 0; i < command.size(); ++i) {
        const char c = command[i];
        const bool has_next = i + 1 < command.size();
        if (quote == '\'') {
            if (c == '\'') quote = '\0';
            else word += c;
        } else if (quote == '"') {
            if (c == '"') {
                quote = '\0';
            } else if (c == '\\' && has_next && std::string_view("\"\\$`").find(command[i + 1]) != std::string_view::npos) {
                word += command[++i];
            } else {
                word += c;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            in_word = true;
        } else if (c == '\\' && has_next) {
            word += command[++i];
            in_word = true;
        } else if (is_blank(c)) {
            if (in_word) words.push_back(std::move(word));
            word.clear();
            in_word = false;
        } else {
            word += c;
            in_word = true;
        }
    }
    if (in_word) words.push_back(std::move(word));
    return words;
}

std::optional<std::vector<std::string>> include_dirs_of_compile_commands(std::string_view text, const std::string& origin,
                                                                         Diagnostics& diagnostics) {
    Reader reader(origin, diagnostics);

    json root;
    try {
        root = json::parse(std::string(text));
    } catch (const json::parse_error& e) {
        reader.fail(std::string("invalid JSON: ") + e.what());
        return std::nullopt;
    }
    if (!root.is_array()) {
        reader.fail("not a compilation database: a JSON array of entries is expected");
        return std::nullopt;
    }

    std::vector<std::string> dirs;
    std::set<std::string> seen;
    std::size_t n = 0;
    for (const json& entry : root) {
        ++n;
        if (!entry.is_object()) {
            reader.skip(n, "is not an object");
            continue;
        }
        std::string directory;
        const auto where = entry.find("directory");
        if (where != entry.end()) {
            if (!where->is_string()) {
                reader.skip(n, "has a 'directory' that is not a string");
                continue;
            }
            directory = where->get<std::string>();
        }
        const std::optional<std::vector<std::string>> words = reader.words_of(entry, n);
        if (!words) continue;

        for (const std::string& dir : directories_of(*words)) {
            if (dir.empty()) continue;
            std::string full = joined(directory, dir);
            if (seen.insert(full).second) dirs.push_back(std::move(full));
        }
    }
    return dirs;
}

}  // namespace mt::core
