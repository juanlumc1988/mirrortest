#include <gtest/gtest.h>
#include <memory>
#include "widget.h"

TEST(WidgetTest, ThroughPointers) {
    std::unique_ptr<Widget> owned = std::make_unique<Widget>(3);
    EXPECT_EQ(owned->width(), 3);
    auto shared = std::make_shared<Widget>(4);
    shared->resize(5);
    Widget* raw = new Widget(6);
    EXPECT_TRUE(raw->visible());
    delete raw;
}
