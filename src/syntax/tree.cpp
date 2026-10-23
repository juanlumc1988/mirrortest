// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/syntax/tree.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>

extern "C" const TSLanguage* tree_sitter_cpp(void);

namespace mt::syntax {

std::string_view Node::type() const {
    return is_null() ? std::string_view() : std::string_view(ts_node_type(node_));
}

bool Node::is_named() const   { return !is_null() && ts_node_is_named(node_); }
bool Node::is_error() const   { return !is_null() && ts_node_is_error(node_); }
bool Node::is_missing() const { return !is_null() && ts_node_is_missing(node_); }
bool Node::has_error() const  { return !is_null() && ts_node_has_error(node_); }

std::string_view Node::text() const {
    if (is_null()) {
        return {};
    }
    const unsigned begin = ts_node_start_byte(node_);
    const unsigned end = ts_node_end_byte(node_);
    return std::string_view(*source_).substr(begin, end - begin);
}

unsigned Node::start_byte() const { return is_null() ? 0u : ts_node_start_byte(node_); }
unsigned Node::end_byte() const   { return is_null() ? 0u : ts_node_end_byte(node_); }
unsigned Node::line() const       { return is_null() ? 0u : ts_node_start_point(node_).row + 1u; }
unsigned Node::column() const     { return is_null() ? 0u : ts_node_start_point(node_).column + 1u; }

unsigned Node::child_count() const { return is_null() ? 0u : ts_node_child_count(node_); }

Node Node::child(unsigned index) const {
    return is_null() ? Node() : Node(ts_node_child(node_, index), source_);
}

unsigned Node::named_child_count() const {
    return is_null() ? 0u : ts_node_named_child_count(node_);
}

Node Node::named_child(unsigned index) const {
    return is_null() ? Node() : Node(ts_node_named_child(node_, index), source_);
}

std::vector<Node> Node::named_children() const {
    std::vector<Node> out;
    const unsigned n = named_child_count();
    out.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        out.push_back(named_child(i));
    }
    return out;
}

Node Node::child_by_field(std::string_view field) const {
    if (is_null()) {
        return {};
    }
    return Node(ts_node_child_by_field_name(node_, field.data(),
                                            static_cast<uint32_t>(field.size())),
                source_);
}

std::string_view Node::field_name_for_child(unsigned index) const {
    if (is_null()) {
        return {};
    }
    const char* name = ts_node_field_name_for_child(node_, index);
    return name == nullptr ? std::string_view() : std::string_view(name);
}

Node Node::parent() const {
    return is_null() ? Node() : Node(ts_node_parent(node_), source_);
}

Node Node::next_named_sibling() const {
    return is_null() ? Node() : Node(ts_node_next_named_sibling(node_), source_);
}

Node Node::prev_named_sibling() const {
    return is_null() ? Node() : Node(ts_node_prev_named_sibling(node_), source_);
}

std::string Node::sexp() const {
    if (is_null()) {
        return {};
    }
    char* raw = ts_node_string(node_);
    std::string out(raw);
    std::free(raw);
    return out;
}

Node Tree::root() const {
    return Node(ts_tree_root_node(tree_.get()), source_.get());
}

Tree parse_cpp(std::string source) {
    if (source.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("mirrortest: input larger than 4 GiB");
    }

    struct ParserDeleter {
        void operator()(TSParser* p) const { ts_parser_delete(p); }
    };
    std::unique_ptr<TSParser, ParserDeleter> parser(ts_parser_new());
    if (!parser || !ts_parser_set_language(parser.get(), tree_sitter_cpp())) {
        throw std::runtime_error(
            "mirrortest: tree-sitter rejected the C++ grammar (runtime/grammar ABI mismatch)");
    }

    auto text = std::make_unique<const std::string>(std::move(source));
    std::unique_ptr<TSTree, Tree::TreeDeleter> tree(ts_parser_parse_string(
        parser.get(), nullptr, text->data(), static_cast<uint32_t>(text->size())));
    if (!tree) {
        throw std::runtime_error("mirrortest: tree-sitter returned no tree");
    }
    return Tree(std::move(text), std::move(tree));
}

}  // namespace mt::syntax
