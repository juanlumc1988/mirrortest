#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "display.h"
#include "gauge.h"

using ::testing::Return;

// The macros before gmock 1.10, which many code bases still use.
class LegacyGauge : public Gauge {
public:
    MOCK_METHOD0(level, int());
    MOCK_CONST_METHOD0(unit, const char*());
    MOCK_METHOD2(range, void(int low, int high));
};

TEST(DisplayTest, ShowsTheLevel) {
    LegacyGauge gauge;
    EXPECT_CALL(gauge, level()).WillOnce(Return(7));
    Display display(gauge);
    EXPECT_EQ(display.shown(), 7);
}
