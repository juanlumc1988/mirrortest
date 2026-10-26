#include <gtest/gtest.h>
#include "repo.h"

// @mt: Repo::count returns std::size_t
// @mt: Repo::name const
// @mt: class Cache header "cache.h"

TEST(RepoTest, Annotated) {
    Repo repo;
    // @mt: returns std::vector<int>
    auto ids = repo.ids();
    helper(repo);  // @mt: ignore
    repo.count();
    std::string label = repo.name();
    (void)label;
    Cache cache;
    cache.drop();
}
