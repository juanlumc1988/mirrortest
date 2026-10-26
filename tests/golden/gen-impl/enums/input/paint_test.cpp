#include <gtest/gtest.h>
#include "color.h"
#include "paint.h"

namespace acme {

TEST(PaintTest, ThePenStartsBlack) {
    Paint paint;
    EXPECT_EQ(paint.color(), Color::Black);
}

TEST(PaintTest, TheColourAndTheModeAreSet) {
    Paint paint;
    paint.set(Color::Red);
    paint.set_mode(Mode::Fast);
    EXPECT_EQ(paint.color(), Color::Red);
}

TEST(PaintTest, EveryColourHasAName) {
    Paint paint;
    Color c = Color::Blue;
    EXPECT_STREQ(paint.name(c), "blue");
}

}  // namespace acme
