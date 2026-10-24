#include <gtest/gtest.h>
#include "acme/core/engine.h"

TEST(EngineTest, Starts) {
    acme::core::Engine engine;
    engine.start();
    EXPECT_TRUE(engine.running());
}
