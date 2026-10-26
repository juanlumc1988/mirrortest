#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "inventory.h"

TEST(InventoryTest, ListsItsItems) {
    Inventory inventory;
    std::vector<std::string> names = inventory.names();
    std::size_t count = inventory.count();
    EXPECT_EQ(count, names.size());
}
