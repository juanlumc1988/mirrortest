// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/impl_merge.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/impl_backend.h"
#include "mirrortest/core/merge.h"
#include "mirrortest/core/output.h"
#include "mirrortest/core/regions.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/ir/type.h"
#include "mirrortest/syntax/extract.h"
#include "mirrortest/syntax/preprocess.h"
#include "mirrortest/syntax/tree.h"

namespace mt::backend {

namespace {

using core::Region;

// ---- small text helpers ---------------------------------------------------------------------------------------

std::string trimmed(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' || s[begin] == '\n')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) --end;
    return s.substr(begin, end - begin);
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

std::string without_eol(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

std::string without_indent(const std::string& line, const std::string& indent) {
    return starts_with(line, indent) ? line.substr(indent.size()) : trimmed(line);
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : separator) + parts[i];
    return out;
}

std::vector<std::string> split_scope(const std::string& qualified) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t colons = qualified.find("::", start);
        if (colons == std::string::npos) {
            if (start < qualified.size()) parts.push_back(qualified.substr(start));
            return parts;
        }
        if (colons > start) parts.push_back(qualified.substr(start, colons - start));
        start = colons + 2;
    }
}

enum class FileKind { Header, Source, Other };

FileKind kind_of(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
    if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") return FileKind::Header;
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++") return FileKind::Source;
    return FileKind::Other;
}

// How badly a text parses: the number of error and missing nodes. The merged
// file must not parse worse than the one it grew from.
std::size_t parse_errors(const std::string& text) {
    const syntax::Tree tree = syntax::parse_cpp(text);
    std::size_t count = 0;
    syntax::walk(tree.root(), [&](const syntax::Node& n) {
        if (n.is_error() || n.is_missing()) ++count;
        return true;
    });
    return count;
}

ir::Model read_declarations_of(const std::string& path, const std::string& text) {
    core::Diagnostics ignored;   // the user's own file: its parse problems are not ours to report
    return frontend::read_header({path, text}, std::string(), syntax::MacroTable(), ignored);
}

std::string include_key(const std::string& line) {
    std::string text = trimmed(line);
    if (text.empty() || text[0] != '#') return {};
    text = trimmed(text.substr(1));
    if (!starts_with(text, "include")) return {};
    text = trimmed(text.substr(7));
    if (text.empty() || (text[0] != '<' && text[0] != '"')) return {};
    const char close = text[0] == '<' ? '>' : '"';
    const std::size_t end = text.find(close, 1);
    if (end == std::string::npos) return {};
    return text.substr(0, end + 1);
}

// ---- members --------------------------------------------------------------------------------------------------

// What the merge compares about a method, a field or a free function.
struct Entry {
    enum class Kind { Method, Field, Function };
    Kind                     kind = Kind::Method;
    std::string              name;
    std::vector<std::string> params;
    std::string              ret;
    bool                     is_const = false;
    bool                     is_static = false;
    unsigned                 line = 0;       // of the declaration, 1-based
    ir::SourceLoc            where;

    std::string plain_key() const {
        return std::to_string(static_cast<int>(kind)) + ":" + name + "/" + std::to_string(params.size());
    }
    std::string typed_key() const { return std::to_string(static_cast<int>(kind)) + ":" + name + "(" + join(params, ",") + ")"; }

    // As a person would say it, for the messages.
    std::string describe() const {
        std::string out;
        if (is_static) out += "static ";
        if (!ret.empty()) out += ret + " ";
        out += name;
        if (kind != Kind::Field) out += "(" + join(params, ", ") + ")" + (is_const ? " const" : "");
        return out;
    }

