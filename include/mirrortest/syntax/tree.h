// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A thin, owning C++ wrapper over the tree-sitter C API, with the C++
// grammar baked in. This is the only header that includes tree_sitter/api.h;
// frontends see Tree and Node and nothing else.
//
// Why tree-sitter (see docs/design.md, "Parser"): gen-impl reads tests whose
// classes do not exist yet, so the input does not compile by definition and
// a semantic parser fails exactly on the use case. tree-sitter is syntactic
// and error-tolerant: it always produces a tree, marks what it could not
// make sense of with ERROR nodes, and carries on.
//
// Ownership: a Tree owns the parse tree and a copy of the source text. Nodes
// are cheap value handles into it, valid for as long as the Tree lives. The
// source lives behind a unique_ptr so moving a Tree never moves the text a
// Node's string_view points into.

#ifndef MT_SYNTAX_TREE_H
#define MT_SYNTAX_TREE_H

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

namespace mt::syntax {

class Node {
public:
    Node() = default;
    Node(TSNode node, const std::string* source) : node_(node), source_(source) {}

    bool is_null() const { return source_ == nullptr || ts_node_is_null(node_); }

    std::string_view type() const;      // grammar symbol: "call_expression"
    bool is_named() const;
    bool is_error() const;              // an ERROR node itself
    bool is_missing() const;            // a token the parser inserted to recover
    bool has_error() const;             // this node or any descendant

    std::string_view text() const;      // the exact source bytes it spans
    unsigned start_byte() const;
    unsigned end_byte() const;
    unsigned line() const;              // 1-based
    unsigned column() const;            // 1-based, in bytes

    unsigned child_count() const;
    Node child(unsigned index) const;
    unsigned named_child_count() const;
    Node named_child(unsigned index) const;
    std::vector<Node> named_children() const;

    // The child stored under a grammar field ("declarator", "body", ...),
    // or a null Node.
    Node child_by_field(std::string_view field) const;
    // The field name under which child(index) is stored, or "" if none.
    std::string_view field_name_for_child(unsigned index) const;

    Node parent() const;
    Node next_named_sibling() const;
    Node prev_named_sibling() const;

    // S-expression of the subtree, as tree-sitter prints it. For tests and
    // for debugging the frontends; not a stable format.
    std::string sexp() const;

    friend bool operator==(const Node& a, const Node& b) {
        return a.source_ == b.source_ && ts_node_eq(a.node_, b.node_);
    }
    friend bool operator!=(const Node& a, const Node& b) { return !(a == b); }

private:
    TSNode node_{};
    const std::string* source_ = nullptr;
};

class Tree {
public:
    Tree(Tree&&) noexcept = default;
    Tree& operator=(Tree&&) noexcept = default;
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree() = default;

    Node root() const;
    const std::string& source() const { return *source_; }
    bool has_error() const { return root().has_error(); }

private:
    friend Tree parse_cpp(std::string source);

    struct TreeDeleter {
        void operator()(TSTree* t) const { ts_tree_delete(t); }
    };

    Tree(std::unique_ptr<const std::string> source, std::unique_ptr<TSTree, TreeDeleter> tree)
        : source_(std::move(source)), tree_(std::move(tree)) {}

    std::unique_ptr<const std::string> source_;
    std::unique_ptr<TSTree, TreeDeleter> tree_;
};

// Parses C++ source text. Never fails on malformed input -- that yields a
// tree with ERROR nodes. Throws std::runtime_error only if the parser itself
// cannot be set up (a grammar/runtime ABI mismatch, i.e. a broken build).
Tree parse_cpp(std::string source);

}  // namespace mt::syntax

#endif  // MT_SYNTAX_TREE_H
