#include <gtest/gtest.h>
#include "geometry/point.h"
#include "geometry/line.h"

TEST(ShapesTest, DrawsALine) {
    Point origin;
    Line line(origin, 5);
    EXPECT_EQ(line.length(), 5);
    Canvas canvas;
    canvas.draw(line);
}
