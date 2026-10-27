#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>
#include "gauge.h"

using namespace testing;

TEST(GaugeTest, NumbersAreCompared) {
    Gauge gauge;
    EXPECT_THAT(gauge.count(), Eq(3u));
    EXPECT_THAT(gauge.level(), AllOf(Ge(1.5), Lt(9.5)));
    EXPECT_THAT(gauge.ratio(), DoubleNear(0.5, 0.01));
    EXPECT_THAT(gauge.gain(), FloatEq(2.0f));
}

TEST(GaugeTest, TruthAndPointers) {
    Gauge gauge;
    EXPECT_THAT(gauge.ready(), IsTrue());
    EXPECT_THAT(gauge.stopped(), Not(IsTrue()));
    EXPECT_THAT(gauge.handle(), NotNull());
}

TEST(GaugeTest, Strings) {
    Gauge gauge;
    EXPECT_THAT(gauge.name(), StrEq("gauge"));
    EXPECT_THAT(gauge.label(), HasSubstr("ga"));
    EXPECT_THAT(gauge.unit(), AllOf(StartsWith("m"), EndsWith("s")));
    // @mt: returns std::vector<int>
    EXPECT_THAT(gauge.history(), ElementsAre(1, 2));
}
