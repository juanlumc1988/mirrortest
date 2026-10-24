#include <gtest/gtest.h>
#include <cstring>
#include <algorithm>
#include "mathx.h"

TEST(MathTest, Clamps) {
    EXPECT_EQ(clamp(5, 0, 3), 3);
    EXPECT_EQ(lerp(0.0, 1.0, 0.5), 0.5);
    EXPECT_TRUE(is_even(4));
    EXPECT_EQ(strlen("abc"), 3u);
    EXPECT_EQ(std::max(1, 2), 2);
    MY_ASSERT_OK(is_even(2));
}
