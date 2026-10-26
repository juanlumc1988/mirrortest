// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/diagnostics.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace mt::core {

namespace {

bool same_loc(const ir::SourceLoc& a, const ir::SourceLoc& b) {
    return a.file == b.file && a.line == b.line && a.column == b.column;
}

bool same_notes(const std::vector<Note>& a, const std::vector<Note>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same_loc(a[i].where, b[i].where) || a[i].message != b[i].message) {
            return false;
        }
    }
    return true;
}

// "file:line:col", degrading as the location gets less precise.
std::string format_location(const ir::SourceLoc& where) {
    if (where.file.empty()) {
        return "mirrortest";
    }
    std::string out = where.file;
    if (where.line != 0) {
        out += ':';
        out += std::to_string(where.line);
        if (where.column != 0) {
            out += ':';
            out += std::to_string(where.column);
        }
    }
    return out;
}

// Output order. Severity is deliberately after the code: two diagnostics at
// the same place with the same code are the same finding.
bool before(const Diagnostic& a, const Diagnostic& b) {
    if (a.where.file != b.where.file) {
        return a.where.file < b.where.file;
    }
    if (a.where.line != b.where.line) {
        return a.where.line < b.where.line;
    }
    if (a.where.column != b.where.column) {
        return a.where.column < b.where.column;
    }
    if (a.code != b.code) {
        return a.code < b.code;
    }
    if (a.severity != b.severity) {
        return static_cast<int>(a.severity) < static_cast<int>(b.severity);
    }
    return a.message < b.message;
}

bool tied(const Diagnostic& a, const Diagnostic& b) {
    return !before(a, b) && !before(b, a);
}

}  // namespace

const char* to_string(Severity severity) {
    switch (severity) {
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
    }
    return "error";
}

bool operator==(const Diagnostic& a, const Diagnostic& b) {
    return a.severity == b.severity && a.code == b.code && a.message == b.message &&
           same_loc(a.where, b.where) && same_notes(a.notes, b.notes);
}

std::string format_diagnostic(const Diagnostic& diagnostic) {
    std::string out = format_location(diagnostic.where);
    out += ": ";
    out += to_string(diagnostic.severity);
    out += ": ";
    out += diagnostic.message;
    if (!diagnostic.code.empty()) {
        out += " [";
        out += diagnostic.code;
        out += ']';
    }
    out += '\n';

    for (const Note& note : diagnostic.notes) {
        out += format_location(note.where);
        out += ": note: ";
        out += note.message;
        out += '\n';
    }
    return out;
}

void Diagnostics::add(Diagnostic diagnostic) {
    items_.push_back(std::move(diagnostic));
}

void Diagnostics::error(std::string_view code, std::string message, ir::SourceLoc where) {
    add({Severity::Error, std::string(code), std::move(message), std::move(where), {}});
}

void Diagnostics::warning(std::string_view code, std::string message, ir::SourceLoc where) {
    add({Severity::Warning, std::string(code), std::move(message), std::move(where), {}});
}

void Diagnostics::note(std::string_view code, std::string message, ir::SourceLoc where) {
    add({Severity::Note, std::string(code), std::move(message), std::move(where), {}});
}

std::size_t Diagnostics::count(Severity severity) const {
    return static_cast<std::size_t>(
        std::count_if(items_.begin(), items_.end(),
                      [severity](const Diagnostic& d) { return d.severity == severity; }));
}

void Diagnostics::promote_warnings() {
    for (Diagnostic& d : items_) {
        if (d.severity == Severity::Warning) {
            d.severity = Severity::Error;
        }
    }
}

std::vector<Diagnostic> Diagnostics::sorted() const {
    std::vector<Diagnostic> ordered = items_;
    // stable_sort keeps the reporting order between diagnostics that tie on
    // every key but differ in their notes, so the result never depends on
    // the implementation's idea of an unstable order.
    std::stable_sort(ordered.begin(), ordered.end(), before);

    // Drop exact duplicates. Equal diagnostics always tie on the sort key,
    // but with differing notes in between they need not be adjacent, so each
    // one is compared against the whole group it ties with.
    std::vector<Diagnostic> out;
    out.reserve(ordered.size());
    for (Diagnostic& d : ordered) {
        bool duplicate = false;
        for (auto it = out.rbegin(); it != out.rend() && tied(*it, d); ++it) {
            if (*it == d) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            out.push_back(std::move(d));
        }
    }
    return out;
}

void Diagnostics::format(std::ostream& out) const {
    for (const Diagnostic& d : sorted()) {
        out << format_diagnostic(d);
    }
}

}  // namespace mt::core
