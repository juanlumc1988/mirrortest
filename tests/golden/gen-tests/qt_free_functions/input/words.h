#ifndef WORDS_H
#define WORDS_H

#include <string>

namespace text {

std::string trim(const std::string& value);
std::string join(const std::string& a, const std::string& b);
std::string join(const std::string& a, const std::string& b, const std::string& c);
unsigned count(const std::string& value, char what) noexcept;
void reset();

}  // namespace text

#endif
