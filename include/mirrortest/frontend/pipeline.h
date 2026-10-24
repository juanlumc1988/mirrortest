// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The tests frontend as one call: a test file in, the model out
// (docs/design.md, section 7.1).
//
// It runs the stages in order -- read the file, classify the names, infer the
// types -- and repeats the last two while that keeps teaching it something.
// What it can learn is the type behind a chained call: in
// `repo.find(1).name()` nothing says what find returns until a test compares
// it, declares it or an annotation says so; once inference knows, `name` is a
// method of that type, which can change which classes exist, which can change
// what is inferred. A chain of N links needs N rounds, and the rounds are
// capped (kMaxRounds) so a pathological file cannot loop.

#ifndef MT_FRONTEND_PIPELINE_H
#define MT_FRONTEND_PIPELINE_H

#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/source.h"
#include "mirrortest/frontend/classify.h"
#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/preprocess.h"

namespace mt::frontend {

struct AnalysisOptions {
    core::Config           config;
    std::vector<ir::Model> known;     // the headers the test includes, already read (7.10)
    syntax::MacroTable     macros;    // config.ignore_macros, for the preprocessing
};

struct Analysis {
    Classified classified;            // the last round's
    ir::Model  model;
};

// The most rounds of classify + infer a file gets: one per link of the longest
// chain it can resolve, which is plenty for code anybody reads.
constexpr int kMaxRounds = 4;

Analysis analyse(const core::SourceFile& test, const AnalysisOptions& options, core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_PIPELINE_H
