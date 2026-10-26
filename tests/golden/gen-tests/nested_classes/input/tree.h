// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#ifndef TREE_H
#define TREE_H

namespace acme {

class Tree {
public:
    struct Node {
        Node();
        explicit Node(int key);
        int key() const;
    };

    // Uses its sibling Node, derives from it and has a type of its own.
    class Cursor : public Node {
    public:
        struct Step {
            int by;
        };
        Node current() const;
        void advance(const Step& step);
    };

    int size() const;
    void add(const Node& node);

protected:
    // Not public: a test cannot name it.
    class Hidden {
    public:
        void touch();
    };
};

}  // namespace acme

#endif  // TREE_H
