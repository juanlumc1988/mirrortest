// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/diff.h"

using mt::core::unified_diff;

namespace {

std::optional<std::string> none;

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t eol = text.find('\n', start);
        const std::size_t end = eol == std::string::npos ? text.size() : eol + 1;
        lines.push_back(text.substr(start, end - start));
        start = end;
    }
    return lines;
}

// A reference implementation of `patch`, strict enough to catch a wrong hunk
// header, a wrong context line or a miscounted range. Returns nullopt, with
// the reason in `why`, if the diff does not apply to `old_text`.
std::optional<std::string> apply_diff(const std::string& old_text, const std::string& diff,
                                      std::string& why) {
    const std::vector<std::string> old_lines = lines_of(old_text);
    const std::vector<std::string> d = lines_of(diff);

    std::string result;
    std::size_t cursor = 0;   // next unconsumed old line
    std::size_t i = 0;
    while (i < d.size() && (d[i].rfind("--- ", 0) == 0 || d[i].rfind("+++ ", 0) == 0)) {
        ++i;
    }

    while (i < d.size()) {
        unsigned long a = 0, b = 1, c = 0, e = 1;
        const char* p = d[i].c_str();
        if (d[i].rfind("@@ -", 0) != 0) {
            why = "expected a hunk header, got: " + d[i];
            return std::nullopt;
        }
        char* q = nullptr;
        a = std::strtoul(p + 4, &q, 10);
        if (*q == ',') b = std::strtoul(q + 1, &q, 10);
        if (std::string(q).rfind(" +", 0) != 0) {
            why = "malformed header: " + d[i];
            return std::nullopt;
        }
        c = std::strtoul(q + 2, &q, 10);
        if (*q == ',') e = std::strtoul(q + 1, &q, 10);
        if (std::string(q) != " @@\n") {
            why = "malformed header tail: " + d[i];
            return std::nullopt;
        }
        ++i;
        (void)c;

        const std::size_t first = b == 0 ? a : a - 1;
        if (first < cursor || first > old_lines.size()) {
            why = "hunk starts out of order or out of range";
            return std::nullopt;
        }
        for (; cursor < first; ++cursor) result += old_lines[cursor];

        std::size_t seen_old = 0, seen_new = 0;
        while (i < d.size() && d[i].rfind("@@ ", 0) != 0) {
            std::string body = d[i].substr(1);
            const char prefix = d[i][0];
            ++i;
            // "\ No newline at end of file" removes the break from the line above.
            if (i < d.size() && d[i] == "\\ No newline at end of file\n") {
                if (body.empty() || body.back() != '\n') {
                    why = "marker after a line that already lacks its newline";
                    return std::nullopt;
                }
                body.pop_back();
                ++i;
            }
            if (prefix == ' ' || prefix == '-') {
                if (cursor >= old_lines.size() || old_lines[cursor] != body) {
                    why = std::string("context/deleted line does not match: '") + body + "'";
                    return std::nullopt;
                }
                ++cursor;
                ++seen_old;
            }
            if (prefix == ' ' || prefix == '+') {
                result += body;
                ++seen_new;
            }
            if (prefix != ' ' && prefix != '-' && prefix != '+') {
                why = "unknown line prefix";
                return std::nullopt;
            }
        }
        if (seen_old != b || seen_new != e) {
            why = "hunk counts disagree with its body";
            return std::nullopt;
        }
    }
    for (; cursor < old_lines.size(); ++cursor) result += old_lines[cursor];
    return result;
}

std::size_t changed_lines(const std::string& diff) {
    std::size_t n = 0;
    for (const std::string& l : lines_of(diff)) {
        if ((l[0] == '-' && l.rfind("--- ", 0) != 0) || (l[0] == '+' && l.rfind("+++ ", 0) != 0)) ++n;
    }
    return n;
}

std::size_t lcs_length(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::vector<std::size_t>> t(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
        }
    }
    return t[a.size()][b.size()];
}

std::string numbered(const char* prefix, int count, int changed_a = -1, int changed_b = -1) {
    std::string out;
    for (int i = 1; i <= count; ++i) {
        out += prefix + std::to_string(i);
        if (i == changed_a || i == changed_b) out += "_changed";
        out += '\n';
    }
    return out;
}

class Lcg {
public:
    explicit Lcg(unsigned seed) : state_(seed) {}
    unsigned next(unsigned bound) {
        state_ = state_ * 1664525u + 1013904223u;
        return (state_ >> 8) % bound;
    }

private:
    unsigned state_;
};

}  // namespace

// ---- exact output -------------------------------------------------------------------

TEST(UnifiedDiff, IdenticalTextIsEmpty) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb\n"), "a\nb\n"), "");
    EXPECT_EQ(unified_diff("f", std::string(""), ""), "");
}

TEST(UnifiedDiff, ANewFileIsComparedWithDevNull) {
    EXPECT_EQ(unified_diff("src/foo.h", none, "a\nb\n"),
              "--- /dev/null\n"
              "+++ b/src/foo.h\n"
              "@@ -0,0 +1,2 @@\n"
              "+a\n"
              "+b\n");
}

