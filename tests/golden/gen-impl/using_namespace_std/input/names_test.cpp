#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "names.h"

using namespace std;

TEST(NamesTest, Lists) {
    Names names;
    string first = names.first();
    vector<int> ids = names.ids();
    names.add(first);
    names.assign(ids);
}
