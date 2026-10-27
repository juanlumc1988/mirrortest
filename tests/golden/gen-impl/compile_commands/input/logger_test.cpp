#include <gtest/gtest.h>
#include "calculator.h"
#include "logger.h"

TEST(LoggerTest, LogsACalculation) {
    Calculator calculator;
    Logger logger;
    logger.log(calculator);
    EXPECT_EQ(calculator.add(1, 2), 3);
}
