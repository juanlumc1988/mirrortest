#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "repo.h"

TEST(RepoTest, UsesDeclaredTypes) {
    Repo repo;
    std::string name = "main";
    std::vector<int> ids = repo.ids();
    int count = repo.count();
    double ratio = repo.ratio();
    repo.rename(name);
    repo.assign(ids);
    repo.limit(count);
    repo.scale(ratio);
}
