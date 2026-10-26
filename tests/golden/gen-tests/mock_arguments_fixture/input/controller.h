#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <memory>
#include <string>

namespace acme {

class Sensor {
public:
    virtual ~Sensor() = default;
    virtual int read() = 0;
};

class Logger {
public:
    virtual ~Logger() = default;
    virtual void log(const std::string& line) = 0;
};

class Controller {
public:
    Controller(Sensor& sensor, Logger* logger);
    explicit Controller(std::unique_ptr<Sensor> owned);
    int value() const;
    void attach(const std::shared_ptr<Logger>& logger, const Sensor* extra, int times);
};

}  // namespace acme

#endif  // CONTROLLER_H
