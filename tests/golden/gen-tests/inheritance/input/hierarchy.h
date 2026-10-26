// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#ifndef HIERARCHY_H
#define HIERARCHY_H

#include <string>

namespace acme {

class Named {
public:
    std::string name() const;
};

class Marker {
public:
    bool marked() const;
};

template <typename T>
class Counted {
public:
    int count() const { return 0; }
};

// Several bases, each with its own access, one of them virtual and one with template arguments:
// the test says that the class derives from every one of them, whatever the access.
class Leaf : public Named, protected Counted<int>, private virtual Marker {
public:
    Leaf();
    int depth() const;
};

// Nothing public of its own: the hierarchy is all there is to check.
struct Tag : Named {};

// No bases, no Inherits.
class Plain {
public:
    int size() const;
};

}  // namespace acme

#endif  // HIERARCHY_H
