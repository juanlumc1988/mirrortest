#pragma once

class Sensor {
public:
    virtual int read() = 0;
};

// The mock the project wrote by hand.
class MockSensor : public Sensor {
public:
    int read() override;
};
