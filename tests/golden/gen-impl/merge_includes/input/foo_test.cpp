#include <gtest/gtest.h>
#include "foo.h"

TEST(FooTest, Size) {
    Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
    std::vector<int> ids = foo.ids();
    std::string name = foo.name();
    (void)ids;
    (void)name;
}
