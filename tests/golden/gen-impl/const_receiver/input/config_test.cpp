#include <gtest/gtest.h>
#include "config.h"

class ConfigTest : public ::testing::Test {
protected:
    ConfigTest() : config_(3) {}
    const Config config_;
};

TEST_F(ConfigTest, ReadsThroughTheConstMember) {
    EXPECT_EQ(config_.level(), 3);
}

TEST(ConfigLocal, ReadsThroughAConstReference) {
    Config config(1);
    const Config& view = config;
    EXPECT_TRUE(view.valid());
    config.set_level(2);
}
