#include <gtest/gtest.h>
#include "counter.h"

TEST(CounterTest, StartsAtTheInitialValue) {
    Counter counter(5);
    EXPECT_EQ(counter.value(), 5);
    EXPECT_TRUE(counter.enabled());
    EXPECT_FALSE(counter.overflowed());
    EXPECT_STREQ(counter.name(), "counter");
    counter.reset();
}