TEST(UnifiedDiff, ANewEmptyFileHasHeadersAndNoHunks) {
    EXPECT_EQ(unified_diff("f", none, ""), "--- /dev/null\n+++ b/f\n");
}

TEST(UnifiedDiff, AnExistingEmptyFileIsNotNew) {
    EXPECT_EQ(unified_diff("f", std::string(""), "a\n"),
              "--- a/f\n+++ b/f\n@@ -0,0 +1 @@\n+a\n");
}

TEST(UnifiedDiff, EmptyingAFile) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb\n"), ""),
              "--- a/f\n+++ b/f\n@@ -1,2 +0,0 @@\n-a\n-b\n");
}

TEST(UnifiedDiff, OneLineChangeInTheMiddleShowsThreeLinesOfContext) {
    EXPECT_EQ(unified_diff("f", std::string("1\n2\n3\n4\n5\n6\n7\n8\n9\n"), "1\n2\n3\n4\nfive\n6\n7\n8\n9\n"),
              "--- a/f\n"
              "+++ b/f\n"
              "@@ -2,7 +2,7 @@\n"
              " 2\n"
              " 3\n"
              " 4\n"
              "-5\n"
              "+five\n"
              " 6\n"
              " 7\n"
              " 8\n");
}

TEST(UnifiedDiff, ChangeAtTheStartAndEndTruncatesTheContext) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb\nc\nd\ne\n"), "A\nb\nc\nd\ne\n"),
              "--- a/f\n+++ b/f\n@@ -1,4 +1,4 @@\n-a\n+A\n b\n c\n d\n");
    EXPECT_EQ(unified_diff("f", std::string("a\nb\nc\nd\ne\n"), "a\nb\nc\nd\nE\n"),
              "--- a/f\n+++ b/f\n@@ -2,4 +2,4 @@\n b\n c\n d\n-e\n+E\n");
}

TEST(UnifiedDiff, AOneLineRangeOmitsItsCount) {
    EXPECT_EQ(unified_diff("f", std::string("a\n"), "b\n"), "--- a/f\n+++ b/f\n@@ -1 +1 @@\n-a\n+b\n");
}

TEST(UnifiedDiff, InsertionsAtBothEnds) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb\n"), "a\nb\nc\n"),
              "--- a/f\n+++ b/f\n@@ -1,2 +1,3 @@\n a\n b\n+c\n");
    EXPECT_EQ(unified_diff("f", std::string("b\n"), "a\nb\n"),
              "--- a/f\n+++ b/f\n@@ -1 +1,2 @@\n+a\n b\n");
}

TEST(UnifiedDiff, MissingFinalNewlineIsMarkedLikeGit) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb"), "a\nb\n"),
              "--- a/f\n"
              "+++ b/f\n"
              "@@ -1,2 +1,2 @@\n"
              " a\n"
              "-b\n"
              "\\ No newline at end of file\n"
              "+b\n");
    EXPECT_EQ(unified_diff("f", std::string("a\nb\n"), "a\nb"),
              "--- a/f\n"
              "+++ b/f\n"
              "@@ -1,2 +1,2 @@\n"
              " a\n"
              "-b\n"
              "+b\n"
              "\\ No newline at end of file\n");
}

TEST(UnifiedDiff, LineEndingsAreContent) {
    const std::string diff = unified_diff("f", std::string("a\r\nb\r\n"), "a\r\nB\r\n");
    EXPECT_EQ(diff, "--- a/f\n+++ b/f\n@@ -1,2 +1,2 @@\n a\r\n-b\r\n+B\r\n");
}

TEST(UnifiedDiff, DistantChangesMakeSeparateHunks) {
    const std::string diff = unified_diff("f", numbered("l", 20), numbered("l", 20, 2, 19));
    EXPECT_NE(diff.find("@@ -1,5 +1,5 @@\n"), std::string::npos) << diff;
    EXPECT_NE(diff.find("@@ -16,5 +16,5 @@\n"), std::string::npos) << diff;
    EXPECT_EQ(std::count(diff.begin(), diff.end(), '@') / 4, 2);
}

TEST(UnifiedDiff, ChangesSixLinesApartShareAHunkAndSevenDoNot) {
    // Twice the context is where trailing and leading context touch.
    const auto hunks = [](int gap) {
        const int total = gap + 2;
        const std::string diff = unified_diff("f", numbered("l", total), numbered("l", total, 1, total));
        return std::count(diff.begin(), diff.end(), '@') / 4;
    };
    EXPECT_EQ(hunks(6), 1);
    EXPECT_EQ(hunks(7), 2);
}

TEST(UnifiedDiff, ContextIsConfigurable) {
    EXPECT_EQ(unified_diff("f", std::string("a\nb\nc\n"), "a\nB\nc\n", 0),
              "--- a/f\n+++ b/f\n@@ -2 +2 @@\n-b\n+B\n");
    EXPECT_EQ(unified_diff("f", std::string("a\nb\nc\n"), "a\nB\nc\n", 1),
              "--- a/f\n+++ b/f\n@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n");
}

