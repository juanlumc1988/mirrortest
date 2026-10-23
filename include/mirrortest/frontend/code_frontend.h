// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The code frontend: C++ declarations in, ir::Model out (design section 6).
//
// It is used in three places -- the headers gen-tests reads, the headers a
// test already includes (the API gen-impl must not regenerate), and the
// fixtures and helper classes inside a test file -- so it reads declarations,
// not headers: any translation unit will do.
//
// What it reads: namespaces (nested `a::b` included), classes and structs with
// a body, their members (methods, constructors, destructors, conversion
// operators, fields), enums, free functions and #include directives. What it
// deliberately does not: bodies, variables, typedefs and aliases outside a
// class, friends, `using`. Anything it does read it reads faithfully or not at
// all -- a function pointer field is a field, not a method.
//
// It tolerates what it cannot make sense of. The parser always yields a tree;
// where it marks an ERROR, the frontend warns (`mt-parse`) and reads what is
// around it, because a header that mostly parses is far more useful than none.

#ifndef MT_FRONTEND_CODE_FRONTEND_H
#define MT_FRONTEND_CODE_FRONTEND_H

#include <string>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/source.h"
#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/preprocess.h"
#include "mirrortest/syntax/tree.h"

namespace mt::frontend {

// Reads the declarations of an already parsed translation unit into `model`.
//
// `markers` are the Qt markers the preprocessing pass left (QObject, slots
// and signals sections, Q_INVOKABLE, Q_DISABLE_COPY); pass {} for code that
// has none. `file` is the path as the user gave it, for locations.
// `include_path` is what an #include of this file would say ("acme/foo.h"); it
// becomes ir::Class::header and ir::Function::header.
//
// A class defined more than once -- typically under both branches of an #if --
// is one class: the members are merged and a signature already present is not
// added twice.
void read_declarations(const syntax::Tree& tree, const std::vector<syntax::QtMarker>& markers,
                       const std::string& file, const std::string& include_path, ir::Model& model,
                       core::Diagnostics& diagnostics);

// Preprocesses (Qt macros), parses and reads one file.
ir::Model read_header(const core::SourceFile& source, const std::string& include_path,
                      const syntax::MacroTable& macros, core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_CODE_FRONTEND_H