    // The part that must agree for two declarations to be the same member.
    std::string shape() const {
        return ret + "|" + join(params, ",") + "|" + (is_const ? "const" : "") + "|" + (is_static ? "static" : "");
    }
};

std::string norm(const ir::Type& t) { return ir::normalize_spelling(t.spelling); }

Entry entry_of(const ir::Method& m) {
    Entry e;
    e.kind = Entry::Kind::Method;
    e.name = m.name;
    for (const ir::Param& p : m.params) e.params.push_back(norm(p.type));
    e.ret = norm(m.ret);
    e.is_const = m.is_const;
    e.is_static = m.is_static;
    e.line = m.where.line;
    e.where = m.where;
    return e;
}

Entry entry_of(const ir::Field& f) {
    Entry e;
    e.kind = Entry::Kind::Field;
    e.name = f.name;
    e.ret = norm(f.type);
    e.is_static = f.is_static;
    e.line = f.where.line;
    e.where = f.where;
    return e;
}

Entry entry_of(const ir::Function& f) {
    Entry e;
    e.kind = Entry::Kind::Function;
    e.name = f.name;
    for (const ir::Param& p : f.params) e.params.push_back(norm(p.type));
    e.ret = norm(f.ret);
    e.line = f.where.line;
    e.where = f.where;
    return e;
}

std::vector<Entry> entries_of(const ir::Class& c) {
    std::vector<Entry> out;
    for (const ir::Method& m : c.methods) out.push_back(entry_of(m));
    for (const ir::Field& f : c.fields) out.push_back(entry_of(f));
    return out;
}

std::vector<Entry> entries_of(const ir::Model& model, const std::vector<std::string>& ns) {
    std::vector<Entry> out;
    for (const ir::Function& f : model.functions) {
        if (f.ns == ns) out.push_back(entry_of(f));
    }
    return out;
}

const ir::Class* find_class(const ir::Model& model, const std::string& label) {
    for (const ir::Class& c : model.classes) {
        if (ir::qualified_name(c.ns, c.name) == label) return &c;
    }
    return nullptr;
}

const ir::Enum* find_enum(const ir::Model& model, const std::string& label) {
    for (const ir::Enum& e : model.enums) {
        if (ir::qualified_name(e.ns, e.name) == label) return &e;
    }
    return nullptr;
}

// Which members a region holds: a class's, or a namespace's free functions.
struct Group {
    bool               is_class = false;
    std::vector<Entry> entries;
    bool               found = false;   // the class exists in the model it was looked up in
};

Group group_in(const ir::Model& model, const std::string& label, const ir::Model& desired) {
    Group g;
    if (find_class(desired, label) != nullptr) {
        g.is_class = true;
        if (const ir::Class* c = find_class(model, label)) {
            g.entries = entries_of(*c);
            g.found = true;
        }
        return g;
    }
    std::vector<std::string> ns = split_scope(label);
    if (!ns.empty() && ns.back() == "functions") ns.pop_back();
    g.entries = entries_of(model, ns);
    g.found = true;   // free functions need no enclosing declaration
    return g;
}

// ---- the blocks of a region in the generated text ---------------------------------------------------------------

// One declaration and the comments directly above it.
struct Block {
    std::vector<std::string> lines;   // without terminators or the region's indentation
    unsigned                 declaration_line = 0;   // 1-based, in the generated text
};

std::vector<Block> header_blocks(const std::vector<std::string>& text_lines, const Region& region) {
    std::vector<Block> blocks;
    std::vector<std::string> pending;
    for (std::size_t i = region.begin + 1; i < region.end && i < text_lines.size(); ++i) {
        const std::string line = without_eol(text_lines[i]);
        const std::string bare = trimmed(line);
        if (bare.empty()) {
            pending.clear();
        } else if (starts_with(bare, "//")) {
            pending.push_back(without_indent(line, region.indent));
        } else {
            Block b;
            b.lines = pending;
            b.lines.push_back(without_indent(line, region.indent));
            b.declaration_line = static_cast<unsigned>(i + 1);
            blocks.push_back(std::move(b));
            pending.clear();
        }
    }
    return blocks;
}

// ---- what a merge in progress knows ----------------------------------------------------------------------------

struct Job {
    const core::OutputFile& generated;
    std::string             text;            // the existing file, as it grows
    std::size_t             added = 0;
    bool                    refused = false; // something needs a region the file lacks
    core::Diagnostics&      diagnostics;

