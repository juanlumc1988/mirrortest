#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "pump.h"
#include "valve.h"

class MockValve : public Valve {
public:
    MOCK_METHOD(void, open, (), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, is_open, (), (const, override));
};

TEST(PumpTest, OpensTheValveWhenStarted) {
    MockValve valve;
    EXPECT_CALL(valve, open());
    Pump pump(&valve);
    pump.start();
    EXPECT_TRUE(pump.running());
}
