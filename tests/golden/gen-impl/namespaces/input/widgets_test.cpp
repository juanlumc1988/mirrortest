#include <gtest/gtest.h>
#include "acme/widgets.h"

using namespace acme::ui;

namespace acme {
namespace core {
TEST(CoreTest, BuildsAnEngine) {
    Engine engine(2);
    EXPECT_EQ(engine.cylinders(), 2);
}
}  // namespace core
}  // namespace acme

TEST(UiTest, BuildsAButton) {
    Button button("ok");
    EXPECT_TRUE(button.enabled());
    acme::ui::Label label;
    label.clear();
}
