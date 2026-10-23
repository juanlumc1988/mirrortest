// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Parser spike input (docs/design.md, R8). Deliberately not compilable: it
// references classes that do not exist and exercises the shapes the
// frontends must handle. Do not "fix" it.

#include <gtest/gtest.h>
#include "foo.h"

class FooFixture : public ::testing::Test {
protected:
    Foo foo_{3};
    const Foo& cfoo_ = foo_;
};

TEST(FooTest, SizeStartsAtThree) {
    Foo foo(3, "abc");
    EXPECT_EQ(foo.size(), 3u) << "message";
    EXPECT_TRUE(foo.empty());
    EXPECT_STREQ(foo.name(), "x");
    int n = foo.count();
    auto a = foo.count();
    foo.run();
    ns::Bar bar;
    Foo::create(1.0f);
    EXPECT_THROW(foo.parse(""), std::runtime_error);
    ASSERT_NO_THROW({ foo.run(); });
    Foo f2();
    std::unique_ptr<Foo> p = std::make_unique<Foo>(1);
    p->reset();
    // @mt: returns std::vector<int>
    EXPECT_EQ(foo.items(), expected);
}

TEST_F(FooFixture, UsesMember) {
    EXPECT_EQ(cfoo_.size(), -1);
    EXPECT_NEAR(foo_.ratio(), 0.5, 1e-9);
}

TEST_P(FooParam, Works) { EXPECT_EQ(GetParam(), 1); }
INSTANTIATE_TEST_SUITE_P(All, FooParam, ::testing::Values(1, 2, 3));
