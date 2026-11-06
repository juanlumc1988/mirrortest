#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include "gauge.h"

using namespace testing;

TEST(GaugeTest, ElementsOfOneType) {
    Gauge gauge;
    EXPECT_THAT(gauge.history(), ElementsAre(1, 2, 3));
    EXPECT_THAT(gauge.names(), UnorderedElementsAre("a", "b"));
    EXPECT_THAT(gauge.ratios(), Each(Gt(0.5)));
    EXPECT_THAT(gauge.sizes(), AllOf(SizeIs(2), Contains(3u)));
}

TEST(GaugeTest, ElementsOfMatchers) {
    Gauge gauge;
    EXPECT_THAT(gauge.labels(), ElementsAre(StartsWith("m"), HasSubstr("s")));
    EXPECT_THAT(gauge.table(), ElementsAre(Pair("k", 1u), Pair("j", 2u)));
    EXPECT_THAT(gauge.sorted(), WhenSorted(ElementsAre(1L, 2L)));
    EXPECT_THAT(gauge.rows(), Contains(ElementsAre(1, 2)).Times(2));
}
