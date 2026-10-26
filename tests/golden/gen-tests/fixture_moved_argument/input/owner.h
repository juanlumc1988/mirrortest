#pragma once
#include <string>

class Owner {
public:
    explicit Owner(std::string&& name);
    void rename();
};
