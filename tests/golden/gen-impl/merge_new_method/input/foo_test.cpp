#include <gtest/gtest.h>
#include "foo.h"

TEST(FooTest, Size) {
    Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
}

TEST(FooTest, More) {
    Foo foo(3);
    EXPECT_FALSE(foo.empty());
    EXPECT_TRUE(foo.reset());
}
