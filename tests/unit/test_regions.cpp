// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The region markers: one spelling, shared by what writes them and what finds
// them again.

#include <gtest/gtest.h>

#include "mirrortest/core/regions.h"

using namespace mt;

namespace {

TEST(Regions, MarkersAreExactlyTheSpelledOnes) {
    EXPECT_EQ(core::region_begin("acme::Foo"), "// <mt:begin acme::Foo>");
    EXPECT_EQ(core::region_end("acme::Foo"), "// <mt:end acme::Foo>");
    EXPECT_EQ(core::region_begin("includes"), "// <mt:begin includes>");
}

TEST(Regions, LabelsNameTheOwner) {
    EXPECT_EQ(core::includes_label(), "includes");
    EXPECT_EQ(core::functions_label({}), "functions");
    EXPECT_EQ(core::functions_label({"acme", "util"}), "acme::util::functions");
    EXPECT_EQ(core::class_label({}, "Foo"), "Foo");
    EXPECT_EQ(core::class_label({"acme"}, "Foo"), "acme::Foo");
}

TEST(Regions, TheLabelsOfOneFileDoNotCollideUnlessTheUserNamesAClassLikeARegion) {
    // Two namespaces' free functions, a class and the includes: four regions.
    EXPECT_NE(core::functions_label({"a"}), core::functions_label({"b"}));
    EXPECT_NE(core::functions_label({}), core::functions_label({"a"}));
    EXPECT_NE(core::class_label({"a"}, "Foo"), core::class_label({"b"}, "Foo"));
    // The one case that does: `class functions` in the global namespace.
    EXPECT_EQ(core::class_label({}, "functions"), core::functions_label({}));
}

}  // namespace
