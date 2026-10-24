#include <gtest/gtest.h>
#include "worker.h"

TEST(WorkerTest, Runs) {
    Worker worker;
    worker.start();
    worker.poll();
    worker.poll();
    EXPECT_TRUE(worker.poll());
    worker.stop();
}
