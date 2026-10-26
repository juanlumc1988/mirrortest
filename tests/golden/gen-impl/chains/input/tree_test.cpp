#include <gtest/gtest.h>
#include "tree.h"

TEST(TreeTest, FindsANode) {
    Tree tree;
    // @mt: returns Node
    EXPECT_EQ(tree.find(1).name(), std::string("root"));
}
