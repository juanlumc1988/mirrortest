// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Diagnostics about the user's input: what could not be inferred, what was
// guessed, what is unsupported. docs/design.md, section 12, is the catalogue.
//
// These are values, not exceptions. A run keeps going after a bad input so
// that one pass reports everything wrong with it; the CLI then refuses to
// write any file if an error was recorded (design D8). Exceptions are for
// internal failures and I/O, never for something the user typed.
//
// The output format is GCC's, so editors and CI annotate it for free:
//
//   tests/foo_test.cpp:17:5: warning: result of Foo::run() is never used; assumed void [mt-unused-result]
//   tests/foo_test.cpp:9:5: note: first inferred here
//
// Ordering is part of the contract: the golden tests compare diagnostics
// byte for byte, so sorted() is a total order with no dependence on the
// order frontends happened to report in.

#ifndef MT_CORE_DIAGNOSTICS_H
#define MT_CORE_DIAGNOSTICS_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/ir/model.h"

namespace mt::core {

enum class Severity { Note, Warning, Error };

// "note", "warning" or "error".
const char* to_string(Severity severity);

// Diagnostic codes, spelled once. A misspelt code would silently never match
// a golden file or a --strict filter, so callers use these and never a
// literal. Keep in step with the table in docs/design.md, section 12.
namespace code {
inline constexpr const char* io               = "mt-io";
inline constexpr const char* config           = "mt-config";
inline constexpr const char* framework        = "mt-framework";
inline constexpr const char* parse            = "mt-parse";
inline constexpr const char* auto_deduced     = "mt-auto";
inline constexpr const char* unknown_return   = "mt-unknown-return";
inline constexpr const char* unknown_param    = "mt-unknown-param";
inline constexpr const char* chained_call     = "mt-chained-call";
inline constexpr const char* unused_result    = "mt-unused-result";
inline constexpr const char* conflict         = "mt-conflict";
inline constexpr const char* literal          = "mt-literal";
inline constexpr const char* literal_range    = "mt-literal-range";
inline constexpr const char* vexing_parse     = "mt-vexing-parse";
inline constexpr const char* ambiguous_scope  = "mt-ambiguous-scope";
inline constexpr const char* unknown_receiver = "mt-unknown-receiver";
inline constexpr const char* unknown_fixture  = "mt-unknown-fixture";
inline constexpr const char* unknown_type     = "mt-unknown-type";
inline constexpr const char* no_include       = "mt-no-include";
inline constexpr const char* annotation       = "mt-annotation";
inline constexpr const char* unsupported      = "mt-unsupported";
inline constexpr const char* conditional      = "mt-conditional";
inline constexpr const char* stub_may_pass    = "mt-stub-may-pass";
inline constexpr const char* merge_region     = "mt-merge-region";
inline constexpr const char* merge_no_region  = "mt-merge-no-region";
inline constexpr const char* merge_mismatch   = "mt-merge-mismatch";
inline constexpr const char* merge_broke      = "mt-merge-broke";
inline constexpr const char* std_version      = "mt-std";
inline constexpr const char* no_stl           = "mt-no-stl";
}  // namespace code

// A secondary location attached to a diagnostic: "first inferred here".
struct Note {
    ir::SourceLoc where;
    std::string   message;
};

struct Diagnostic {
    Severity       severity = Severity::Error;
    std::string    code;          // "mt-..."; see namespace code
    std::string    message;       // one line, no trailing period required
    ir::SourceLoc  where;         // may be unknown (configuration errors)
    std::vector<Note> notes;      // printed right after the diagnostic, in order

    friend bool operator==(const Diagnostic& a, const Diagnostic& b);
    friend bool operator!=(const Diagnostic& a, const Diagnostic& b) { return !(a == b); }
};

// "file:line:col: severity: message [code]" followed by one "note:" line per
// Note. A diagnostic without a location is printed as
// "mirrortest: severity: message [code]"; one with a file but no line, as
// "file: severity: message [code]". Every line ends with '\n'.
std::string format_diagnostic(const Diagnostic& diagnostic);

class Diagnostics {
public:
    void add(Diagnostic diagnostic);

    void error(std::string_view code, std::string message, ir::SourceLoc where = {});
    void warning(std::string_view code, std::string message, ir::SourceLoc where = {});
    void note(std::string_view code, std::string message, ir::SourceLoc where = {});

    bool empty() const { return items_.empty(); }
    bool has_errors() const { return count(Severity::Error) != 0; }
    std::size_t count(Severity severity) const;

    // --strict: every warning becomes an error. Notes stay notes -- they are
    // information, not something the user is asked to fix.
    void promote_warnings();

    // Everything recorded, in the order it was reported.
    const std::vector<Diagnostic>& all() const { return items_; }

    // Everything recorded in output order, exact duplicates removed.
    //
    // Order: file, line, column, code, severity, message. Diagnostics with no
    // file sort first. A diagnostic reported twice -- the same construct
    // reached through both branches of an #if, say -- carries no extra
    // information and is printed once.
    std::vector<Diagnostic> sorted() const;

    // Writes sorted() in the GCC format.
    void format(std::ostream& out) const;

private:
    std::vector<Diagnostic> items_;
};

}  // namespace mt::core

#endif  // MT_CORE_DIAGNOSTICS_H
