#include <gtest/gtest.h>
#include "stack.h"

class StackTest : public ::testing::Test {
protected:
    void SetUp() override {
        stack_.push(1);
        stack_.push(2);
    }
    Stack stack_;
};

TEST_F(StackTest, PopReturnsTheLastPushed) {
    EXPECT_EQ(stack_.pop(), 2);
    EXPECT_EQ(stack_.size(), 1u);
}

TEST_F(StackTest, IsNotEmpty) {
    EXPECT_FALSE(stack_.empty());
}
