#include <gtest/gtest.h>
#include "foo.h"

TEST(FooTest, Size) {
    Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
    EXPECT_EQ(foo.length(), 3u);
    foo.run();
}
