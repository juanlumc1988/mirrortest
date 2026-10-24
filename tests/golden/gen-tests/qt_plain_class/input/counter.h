#pragma once

class Counter {
public:
    explicit Counter(int initial);
    int value() const;
    void increment();
};
