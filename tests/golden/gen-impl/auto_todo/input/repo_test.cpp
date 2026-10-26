#include <gtest/gtest.h>
#include "repo.h"

TEST(RepoTest, Finds) {
    Repo repo;
    auto item = repo.find(1);
    EXPECT_TRUE(item.valid());
}
