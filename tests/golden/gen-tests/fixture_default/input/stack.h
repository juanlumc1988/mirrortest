#pragma once
#include <string>

class Stack {
public:
    Stack();
    Stack(const Stack& other);
    ~Stack();
    void push(const std::string& item);
    bool empty() const;
    static Stack make();
};
