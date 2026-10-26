#ifndef BOX_H
#define BOX_H

#include <string>

namespace acme {

template <typename T, int N = 4>
class Box {
public:
    Box() : value_() {}
    explicit Box(T value) : value_(value) {}
    T get(int) const { return value_; }
    void put(const T& value) { value_ = value; }
    Box clone() const { return *this; }
    static int capacity() { return N; }

    struct Item {
        T value;
    };
    Item first() const { return Item{value_}; }

private:
    T value_;
};

// Not listed in template_instantiations: skipped, with the way to list it.
template <typename K, typename V>
class Pair {
public:
    K key() const;
};

}  // namespace acme

#endif  // BOX_H
