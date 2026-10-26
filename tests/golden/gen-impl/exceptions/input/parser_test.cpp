#include <gtest/gtest.h>
#include "parse_error.h"
#include "parser.h"

namespace acme {

TEST(ParserTest, BadInputIsRejected) {
    Parser parser;
    EXPECT_THROW(parser.parse("x"), ParseError);
    EXPECT_THROW(parser.parse(""), EmptyInput);
    EXPECT_THROW(parser.parse("x"), std::runtime_error);
}

TEST(ParserTest, GoodInputIsParsed) {
    Parser parser;
    EXPECT_EQ(parser.parse("1"), 1);
    EXPECT_NO_THROW(parser.parse("2"));
}

}  // namespace acme
