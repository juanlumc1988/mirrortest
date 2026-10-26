#include <gtest/gtest.h>
#include "acme/bar.h"

TEST(BarTest, TakesAFoo) {
    acme::Foo foo(7);
    acme::Bar bar;
    bar.put(acme::Foo(2));
    EXPECT_EQ(bar.count(), 1u);
}
