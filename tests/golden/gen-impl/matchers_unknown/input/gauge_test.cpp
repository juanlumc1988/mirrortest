#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "gauge.h"

using namespace testing;

TEST(GaugeTest, MatchersThatDoNotSayTheType) {
    Gauge gauge;
    EXPECT_THAT(gauge.sizes(), AllOf(SizeIs(2), IsEmpty()));
    EXPECT_THAT(gauge.mixed(), ElementsAre(1, 2.5));
    EXPECT_THAT(gauge.odd(), ElementsAre(IsOdd(), IsEven()));
    EXPECT_THAT(gauge.count(), Eq(3u));
}
