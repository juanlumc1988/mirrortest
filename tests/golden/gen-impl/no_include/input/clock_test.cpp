#include <gtest/gtest.h>

TEST(ClockTest, Ticks) {
    HighResClock clock;
    EXPECT_EQ(clock.ticks(), 0u);
    EXPECT_EQ(now_ms(), 0);
}
