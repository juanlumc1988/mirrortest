#include <gtest/gtest.h>
#include "foo.h"

TEST(FooTest, Size) {
    Foo foo(3);
    EXPECT_EQ(foo.size(), 3u);
    foo.run();
    // @mt: returns std::runtime_error
    auto m = foo.mystery();
    (void)m;
}
