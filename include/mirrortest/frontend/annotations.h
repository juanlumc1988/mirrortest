// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// `// @mt:` annotations: the user telling the tool what no test can say
// (docs/design.md, section 7.11).
//
//     // @mt: returns std::vector<int>
//     auto items = foo.items();
//
//     // @mt: acme::Foo::items returns std::vector<int>; acme::Foo::reset noexcept
//     // @mt: class Foo namespace acme; class Foo header "acme/foo.h"
//
// A directive is short (about the next statement) or long (about a member or a
// class, valid for the whole file). Reading them is purely syntactic and
// happens with the rest of the test file; applying the short ones needs to know
// which calls are the code under test, so it happens at the end of
// classification (apply_call_annotations).
//
// Everything an annotation says is Origin::Annotated, the highest trust there
// is, and a malformed one is an `mt-annotation` error: an annotation that is
// silently ignored is worse than none.

#ifndef MT_FRONTEND_ANNOTATIONS_H
#define MT_FRONTEND_ANNOTATIONS_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/tree.h"

namespace mt::frontend {

struct Classified;   // frontend/classify.h

// What the short directives say about one call, kept on the call's fact.
struct CallAnnotation {
    std::optional<ir::Type> returns;     // `returns T`: the result's type, Origin::Annotated
    bool overload = false;               // `overload`: a distinct overload, even if only the types differ
    bool ignore = false;                 // `ignore`: not code under test; generate nothing for it
};

// The span of the statement a short directive applies to.
struct StatementRange {
    ir::SourceLoc begin;
    ir::SourceLoc end;

    bool valid() const { return begin.known(); }
    bool contains(const ir::SourceLoc& p) const;
};

// The short directives of one comment, which share a target.
struct ShortAnnotation {
    ir::SourceLoc           where;       // the comment
    std::optional<ir::Type> returns;
    bool                    overload = false;
    bool                    ignore = false;
    StatementRange          target;      // !valid() if no statement could be found
};

struct Annotations {
    std::vector<ShortAnnotation> shorts;

    // Long forms. `overrides` has the shape and the meaning of config.overrides
    // (keyed "Class::member" or "ns::Class::member", as written, without a
    // leading "::"); the configuration's own entries are merged in by classify().
    std::map<std::string, core::MemberOverride> overrides;
    std::map<std::string, std::string>          class_namespace;   // "Foo" -> "acme::detail"
    std::map<std::string, std::string>          class_header;      // "Foo" -> "acme/foo.h"
};

// Reads every `// @mt:` comment of a parsed file. Problems are `mt-annotation`
// errors located at the comment; the directives that are fine are still kept.
Annotations read_annotations(const syntax::Tree& tree, const std::string& file, core::Diagnostics& diagnostics);

// Attaches the short directives to the one call of the code under test in the
// statement each one applies to. A statement with none, or with several, is an
// `mt-annotation` error: the directive would otherwise land on something other
// than what its author meant.
void apply_call_annotations(Classified& classified, core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_ANNOTATIONS_H
