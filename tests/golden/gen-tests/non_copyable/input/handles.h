// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#ifndef HANDLES_H
#define HANDLES_H

namespace acme {

// Owns a descriptor: moved, never copied.
class Handle {
public:
    Handle();
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    int fd() const;
};

// Stays where it was built: neither copied nor moved.
class Pinned : public Handle {
public:
    Pinned();
    Pinned(const Pinned&) = delete;
    Pinned(Pinned&&) = delete;
    Pinned& operator=(const Pinned&) = delete;
    Pinned& operator=(Pinned&&) = delete;
};

// Nothing else to test: the refusal is all there is.
class Lock {
    Lock(const Lock&) = delete;
};

// Defaulted: not refused, so nothing is asserted.
class Plain {
public:
    Plain(const Plain&) = default;
    int size() const;
};

}  // namespace acme

#endif  // HANDLES_H
