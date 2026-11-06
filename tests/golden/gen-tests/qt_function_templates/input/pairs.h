#ifndef PAIRS_H
#define PAIRS_H

namespace acme {

template <typename T> T larger(T a, T b) { return a < b ? b : a; }

template <typename T, int N = 2> T times(T value) noexcept { return value * static_cast<T>(N); }

}  // namespace acme

#endif