    ir::SourceLoc at(unsigned line = 0) const {
        ir::SourceLoc where;
        where.file = generated.path;
        where.line = line;
        where.column = line == 0 ? 0 : 1;
        return where;
    }
};

// The region `label` of the text being merged, as it is now.
std::optional<Region> current_region(Job& job, const std::string& label) {
    core::Diagnostics scratch;   // the regions were validated before the first insertion
    const std::optional<std::vector<Region>> regions = core::find_regions(job.text, job.generated.path, scratch);
    if (!regions) return std::nullopt;
    const Region* r = core::find_region(*regions, label);
    return r == nullptr ? std::nullopt : std::optional<Region>(*r);
}

void refuse(Job& job, const std::string& label, const std::string& why) {
    job.diagnostics.warning(core::code::merge_no_region,
                            "'" + label + "' " + why + " in " + job.generated.path + "; the new text goes to a .new file instead",
                            job.at());
    job.refused = true;
}

// Inserts the lines into the region of that label, if it still exists.
bool insert(Job& job, const std::string& label, const std::vector<std::string>& lines) {
    const std::optional<Region> region = current_region(job, label);
    if (!region) return false;
    job.text = core::insert_before_end(job.text, *region, lines);
    return true;
}

// ---- headers -------------------------------------------------------------------------------------------------------

// The enumerators of an enum the run wants that the existing one lacks, by name. Their notes go with them.
void merge_enumerators(Job& job, const std::string& label, const Region& generated_region, const std::vector<std::string>& generated_lines,
                       const ir::Model& present) {
    const ir::Enum* have = find_enum(present, label);
    if (have == nullptr) {
        refuse(job, label, "is declared by the tests but not by the existing file, though its region is there");
        return;
    }
    std::vector<std::string> to_insert;
    std::size_t count = 0;
    for (const Block& block : header_blocks(generated_lines, generated_region)) {
        std::string name = trimmed(block.lines.back());
        if (!name.empty() && name.back() == ',') name.pop_back();
        if (std::find(have->enumerators.begin(), have->enumerators.end(), name) != have->enumerators.end()) continue;
        for (const std::string& line : block.lines) to_insert.push_back(line);
        ++count;
    }
    if (count == 0) return;
    if (!insert(job, label, to_insert)) {
        refuse(job, label, "has no region");
        return;
    }
    job.added += count;
}

void merge_members(Job& job, const std::string& label, const Region& generated_region, const std::vector<std::string>& generated_lines,
                   const ir::Model& desired, const ir::Model& present) {
    if (find_enum(desired, label) != nullptr) {
        merge_enumerators(job, label, generated_region, generated_lines, present);
        return;
    }
    const Group wanted = group_in(desired, label, desired);
    const Group have = group_in(present, label, desired);
    if (!have.found) {
        refuse(job, label, "is declared by the tests but not by the existing file, though its region is there");
        return;
    }

    // Overloads of one arity can only be told apart by their types.
    std::set<std::string> crowded;
    {
        std::set<std::string> seen;
        for (const Entry& e : wanted.entries) {
            if (!seen.insert(e.plain_key()).second) crowded.insert(e.plain_key());
        }
    }
    const auto find_present = [&](const Entry& e) -> const Entry* {
        const bool typed = crowded.count(e.plain_key()) != 0;
        for (const Entry& h : have.entries) {
            if (typed ? h.typed_key() == e.typed_key() : h.plain_key() == e.plain_key()) return &h;
        }
        return nullptr;
    };

    std::vector<std::string> to_insert;
    std::size_t count = 0;
    for (const Block& block : header_blocks(generated_lines, generated_region)) {
        const auto wants = std::find_if(wanted.entries.begin(), wanted.entries.end(),
                                        [&](const Entry& e) { return e.line == block.declaration_line; });
        if (wants == wanted.entries.end()) continue;   // a line that declares nothing we know

        if (const Entry* present_entry = find_present(*wants)) {
            if (present_entry->shape() != wants->shape()) {
                job.diagnostics.warning(core::code::merge_mismatch,
                                        "'" + present_entry->describe() + "' is declared here but the tests imply '" + wants->describe() +
                                            "'; left as it is",
                                        present_entry->where);
            }
            continue;
        }
        for (const std::string& line : block.lines) to_insert.push_back(line);
        ++count;
    }
    if (count == 0) return;
    if (!insert(job, label, to_insert)) {
        refuse(job, label, "has no region");
        return;
    }
    job.added += count;
}

void merge_includes(Job& job, const std::vector<std::string>& generated_lines, const Region& generated_region) {
    std::set<std::string> have;
    for (const std::string& line : core::split_lines(job.text)) {
        const std::string key = include_key(line);
        if (!key.empty()) have.insert(key);
    }
    std::vector<std::string> missing;
    for (std::size_t i = generated_region.begin + 1; i < generated_region.end && i < generated_lines.size(); ++i) {
        const std::string line = without_eol(generated_lines[i]);
        const std::string key = include_key(line);
        if (!key.empty() && have.insert(key).second) missing.push_back(without_indent(line, generated_region.indent));
    }
    if (missing.empty()) return;
    if (!insert(job, core::includes_label(), missing)) {
        // The includes are a convenience, not the file's substance: say so and carry on.
        job.diagnostics.warning(core::code::merge_no_region,
                                job.generated.path + " has no 'includes' region, so these lines are not added: " + join(missing, " "),
                                job.at());
    }
}

// ---- sources -------------------------------------------------------------------------------------------------------

struct Definition {
    std::string signature;       // "acme::Foo::size() const"
    std::size_t first = 0;       // line indexes, 0-based, inclusive
    std::size_t last = 0;
};

void collect_definitions(const syntax::Node& container, std::vector<std::string> ns, const std::string& file,
                         std::vector<Definition>& out) {
    for (const syntax::Node& child : container.named_children()) {
        const std::string_view type = child.type();
        if (type == "namespace_definition") {
            const syntax::Node name = child.child_by_field("name");
            const syntax::Node body = child.child_by_field("body");
            std::vector<std::string> inner = ns;
            if (!name.is_null()) {
                for (const std::string& part : split_scope(std::string(name.text()))) inner.push_back(part);
            }
            if (!body.is_null()) collect_definitions(body, inner, file, out);
        } else if (type == "linkage_specification" || type.substr(0, 7) == "preproc") {
            const syntax::Node body = child.child_by_field("body");
            collect_definitions(body.is_null() ? child : body, ns, file, out);
        } else if (type == "function_definition") {
            const syntax::Declarator d = syntax::parse_declarator(child.child_by_field("declarator"));
            if (d.function.is_null() || d.name.empty()) continue;
            std::vector<std::string> parts = ns;
            for (const std::string& part : split_scope(d.name)) parts.push_back(part);
            std::vector<std::string> types;
            for (const ir::Param& p : syntax::read_parameters(d.function.child_by_field("parameters"), file)) {
                types.push_back(norm(p.type));
            }
            const syntax::FunctionTraits traits = syntax::read_function_traits(d.function, file);
            Definition def;
            def.signature = join(parts, "::") + "(" + join(types, ", ") + ")" + (traits.is_const ? " const" : "");
            def.first = child.line() - 1;
            // The node ends at its closing brace; its last line is where that is.
            const std::string_view text = child.text();
            def.last = def.first + static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
            out.push_back(std::move(def));
        }
    }
}

std::vector<Definition> definitions_of(const std::string& text, const std::string& file) {
    const syntax::Tree tree = syntax::parse_cpp(text);
    std::vector<Definition> out;
    collect_definitions(tree.root(), {}, file, out);
    return out;
}

// What a header (as merged) declares and a source is therefore allowed to define.
std::set<std::string> declared_signatures(const ir::Model& header) {
    std::set<std::string> out;
    for (const ir::Class& c : header.classes) {
        for (const ir::Method& m : c.methods) {
            std::vector<std::string> types;
            for (const ir::Param& p : m.params) types.push_back(norm(p.type));
            out.insert(ir::qualified_name(c.ns, c.name) + "::" + m.name + "(" + join(types, ", ") + ")" + (m.is_const ? " const" : ""));
        }
    }
    for (const ir::Function& f : header.functions) {
        std::vector<std::string> types;
        for (const ir::Param& p : f.params) types.push_back(norm(p.type));
        out.insert(ir::qualified_name(f.ns, f.name) + "(" + join(types, ", ") + ")");
    }
    return out;
}

void merge_definitions(Job& job, const std::string& label, const Region& generated_region,
                       const std::vector<std::string>& generated_lines, const std::vector<Definition>& wanted,
                       const std::set<std::string>& defined, const std::set<std::string>& declared) {
    std::vector<std::string> to_insert;
    std::size_t count = 0;
    for (const Definition& def : wanted) {
        if (def.first <= generated_region.begin || def.last >= generated_region.end) continue;   // another region's
        if (defined.count(def.signature) != 0 || declared.count(def.signature) == 0) continue;
        to_insert.push_back(std::string());   // a blank line before each (the region may be empty; a leading one is trimmed below)
        for (std::size_t i = def.first; i <= def.last && i < generated_lines.size(); ++i) {
            to_insert.push_back(without_indent(without_eol(generated_lines[i]), generated_region.indent));
        }
        ++count;
    }
    if (count == 0) return;

    // Separate from what is there, but do not open an empty region with a blank line.
    const std::optional<Region> region = current_region(job, label);
    if (!region) {
        refuse(job, label, "has no region");
        return;
    }
    bool holds_code = false;
    for (const std::string& line : core::region_body(job.text, *region)) holds_code = holds_code || !trimmed(line).empty();
    if (!holds_code) to_insert.erase(to_insert.begin());
    job.text = core::insert_before_end(job.text, *region, to_insert);
    job.added += count;
}

}  // namespace

