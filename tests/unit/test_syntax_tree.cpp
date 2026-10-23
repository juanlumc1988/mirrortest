// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Pins the tree-sitter-cpp node shapes the frontends rely on. These are the
// findings of the parser spike (docs/design.md, "Parser"); if a grammar bump
// changes any of them, this file is where it shows up first, before a
// frontend quietly starts missing things.

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "mirrortest/syntax/tree.h"

using mt::syntax::Node;
using mt::syntax::parse_cpp;
using mt::syntax::Tree;

namespace {

// First node of the given type in a pre-order walk, or a null Node.
Node find_first(Node n, std::string_view type) {
    if (n.is_null() || n.type() == type) {
        return n;
    }
    for (const Node& c : n.named_children()) {
        Node hit = find_first(c, type);
        if (!hit.is_null()) {
            return hit;
        }
    }
    return {};
}

}  // namespace

TEST(SyntaxTree, GTestMacroIsAFunctionDefinition) {
    const Tree tree = parse_cpp("TEST(FooTest, Bar) {\n    foo.run();\n}\n");
    ASSERT_FALSE(tree.has_error());

    const Node def = tree.root().named_child(0);
    ASSERT_EQ(def.type(), "function_definition");

    const Node decl = def.child_by_field("declarator");
    ASSERT_EQ(decl.type(), "function_declarator");
    EXPECT_EQ(decl.child_by_field("declarator").text(), "TEST");

    // Suite and test names arrive as parameter declarations whose "type" is
    // the identifier.
    const Node params = decl.child_by_field("parameters");
    ASSERT_EQ(params.named_child_count(), 2u);
    EXPECT_EQ(params.named_child(0).child_by_field("type").text(), "FooTest");
    EXPECT_EQ(params.named_child(1).child_by_field("type").text(), "Bar");

    EXPECT_EQ(def.child_by_field("body").type(), "compound_statement");
}

TEST(SyntaxTree, MethodCallShape) {
    const Tree tree = parse_cpp("void f() { EXPECT_EQ(foo.size(), 3u); }");
    ASSERT_FALSE(tree.has_error());

    const Node assertion = find_first(tree.root(), "call_expression");
    ASSERT_FALSE(assertion.is_null());
    EXPECT_EQ(assertion.child_by_field("function").text(), "EXPECT_EQ");

    const Node args = assertion.child_by_field("arguments");
    ASSERT_EQ(args.named_child_count(), 2u);

    const Node call = args.named_child(0);
    ASSERT_EQ(call.type(), "call_expression");
    const Node callee = call.child_by_field("function");
    ASSERT_EQ(callee.type(), "field_expression");
    EXPECT_EQ(callee.child_by_field("argument").text(), "foo");
    EXPECT_EQ(callee.child_by_field("field").text(), "size");

    EXPECT_EQ(args.named_child(1).type(), "number_literal");
    EXPECT_EQ(args.named_child(1).text(), "3u");
}

TEST(SyntaxTree, NegativeLiteralIsOneToken) {
    // Not unary minus applied to 1: the literal classifier must accept a sign.
    const Tree tree = parse_cpp("void f() { g(-1); }");
    const Node lit = find_first(tree.root(), "number_literal");
    ASSERT_FALSE(lit.is_null());
    EXPECT_EQ(lit.text(), "-1");
}

TEST(SyntaxTree, ScopeOfQualifiedCallIsAlwaysANamespaceIdentifier) {
    // Foo::create() and ns::create() are indistinguishable syntactically; the
    // test frontend has to decide with what it knows about Foo.
    const Tree tree = parse_cpp("void f() { Foo::create(1.0f); }");
    const Node q = find_first(tree.root(), "qualified_identifier");
    ASSERT_FALSE(q.is_null());
    EXPECT_EQ(q.child_by_field("scope").type(), "namespace_identifier");
    EXPECT_EQ(q.child_by_field("scope").text(), "Foo");
}

TEST(SyntaxTree, MostVexingParseIsAFunctionDeclarator) {
    const Tree tree = parse_cpp("void f() { Foo f2(); }");
    const Node decl = find_first(tree.root(), "declaration");
    ASSERT_FALSE(decl.is_null());
    EXPECT_EQ(decl.child_by_field("declarator").type(), "function_declarator");
}

TEST(SyntaxTree, QtMacrosBreakTheClassUntilBlanked) {
    // Why the frontends blank Qt macros before parsing: left alone, Q_OBJECT
    // and "slots" swallow the first slot declaration into an ERROR node.
    const std::string raw =
        "class TestFoo : public QObject {\n"
        "    Q_OBJECT\n"
        "private slots:\n"
        "    void initTestCase();\n"
        "};\n";
    EXPECT_TRUE(parse_cpp(raw).has_error());

    // Same bytes, macros replaced by spaces of the same length: clean parse,
    // identical line/column positions.
    const std::string blanked =
        "class TestFoo : public QObject {\n"
        "            \n"
        "private      :\n"
        "    void initTestCase();\n"
        "};\n";
    ASSERT_EQ(raw.size(), blanked.size());
    const Tree tree = parse_cpp(blanked);
    EXPECT_FALSE(tree.has_error());
    const Node slot = find_first(tree.root(), "function_declarator");
    ASSERT_FALSE(slot.is_null());
    EXPECT_EQ(slot.child_by_field("declarator").text(), "initTestCase");
    EXPECT_EQ(slot.line(), 4u);
}

TEST(SyntaxTree, ErrorsAreToleratedNotFatal) {
    const Tree tree = parse_cpp("TEST(A, B) { int x = ; foo.bar(1); }");
    EXPECT_TRUE(tree.has_error());
    // Recovery keeps going: the call after the broken statement survives.
    const Node call = find_first(tree.root(), "field_expression");
    ASSERT_FALSE(call.is_null());
    EXPECT_EQ(call.child_by_field("field").text(), "bar");
}

TEST(SyntaxTree, PositionsAreOneBased) {
    const Tree tree = parse_cpp("\n  int x;");
    const Node decl = tree.root().named_child(0);
    EXPECT_EQ(decl.line(), 2u);
    EXPECT_EQ(decl.column(), 3u);
}

TEST(SyntaxTree, NodesSurviveMovingTheTree) {
    Tree a = parse_cpp("int value;");
    const Node before = a.root().named_child(0);
    Tree b = std::move(a);
    EXPECT_EQ(b.root().named_child(0), before);
    EXPECT_EQ(before.text(), "int value;");
}

TEST(SyntaxTree, NullNodeIsSafe) {
    const Node n;
    EXPECT_TRUE(n.is_null());
    EXPECT_EQ(n.type(), "");
    EXPECT_EQ(n.text(), "");
    EXPECT_EQ(n.child_count(), 0u);
    EXPECT_TRUE(n.child_by_field("body").is_null());
    EXPECT_TRUE(n.parent().is_null());
}