TEST(UnifiedDiff, IsDeterministic) {
    const std::string a = numbered("l", 300);
    const std::string b = numbered("l", 300, 10, 200);
    EXPECT_EQ(unified_diff("f", a, b), unified_diff("f", a, b));
}

// ---- correctness against a reference patch and a reference LCS --------------------------

TEST(UnifiedDiffProperty, ApplyingTheDiffGivesTheNewTextAndTheDiffIsMinimal) {
    Lcg rng(2024);
    const char* const alphabet[] = {"a\n", "b\n", "c\n", "d\n", "}\n", "\n"};

    for (int round = 0; round < 600; ++round) {
        std::vector<std::string> old_lines;
        const unsigned n = rng.next(25);
        for (unsigned k = 0; k < n; ++k) old_lines.push_back(alphabet[rng.next(6)]);

        std::vector<std::string> new_lines = old_lines;
        const unsigned edits = rng.next(6);
        for (unsigned e = 0; e < edits; ++e) {
            const unsigned op = rng.next(3);
            if (op == 0 || new_lines.empty()) {
                new_lines.insert(new_lines.begin() + rng.next(static_cast<unsigned>(new_lines.size()) + 1),
                                 alphabet[rng.next(6)]);
            } else if (op == 1) {
                new_lines.erase(new_lines.begin() + rng.next(static_cast<unsigned>(new_lines.size())));
            } else {
                new_lines[rng.next(static_cast<unsigned>(new_lines.size()))] = alphabet[rng.next(6)];
            }
        }

        std::string old_text, new_text;
        for (const auto& l : old_lines) old_text += l;
        for (const auto& l : new_lines) new_text += l;
        if (rng.next(4) == 0 && !old_text.empty()) old_text.pop_back();   // no final newline
        if (rng.next(4) == 0 && !new_text.empty()) new_text.pop_back();

        for (unsigned context : {0u, 1u, 3u}) {
            const std::string diff = unified_diff("f", old_text, new_text, context);
            if (old_text == new_text) {
                ASSERT_EQ(diff, "");
                continue;
            }
            std::string why;
            const auto applied = apply_diff(old_text, diff, why);
            ASSERT_TRUE(applied.has_value()) << why << "\n--- old\n" << old_text << "--- new\n" << new_text << "--- diff\n" << diff;
            ASSERT_EQ(*applied, new_text) << "round " << round << " context " << context << "\n" << diff;

            const auto a = lines_of(old_text);
            const auto b = lines_of(new_text);
            ASSERT_EQ(changed_lines(diff), a.size() + b.size() - 2 * lcs_length(a, b))
                << "diff is not minimal, round " << round << "\n" << diff;
        }
    }
}

TEST(UnifiedDiffProperty, ANewFileAppliesToNothing) {
    Lcg rng(7);
    for (int round = 0; round < 50; ++round) {
        std::string text;
        for (unsigned k = rng.next(10); k > 0; --k) text += "line" + std::to_string(rng.next(5)) + "\n";
        const std::string diff = unified_diff("f", none, text);
        std::string why;
        const auto applied = apply_diff("", diff, why);
        ASSERT_TRUE(applied.has_value()) << why;
        ASSERT_EQ(*applied, text);
    }
}

TEST(UnifiedDiffProperty, ALargeFileWithScatteredChangesStillDiffsMinimally) {
    std::string old_text = numbered("line", 5000);
    std::string new_text = old_text;
    for (int at : {10, 500, 1200, 1201, 2500, 4000, 4999}) {
        const std::size_t pos = new_text.find("line" + std::to_string(at) + "\n");
        ASSERT_NE(pos, std::string::npos);
        new_text.replace(pos, 4 + std::to_string(at).size(), "edited" + std::to_string(at));
    }
    const std::string diff = unified_diff("f", old_text, new_text);
    std::string why;
    const auto applied = apply_diff(old_text, diff, why);
    ASSERT_TRUE(applied.has_value()) << why;
    EXPECT_EQ(*applied, new_text);
    EXPECT_EQ(changed_lines(diff), 14u) << "7 lines removed + 7 added";
}

TEST(UnifiedDiffProperty, PastTheEditLimitTheDiffIsStillCorrect) {
    // 3000 lines replaced by 3000 others: 6000 edits, well beyond the cap.
    const std::string old_text = numbered("old", 3000);
    const std::string new_text = numbered("new", 3000);
    const std::string diff = unified_diff("f", old_text, new_text);

    std::string why;
    const auto applied = apply_diff(old_text, diff, why);
    ASSERT_TRUE(applied.has_value()) << why;
    EXPECT_EQ(*applied, new_text);
    EXPECT_EQ(changed_lines(diff), 6000u);
    EXPECT_NE(diff.find("@@ -1,3000 +1,3000 @@\n"), std::string::npos);
}
