#ifndef TOOLS_H
#define TOOLS_H

#include <string>

namespace acme {

template <typename T> T clamp_to(T lo, T hi, T value) { return value < lo ? lo : (hi < value ? hi : value); }

template <typename T, int N = 4> T scaled(T value) noexcept { return value * static_cast<T>(N); }

template <typename K, typename V> V lookup(const K& key, V fallback) {
    (void)key;
    return fallback;
}

template <typename T> void reset(T& target) { target = T(); }

template <typename T> T unlisted(T value) { return value; }

template <typename... Ts> int count_all(Ts... values) { return static_cast<int>(sizeof...(values)); }

int plain(int v);

}  // namespace acme

#endif
