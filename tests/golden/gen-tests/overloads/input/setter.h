#include <string>

class Setter {
public:
    void set(int value);
    void set(double value);
    void set(const std::string& value);
    int size();
    int size() const;
};
