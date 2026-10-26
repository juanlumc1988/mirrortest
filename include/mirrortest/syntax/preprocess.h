// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Blanking: make Qt-flavoured C++ parseable by a syntactic parser without
// moving a single byte.
//
// tree-sitter does not preprocess, and the Qt keywords are not C++. Left
// alone, `Q_OBJECT`, `private slots:`, `signals:`, `Q_PROPERTY(...)` and
// export macros such as `class FOO_EXPORT Widget` turn into ERROR nodes that
// swallow the declarations next to them (docs/design.md, R8: eleven errors in
// one ordinary header, none once the macros are gone). So they are replaced
// by spaces first.
//
// The one invariant everything else relies on: the output is exactly as long
// as the input and every line break is where it was. A blanked range keeps
// its newlines, so a line and column reported against the output are the
// line and column in the user's file, and diagnostics need no translation.
// (One exception, and only for a macro that spans several lines: a mock
// method that is expanded keeps its line breaks at the end of the range, so a
// line is still a line, and a column on the last line of the macro is off.)
//
// What the macros meant is not lost. Each one that carries information the
// model needs -- this class is a QObject, this section holds slots -- leaves
// a QtMarker at its offset, and the code frontend attaches it to the syntax
// node found there.
//
// This is a lexical scan, not a preprocessor. It understands exactly enough
// C++ to avoid touching text that merely contains a macro name: comments,
// string, character and raw-string literals, numbers with digit separators,
// and preprocessor directives (including continued ones).

#ifndef MT_SYNTAX_PREPROCESS_H
#define MT_SYNTAX_PREPROCESS_H

#include <string>
#include <string_view>
#include <vector>

namespace mt::syntax {

// Extra project macros to blank, from the configuration (`ignore_macros`).
// A name followed on the same line by '(' is treated as function-like and
// blanked together with its balanced argument list; otherwise as object-like.
struct MacroTable {
    std::vector<std::string> ignore;

    // gmock's MOCK_METHOD macros become the member each declares (mock_macros.h): the
    // reader of a test file wants it, a header's has no use for it.
    bool expand_mock_methods = false;
};

struct QtMarker {
    enum class Kind {
        QObject,         // Q_OBJECT: offset of the macro
        Gadget,          // Q_GADGET: a Qt value type, not a QObject
        SlotsSection,    // offset of the access specifier the `slots` keyword follows
        SignalsSection,  // same for `signals`; see preprocess() for the offset
        Invokable,       // Q_INVOKABLE: offset of the macro; applies to the next member
        DisableCopy,     // Q_DISABLE_COPY / Q_DISABLE_COPY_MOVE
    };
    Kind     kind;
    unsigned offset;     // byte offset into the (equal-length) text
};

// A half-open byte range [begin, end) that was blanked or replaced.
struct BlankedRange {
    unsigned begin;
    unsigned end;
};

struct Preprocessed {
    std::string               text;     // same length as the input
    std::vector<QtMarker>     markers;  // in source order
    std::vector<BlankedRange> blanked;  // in source order
};

// Returns `source` with Qt macros blanked and a UTF-8 byte-order mark
// replaced by spaces.
//
// Macros that stand for a C++ keyword are replaced by that keyword, padded
// with spaces (`Q_DECL_OVERRIDE` -> `override`); `signals:` becomes
// `public :` since the parser must still see an access specifier there. A
// `signals` that already follows one (`public signals:`) is just blanked, and
// its marker sits at that specifier.
//
// Sources larger than 4 GiB are returned unchanged: parse_cpp refuses them
// anyway and offsets are 32-bit.
Preprocessed preprocess(std::string_view source, const MacroTable& table = {});

}  // namespace mt::syntax

#endif  // MT_SYNTAX_PREPROCESS_H
