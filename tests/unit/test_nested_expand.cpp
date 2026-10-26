// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A class nested in another, made a class of the model with the name that reaches it from the
// namespace.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/backend/nested_expand.h"
#include "mirrortest/backend/template_expand.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/ir/type.h"

using namespace mt;

namespace {

using Lines = std::vector<std::string>;

ir::Model flat(const std::string& header) {
    core::Diagnostics d;
    return backend::flatten_nested_classes(frontend::read_header({"tree.h", header}, "tree.h", {}, d));
}

Lines class_names(const ir::Model& m) {
    Lines out;
    for (const ir::Class& c : m.classes) out.push_back(c.name);
    return out;
}

const ir::Class& the_class(const ir::Model& m, const char* name) {
    static const ir::Class none;
    for (const ir::Class& c : m.classes) {
        if (c.name == name) return c;
    }
    ADD_FAILURE() << "no class " << name;
    return none;
}

Lines signatures(const ir::Class& c) {
    Lines out;
    for (const ir::Method& m : c.methods) {
        std::string s = m.ret.spelling + " " + m.name + "(";
        for (std::size_t i = 0; i < m.params.size(); ++i) s += (i == 0 ? "" : ", ") + m.params[i].type.spelling;
        out.push_back(s + ")");
    }
    return out;
}

}  // namespace

TEST(NestedExpand, APublicNestedClassFollowsTheClassThatHoldsIt) {
    const ir::Model m = flat("class Tree { public: struct Node { int key() const; }; void walk(); };\nclass Other {};\n");
    EXPECT_EQ(class_names(m), (Lines{"Tree", "Tree::Node", "Other"}));
    EXPECT_EQ(the_class(m, "Tree::Node").enclosing, "Tree");
    EXPECT_EQ(the_class(m, "Tree").enclosing, "") << "only what was nested is marked";
    EXPECT_EQ(the_class(m, "Other").enclosing, "");
}

TEST(NestedExpand, TheNestedClassKeepsItsNamespaceMembersAndKind) {
    const ir::Model m = flat("namespace acme { class Tree { public: struct Node { int key() const; }; }; }\n");
    const ir::Class& node = the_class(m, "Tree::Node");
    EXPECT_EQ(node.ns, Lines{"acme"});
    EXPECT_TRUE(node.is_struct);
    EXPECT_EQ(signatures(node), Lines{"int key()"});
    EXPECT_EQ(node.header, "tree.h");
    EXPECT_EQ(ir::qualified_name(node.ns, node.name), "acme::Tree::Node");
}

TEST(NestedExpand, ClassesNestedInTheNestedOneComeAfterItDepthFirst) {
    const ir::Model m = flat("class A { public: class B { public: class C {}; class D {}; }; class E {}; };\n");
    EXPECT_EQ(class_names(m), (Lines{"A", "A::B", "A::B::C", "A::B::D", "A::E"}));
    EXPECT_EQ(the_class(m, "A::B::C").enclosing, "A::B");
}

TEST(NestedExpand, ANonPublicClassAndWhatIsInsideItAreLeftOut) {
    const ir::Model m = flat(
        "class A { public: class B {}; protected: class P { public: class Deep {}; }; private: struct Q {};\n"
        "public: class R { private: class Hidden { public: class Deeper {}; }; }; };\n");
    EXPECT_EQ(class_names(m), (Lines{"A", "A::B", "A::R"}));
}

TEST(NestedExpand, AStructHoldsPublicNestedClassesByDefaultAndAClassPrivateOnes) {
    const ir::Model m = flat("struct S { struct In {}; };\nclass C { struct In {}; };\n");
    EXPECT_EQ(class_names(m), (Lines{"S", "S::In", "C"}));
}

TEST(NestedExpand, ANestedTemplateIsLeftOut) {
    const ir::Model m = flat("class A { public: template <typename T> struct Rebind { T get() const; }; struct Plain {}; };\n");
    EXPECT_EQ(class_names(m), (Lines{"A", "A::Plain"}));
}

