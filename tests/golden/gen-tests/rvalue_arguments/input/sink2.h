#include <string>

class Queue {
public:
    void push(std::string&& item);
    void push(const std::string& item);
};
