#ifndef CALC_H
#define CALC_H

#include <map>
#include <memory>
#include <string>

namespace acme {

class Calc {
public:
    int add(int a, int b) const;
    void set(const std::string& name, double value, const char* unit);
    static int twice(int x);
    int none() const;
    void take(std::string&& s);
    void own(std::unique_ptr<int> p);
    void fill(std::map<int, int> m);
};

}  // namespace acme

#endif  // CALC_H