TEST(NestedExpand, TypesOfTheEnclosingClassesAreQualified) {
    const ir::Model m = flat(
        "class Tree { public: struct Node { int key; }; enum class Order { Pre, Post };\n"
        "  class Leaf : public Node { public: explicit Leaf(Order o); Node first(const Node& from, Order o) const; Leaf* self(); }; };\n");
    const ir::Class& leaf = the_class(m, "Tree::Leaf");
    EXPECT_EQ(signatures(leaf), (Lines{" Leaf(Tree::Order)", "Tree::Node first(const Tree::Node&, Tree::Order)", "Tree::Leaf* self()"}));
    ASSERT_EQ(leaf.bases.size(), 1u);
    EXPECT_EQ(leaf.bases[0].name, "Tree::Node");
}

TEST(NestedExpand, AFieldIsQualifiedToo) {
    const ir::Model m = flat("class Tree { public: struct Node {}; struct Holder { Node held; }; };\n");
    ASSERT_EQ(the_class(m, "Tree::Holder").fields.size(), 1u);
    EXPECT_EQ(the_class(m, "Tree::Holder").fields[0].type.spelling, "Tree::Node");
}

TEST(NestedExpand, ATypeTheNestedClassDeclaresItselfIsLeftForThePlanner) {
    // Item is Tree's and Leaf's: inside Leaf it is Leaf's own, which the planner qualifies with Tree::Leaf.
    const ir::Model m = flat("class Tree { public: struct Item {}; class Leaf { public: struct Item {}; Item make() const; }; };\n");
    EXPECT_EQ(signatures(the_class(m, "Tree::Leaf")), Lines{"Item make()"});
}

TEST(NestedExpand, TheNearestClassWinsAndANameQualifiedOnceIsLeftAlone) {
    const ir::Model m = flat(
        "class A { public: struct X {}; class B { public: struct X {}; class C { public: X get() const; }; }; };\n");
    EXPECT_EQ(signatures(the_class(m, "A::B::C")), Lines{"A::B::X get()"});
}

TEST(NestedExpand, ANameAfterAScopeIsNotAType) {
    const ir::Model m = flat("namespace n { struct Node {}; }\nclass Tree { public: struct Node {}; struct Leaf { n::Node other(); Node mine(); }; };\n");
    EXPECT_EQ(signatures(the_class(m, "Tree::Leaf")), (Lines{"n::Node other()", "Tree::Node mine()"}));
}

TEST(NestedExpand, TypesInsideTemplateArgumentsAreQualified) {
    const ir::Model m = flat("class Tree { public: struct Node {}; struct Leaf { std::map<int, Node> nodes() const; }; };\n");
    EXPECT_EQ(signatures(the_class(m, "Tree::Leaf")), Lines{"std::map<int, Tree::Node> nodes()"});
}

TEST(NestedExpand, TheOriginalClassesAreAsTheyWere) {
    const ir::Model m = flat("class Tree { public: struct Node { int key() const; }; };\n");
    const ir::Class& tree = the_class(m, "Tree");
    ASSERT_EQ(tree.nested_classes.size(), 1u);
    EXPECT_EQ(tree.nested_classes[0].name, "Node") << "found by the lookups that walk the nested classes";
}

TEST(NestedExpand, AModelWithoutNestedClassesIsTheSame) {
    const ir::Model m = flat("class A { public: void f(); };\nclass B {};\n");
    EXPECT_EQ(class_names(m), (Lines{"A", "B"}));
}

TEST(NestedExpand, TheNestedClassOfAnInstantiationIsPartOfTheInstance) {
    core::Diagnostics d;
    const ir::Model parsed = frontend::read_header(
        {"box.h", "template <typename T> class Box { public: struct Slot { T value() const; Slot next(Box* owner) const; }; Slot first() const; };\n"}, "box.h", {}, d);
    core::Config config;
    config.template_instantiations = {{"Box", {"int"}}};
    const ir::Model m = backend::flatten_nested_classes(backend::instantiate_templates(parsed, config, d));
    EXPECT_EQ(class_names(m), (Lines{"Box<int>", "Box<int>::Slot"}));
    EXPECT_EQ(signatures(the_class(m, "Box<int>::Slot")), (Lines{"int value()", "Box<int>::Slot next(Box<int>*)"}));
    EXPECT_EQ(signatures(the_class(m, "Box<int>")), Lines{"Slot first()"}) << "its own types are qualified by the planner";
    EXPECT_EQ(the_class(m, "Box<int>::Slot").enclosing, "Box<int>");
}
