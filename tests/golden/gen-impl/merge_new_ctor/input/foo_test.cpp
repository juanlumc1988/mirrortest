#include <gtest/gtest.h>
#include "foo.h"

TEST(FooTest, Size) {
    Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
}

TEST(FooTest, Pair) {
    Foo pair(3, 4);
    Foo plain;
    (void)pair;
    (void)plain;
}
