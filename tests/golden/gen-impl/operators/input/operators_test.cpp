#include <gtest/gtest.h>
#include "grid.h"
#include "point.h"
#include "sensor.h"

TEST(PointTest, EqualPointsAreEqual) {
    Point a(1, 2);
    Point b(1, 2);
    EXPECT_TRUE(a == b);
    EXPECT_EQ(a, b);
}

TEST(PointTest, PointsAreOrdered) {
    Point a(1, 2);
    Point b(3, 4);
    EXPECT_TRUE(a < b);
    EXPECT_NE(a, b);
    EXPECT_GE(b, a);
}

TEST(PointTest, AxesAreReadByIndex) {
    Point p(1, 2);
    EXPECT_EQ(p[0], 1);
    EXPECT_EQ(p[1], 2);
}

TEST(GridTest, ACellIsWrittenByIndex) {
    Grid grid;
    grid[3] = 7;
    EXPECT_EQ(grid[3], 7);
}

TEST(SensorTest, ThePositionIsComparedWithAPoint) {
    Sensor sensor;
    Point origin(0, 0);
    EXPECT_EQ(sensor.position(), origin);
}
