#pragma once

namespace vendor {
class Base;
}

class Device : public vendor::Base {
public:
    virtual int open() = 0;
    virtual bool operator==(const Device& other) const = 0;
    virtual operator bool() const = 0;
    virtual void log(const char* format, ...) = 0;
};

template <typename T>
class Channel {
public:
    virtual T receive() = 0;
};

class OnlyAnOperator {
public:
    virtual bool operator<(const OnlyAnOperator& other) const = 0;
};
