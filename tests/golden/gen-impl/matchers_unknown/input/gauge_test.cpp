#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "gauge.h"

using namespace testing;

TEST(GaugeTest, MatchersOverContainersDoNotSayTheType) {
    Gauge gauge;
    EXPECT_THAT(gauge.history(), ElementsAre(1, 2));
    EXPECT_THAT(gauge.sizes(), AllOf(SizeIs(2), Contains(3)));
    EXPECT_THAT(gauge.count(), Eq(3u));
}
