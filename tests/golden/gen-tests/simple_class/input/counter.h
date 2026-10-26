#ifndef COUNTER_H
#define COUNTER_H

class Counter {
public:
    explicit Counter(int initial);
    int value() const;
    void increment();
    void reset();
};

#endif
