#ifndef MATH_H
#define MATH_H

#include <string>

namespace calc {

int add(int a, int b);
int add(int a, int b);
double add(double a, double b);
long long scale(long long value, int factor = 2);
std::string label(unsigned id) noexcept;
bool operator<(const std::string& name, int rank);
inline int twice(int value) { return 2 * value; }
static inline int thrice(int value) { return 3 * value; }
template <typename T> T identity(T value);

namespace inner {
void ping();
}

}  // namespace calc

int counter();

extern "C" {
int c_version(void);
}

#endif