ImplMerger::ImplMerger(std::string out_include, Reader reader) : out_include_(std::move(out_include)), reader_(std::move(reader)) {}

core::MergeFn ImplMerger::function() {
    return [this](const core::OutputFile& generated, const std::string& existing, core::Diagnostics& diagnostics) {
        return merge(generated, existing, diagnostics);
    };
}

std::optional<core::MergeResult> ImplMerger::merge(const core::OutputFile& generated, const std::string& existing,
                                                   core::Diagnostics& diagnostics) {
    const FileKind kind = kind_of(generated.path);
    if (kind == FileKind::Other) return std::nullopt;

    // Malformed regions in the user's file are an error: nothing may be written until they are fixed.
    const std::optional<std::vector<Region>> existing_regions = core::find_regions(existing, generated.path, diagnostics);
    if (!existing_regions) return std::nullopt;
    core::Diagnostics ours;   // regions of our own output cannot be malformed, and if they are the file is just not merged
    const std::optional<std::vector<Region>> wanted_regions = core::find_regions(generated.content, generated.path, ours);
    if (!wanted_regions) return std::nullopt;

    Job job{generated, existing, 0, false, diagnostics};
    const std::vector<std::string> generated_lines = core::split_lines(generated.content);
    const ir::Model desired = read_declarations_of(generated.path, generated.content);

    if (kind == FileKind::Header) {
        const ir::Model present = read_declarations_of(generated.path, existing);
        for (const Region& r : *wanted_regions) {
            if (r.label == core::includes_label()) continue;
            if (core::find_region(*existing_regions, r.label) == nullptr) {
                refuse(job, r.label, "has no region");
                continue;
            }
            merge_members(job, r.label, r, generated_lines, desired, present);
        }
    } else {
        // The header this source goes with, as it is after its own merge.
        std::string header_include;
        for (const std::string& line : generated_lines) {
            const std::string key = include_key(line);
            if (!key.empty() && key[0] == '"') {
                header_include = key.substr(1, key.size() - 2);
                break;
            }
        }
        std::string header_text;
        if (!header_include.empty()) {
            const std::string path = core::output_path(out_include_, header_include);
            const auto merged = merged_headers_.find(path);
            if (merged != merged_headers_.end()) {
                header_text = merged->second;
            } else if (const std::optional<std::string> on_disk = reader_ ? reader_(path) : std::nullopt) {
                header_text = *on_disk;
            }
        }
        const std::set<std::string> declared = declared_signatures(read_declarations_of(header_include, header_text));

        std::set<std::string> defined;
        for (const Definition& d : definitions_of(existing, generated.path)) defined.insert(d.signature);
        const std::vector<Definition> wanted = definitions_of(generated.content, generated.path);
        for (const Region& r : *wanted_regions) {
            if (r.label == core::includes_label()) continue;
            if (core::find_region(*existing_regions, r.label) == nullptr) {
                refuse(job, r.label, "has no region");
                continue;
            }
            merge_definitions(job, r.label, r, generated_lines, wanted, defined, declared);
        }
    }

    if (job.refused) return std::nullopt;

    if (const Region* includes = core::find_region(*wanted_regions, core::includes_label())) {
        merge_includes(job, generated_lines, *includes);
    }

    // What came out must not be worse than what went in.
    if (job.text != existing && parse_errors(job.text) > parse_errors(existing)) {
        diagnostics.warning(core::code::merge_broke,
                            "merging into " + generated.path + " would leave it with syntax errors it did not have; the new text goes to a .new file instead",
                            job.at());
        return std::nullopt;
    }

    if (kind == FileKind::Header) merged_headers_[generated.path] = job.text;
    core::MergeResult result;
    result.content = job.text;
    result.added_members = job.added;
    return result;
}

}  // namespace mt::backend
