// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/pipeline.h"

#include <map>

#include "mirrortest/frontend/inference.h"
#include "mirrortest/frontend/test_frontend.h"

namespace mt::frontend {

namespace {

// The call a chain link hangs from, as a fact whose result inference reports:
// a link that was resolved in an earlier round is the method call it became.
FactRef root_fact(const TestFileFacts& facts, FactRef root) {
    if (root.kind == FactRef::Kind::Unresolved) {
        const Unresolved& u = facts.unresolved[root.index];
        if (u.resolved && u.method_call < facts.method_calls.size()) return {FactRef::Kind::MethodCall, u.method_call};
    }
    return root;
}

}  // namespace

Analysis analyse(const core::SourceFile& test, const AnalysisOptions& options, core::Diagnostics& diagnostics) {
    TestFrontendOptions frontend_options;
    frontend_options.standard = options.config.cxx_std;
    frontend_options.macros = options.macros;
    const TestFileFacts base = read_test_file(test, frontend_options, diagnostics);

    ClassifyOptions classify_options;
    classify_options.config = options.config;
    classify_options.known = options.known;

    // Chain links resolved so far: index into facts.unresolved -> the receiver.
    std::map<std::size_t, Receiver> resolved;

    Analysis out;
    for (int round = 1;; ++round) {
        // Each round starts from what the file said, plus what has been learnt:
        // classification adds to the facts, and doing it twice to the same ones
        // would add twice.
        TestFileFacts facts = base;
        for (const auto& entry : resolved) {
            Unresolved& u = facts.unresolved[entry.first];
            MethodCall call = u.call;
            call.receiver = entry.second.cls;
            call.on_const = entry.second.on_const;
            facts.method_calls.push_back(std::move(call));
            u.resolved = true;
            u.method_call = facts.method_calls.size() - 1;
        }

        // The diagnostics of a round that is followed by another are not final:
        // the next one may settle what this one complained about.
        core::Diagnostics scratch;
        out.classified = classify(std::move(facts), classify_options, scratch);
        ResultTypes results;
        out.model = infer(out.classified, options.config, options.known, scratch, &results);

        // The links whose root now has a type, and whose type has members.
        const TestFileFacts& f = out.classified.facts;
        std::map<std::size_t, Receiver> ready;
        std::vector<std::size_t> hopeless;
        for (std::size_t i = 0; i < f.unresolved.size(); ++i) {
            const Unresolved& u = f.unresolved[i];
            if (u.resolved) continue;
            const ir::Type* type = results.find(root_fact(f, u.root));
            if (type == nullptr) continue;
            if (const std::optional<Receiver> receiver = receiver_of_type(*type, u.call.via_arrow)) {
                ready.emplace(i, *receiver);
            } else {
                hopeless.push_back(i);
            }
        }

        if (ready.empty() || round == kMaxRounds) {
            for (std::size_t i : hopeless) {
                const Unresolved& u = f.unresolved[i];
                scratch.error(core::code::chained_call,
                              "'" + u.text + "' calls '" + u.call.method + "' on a value of type '" +
                                  results.find(root_fact(f, u.root))->spelling + "', which has no members",
                              u.where);
            }
            for (const auto& entry : ready) {
                scratch.warning(core::code::chained_call,
                                "'" + f.unresolved[entry.first].text + "' is a chain of more than " +
                                    std::to_string(kMaxRounds) + " links; the rest of it is not followed",
                                f.unresolved[entry.first].where);
            }
            for (const core::Diagnostic& d : scratch.all()) diagnostics.add(d);
            return out;
        }
        resolved.insert(ready.begin(), ready.end());
    }
}

}  // namespace mt::frontend
