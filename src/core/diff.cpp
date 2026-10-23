// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/diff.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mt::core {

namespace {

using Lines = std::vector<std::string_view>;

// Each line keeps its terminating '\n'. The last one may lack it, which is
// what makes "no newline at end of file" a difference like any other.
Lines split_lines(std::string_view text) {
    Lines lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t eol = text.find('\n', start);
        const std::size_t end = eol == std::string_view::npos ? text.size() : eol + 1;
        lines.push_back(text.substr(start, end - start));
        start = end;
    }
    return lines;
}

enum class Kind { Equal, Delete, Insert };

struct Op {
    Kind             kind;
    std::string_view text;
};

// Myers' greedy algorithm over a[0..n) and b[0..m). Appends the edit script,
// in order, to `out`. Returns false (appending nothing) if more than
// `limit` edits would be needed.
bool myers(const Lines& a, const Lines& b, unsigned limit, std::vector<Op>& out) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    const int max_d = std::min(n + m, static_cast<int>(limit));

    // v[k] = furthest x reached on diagonal k. Stored with an offset so that
    // k = -max_d-1 .. max_d+1 are valid indices.
    const int offset = max_d + 1;
    std::vector<int> v(static_cast<std::size_t>(2 * max_d + 3), 0);
    auto at = [&](int k) -> int& { return v[static_cast<std::size_t>(k + offset)]; };

    // trace[d] is the window of v, k = -(d+1) .. d+1, as it stood before step d.
    std::vector<std::vector<int>> trace;
    bool found = false;
    for (int d = 0; d <= max_d && !found; ++d) {
        std::vector<int> window(static_cast<std::size_t>(2 * d + 3));
        for (int k = -(d + 1); k <= d + 1; ++k) {
            window[static_cast<std::size_t>(k + d + 1)] = at(k);
        }
        trace.push_back(std::move(window));

        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? at(k + 1) : at(k - 1) + 1;
            int y = x - k;
            while (x < n && y < m && a[static_cast<std::size_t>(x)] == b[static_cast<std::size_t>(y)]) {
                ++x;
                ++y;
            }
            at(k) = x;
            if (x >= n && y >= m) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }

    // Walk back from (n, m) to (0, 0), collecting the script in reverse.
    std::vector<Op> reversed;
    int x = n;
    int y = m;
    for (int d = static_cast<int>(trace.size()) - 1; d >= 0; --d) {
        const std::vector<int>& w = trace[static_cast<std::size_t>(d)];
        auto prev = [&](int k) { return w[static_cast<std::size_t>(k + d + 1)]; };
        const int k = x - y;
        const int prev_k = (k == -d || (k != d && prev(k - 1) < prev(k + 1))) ? k + 1 : k - 1;
        const int prev_x = prev(prev_k);
        const int prev_y = prev_x - prev_k;

        while (x > prev_x && y > prev_y) {
            reversed.push_back({Kind::Equal, a[static_cast<std::size_t>(x - 1)]});
            --x;
            --y;
        }
        if (d > 0) {
            if (x == prev_x) {
                reversed.push_back({Kind::Insert, b[static_cast<std::size_t>(prev_y)]});
            } else {
                reversed.push_back({Kind::Delete, a[static_cast<std::size_t>(prev_x)]});
            }
        }
        x = prev_x;
        y = prev_y;
    }
    out.insert(out.end(), reversed.rbegin(), reversed.rend());
    return true;
}

