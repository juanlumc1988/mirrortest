#include <gtest/gtest.h>
#include "registry.h"

TEST(RegistryTest, IsASingleton) {
    EXPECT_EQ(Registry::count(), 0u);
    Registry::add("first");
    Registry& registry = Registry::instance();
    registry.clear();
}
