// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/merge.h"

#include <algorithm>
#include <map>

#include "mirrortest/core/regions.h"

namespace mt::core {

namespace {

std::string trimmed(const std::string& line) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    std::size_t begin = 0;
    std::size_t end = line.size();
    while (begin < end && is_space(line[begin])) ++begin;
    while (end > begin && is_space(line[end - 1])) --end;
    return line.substr(begin, end - begin);
}

std::string leading_whitespace(const std::string& line) {
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return line.substr(0, n);
}

enum class MarkerKind { None, Begin, End };

struct Marker {
    MarkerKind  kind = MarkerKind::None;
    std::string label;
};

Marker parse_marker(const std::string& line) {
    const std::string text = trimmed(line);
    for (const MarkerKind kind : {MarkerKind::Begin, MarkerKind::End}) {
        const std::string prefix = kind == MarkerKind::Begin ? "// <mt:begin " : "// <mt:end ";
        if (text.size() <= prefix.size() + 1 || text.compare(0, prefix.size(), prefix) != 0 || text.back() != '>') continue;
        const std::string label = text.substr(prefix.size(), text.size() - prefix.size() - 1);
        if (label.empty() || label.find_first_of(" \t<>") != std::string::npos) continue;
        return {kind, label};
    }
    return {};
}

ir::SourceLoc at(const std::string& file, std::size_t line_index) {
    ir::SourceLoc where;
    where.file = file;
    where.line = static_cast<unsigned>(line_index + 1);
    where.column = 1;
    return where;
}

}  // namespace

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline + 1 - start));
        start = newline + 1;
    }
    return lines;
}

std::string line_ending_of(const std::string& text) {
    const std::size_t newline = text.find('\n');
    if (newline != std::string::npos && newline > 0 && text[newline - 1] == '\r') return "\r\n";
    return "\n";
}

std::optional<std::vector<Region>> find_regions(const std::string& text, const std::string& file, Diagnostics& diagnostics) {
    const std::vector<std::string> lines = split_lines(text);
    std::vector<Region> regions;
    bool bad = false;
    const auto error = [&](std::size_t index, const std::string& message) {
        diagnostics.error(code::merge_region, message, at(file, index));
        bad = true;
    };

    // A begin that was refused has an end that must not be reported as well:
    // one mistake, one message.
    std::map<std::string, int> refused;

    const Region* open = nullptr;
    Region current;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Marker marker = parse_marker(lines[i]);
        if (marker.kind == MarkerKind::Begin) {
            if (open != nullptr) {
                error(i, "region '" + marker.label + "' begins inside region '" + current.label + "' (line " +
                             std::to_string(current.begin + 1) + "); regions do not nest");
                ++refused[marker.label];
                continue;
            }
            if (const Region* seen = find_region(regions, marker.label)) {
                error(i, "region '" + marker.label + "' appears twice (first at line " + std::to_string(seen->begin + 1) + ")");
                ++refused[marker.label];
                continue;
            }
            current = Region();
            current.label = marker.label;
            current.begin = i;
            open = &current;
        } else if (marker.kind == MarkerKind::End) {
            int& pending = refused[marker.label];
            if (pending > 0 && (open == nullptr || marker.label != current.label)) {
                --pending;
            } else if (open == nullptr) {
                error(i, "region '" + marker.label + "' ends but never began");
            } else if (marker.label != current.label) {
                error(i, "region '" + current.label + "' (line " + std::to_string(current.begin + 1) + ") is closed by the end of '" +
                             marker.label + "'");
                open = nullptr;
            } else {
                current.end = i;
                current.indent = leading_whitespace(lines[i]);
                regions.push_back(current);
                open = nullptr;
            }
        }
    }
    if (open != nullptr) error(current.begin, "region '" + current.label + "' is never closed");
    if (bad) return std::nullopt;
    return regions;
}

const Region* find_region(const std::vector<Region>& regions, const std::string& label) {
    for (const Region& r : regions) {
        if (r.label == label) return &r;
    }
    return nullptr;
}

std::vector<std::string> region_body(const std::string& text, const Region& region) {
    const std::vector<std::string> lines = split_lines(text);
    std::vector<std::string> body;
    for (std::size_t i = region.begin + 1; i < region.end && i < lines.size(); ++i) {
        std::string line = lines[i];
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        body.push_back(std::move(line));
    }
    return body;
}

std::string insert_before_end(const std::string& text, const Region& region, const std::vector<std::string>& lines) {
    std::vector<std::string> all = split_lines(text);
    if (region.end >= all.size()) return text;   // not a region of this text

    const std::string eol = line_ending_of(text);
    std::string inserted;
    for (const std::string& line : lines) {
        if (!line.empty()) inserted += region.indent + line;
        inserted += eol;
    }
    // The end marker may be the last line of a text with no final newline; the
    // inserted lines then still end in a terminator, so the marker stays on its own line.
    all.insert(all.begin() + static_cast<std::ptrdiff_t>(region.end), inserted);

    std::string out;
    for (const std::string& line : all) out += line;
    return out;
}

}  // namespace mt::core