// The whole edit script: common head, differing middle, common tail.
std::vector<Op> edit_script(const Lines& a, const Lines& b) {
    std::size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) {
        ++head;
    }
    std::size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head &&
           a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) {
        ++tail;
    }

    const Lines mid_a(a.begin() + static_cast<long>(head), a.end() - static_cast<long>(tail));
    const Lines mid_b(b.begin() + static_cast<long>(head), b.end() - static_cast<long>(tail));

    std::vector<Op> ops;
    ops.reserve(a.size() + b.size());
    for (std::size_t i = 0; i < head; ++i) {
        ops.push_back({Kind::Equal, a[i]});
    }
    if (!mid_a.empty() || !mid_b.empty()) {
        if (!myers(mid_a, mid_b, kMaxEdits, ops)) {
            for (std::string_view line : mid_a) {
                ops.push_back({Kind::Delete, line});
            }
            for (std::string_view line : mid_b) {
                ops.push_back({Kind::Insert, line});
            }
        }
    }
    for (std::size_t i = a.size() - tail; i < a.size(); ++i) {
        ops.push_back({Kind::Equal, a[i]});
    }
    return ops;
}

// "3", "3,7" or "2,0": git's spelling of a range of `count` lines whose first
// is at 0-based index `first`.
void append_range(std::string& out, std::size_t first, std::size_t count) {
    out += std::to_string(count == 0 ? first : first + 1);
    if (count != 1) {
        out += ',';
        out += std::to_string(count);
    }
}

void append_line(std::string& out, char prefix, std::string_view text) {
    out += prefix;
    out += text;
    if (text.empty() || text.back() != '\n') {
        out += "\n\\ No newline at end of file\n";
    }
}

}  // namespace

std::string unified_diff(std::string_view path, const std::optional<std::string>& old_text,
                         std::string_view new_text, unsigned context) {
    if (old_text && *old_text == new_text) {
        return {};
    }

    const std::string_view old_view = old_text ? std::string_view(*old_text) : std::string_view();
    const Lines a = split_lines(old_view);
    const Lines b = split_lines(new_text);

    std::string out;
    if (old_text) {
        out += "--- a/";
        out += path;
    } else {
        out += "--- /dev/null";
    }
    out += "\n+++ b/";
    out += path;
    out += '\n';

    const std::vector<Op> ops = edit_script(a, b);

    // old_before[i] / new_before[i]: lines of each side that precede op i.
    std::vector<std::size_t> old_before(ops.size() + 1, 0);
    std::vector<std::size_t> new_before(ops.size() + 1, 0);
    for (std::size_t i = 0; i < ops.size(); ++i) {
        old_before[i + 1] = old_before[i] + (ops[i].kind != Kind::Insert ? 1 : 0);
        new_before[i + 1] = new_before[i] + (ops[i].kind != Kind::Delete ? 1 : 0);
    }

    const std::size_t ctx = context;
    std::size_t i = 0;
    while (i < ops.size()) {
        if (ops[i].kind == Kind::Equal) {
            ++i;
            continue;
        }

        // A hunk runs from `context` lines before its first change to
        // `context` lines after its last. Two changes closer than twice the
        // context share one, otherwise their trailing and leading context
        // would overlap.
        const std::size_t begin = i >= ctx ? i - ctx : 0;
        std::size_t last_change = i;
        std::size_t p = i;
        while (p < ops.size()) {
            if (ops[p].kind != Kind::Equal) {
                last_change = p;
                ++p;
                continue;
            }
            std::size_t run_end = p;
            while (run_end < ops.size() && ops[run_end].kind == Kind::Equal) {
                ++run_end;
            }
            if (run_end == ops.size() || run_end - p > 2 * ctx) {
                break;
            }
            p = run_end;
        }
        const std::size_t end = std::min(ops.size(), last_change + 1 + ctx);

        out += "@@ -";
        append_range(out, old_before[begin], old_before[end] - old_before[begin]);
        out += " +";
        append_range(out, new_before[begin], new_before[end] - new_before[begin]);
        out += " @@\n";
        for (std::size_t k = begin; k < end; ++k) {
            const char prefix = ops[k].kind == Kind::Equal ? ' ' : ops[k].kind == Kind::Delete ? '-' : '+';
            append_line(out, prefix, ops[k].text);
        }
        i = end;
    }
    return out;
}

}  // namespace mt::core
