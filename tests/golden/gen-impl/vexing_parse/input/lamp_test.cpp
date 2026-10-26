#include <gtest/gtest.h>
#include "lamp.h"

TEST(LampTest, Vexed) {
    Lamp lamp();
    Lamp other;
    other.on();
}
