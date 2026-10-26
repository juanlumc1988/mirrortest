#include <gtest/gtest.h>
#include "meter.h"

TEST(MeterTest, Disagrees) {
    Meter meter;
    EXPECT_EQ(meter.reading(), 3u);
    EXPECT_EQ(meter.reading(), 3);
    meter.calibrate(1);
    meter.calibrate(2.5);
    long total = meter.total();
    EXPECT_EQ(meter.total(), 5u);
    (void)total;
}
