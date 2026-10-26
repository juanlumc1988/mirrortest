#ifndef SENSOR_H
#define SENSOR_H

#include <map>
#include <string>

namespace acme {

class Sensor {
public:
    struct Reading {
        int value;
    };

    virtual ~Sensor() = default;
    virtual int read() = 0;
    virtual void calibrate(double offset, const std::string& unit) = 0;
    virtual std::map<int, std::string> table(const std::map<int, int>& in) const = 0;
    virtual Reading last() const noexcept = 0;
    virtual void touch() & = 0;
    virtual void notify() {}
};

class Probe : public Sensor {
public:
    int read() override;
    virtual double extra(int a, int b) = 0;
};

class Controller {
public:
    explicit Controller(int id);
    int value() const;
};

}  // namespace acme

#endif  // SENSOR_H
