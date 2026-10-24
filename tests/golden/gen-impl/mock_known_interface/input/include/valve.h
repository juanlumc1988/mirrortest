#ifndef VALVE_H
#define VALVE_H

class Valve {
public:
    virtual ~Valve() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

#endif  // VALVE_H
