#include <gtest/gtest.h>
#include "point.h"

TEST(PointTest, HasCoordinates) {
    Point p;
    p.x = 5;
    p.label = "origin";
    double y = p.y;
    EXPECT_EQ(p.x, 5);
    (void)y;
}
