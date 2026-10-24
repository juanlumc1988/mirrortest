#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "clock.h"
#include "scheduler.h"
#include "timer.h"

namespace {

class FakeClock : public acme::Clock {
public:
    long now() const override { return now_; }
    void advance(long ticks) override { now_ += ticks; }

private:
    long now_ = 100;
};

}  // namespace

TEST(SchedulerTest, StartsAtTheClocksTime) {
    FakeClock clock;
    acme::Scheduler scheduler(&clock);
    EXPECT_EQ(scheduler.started(), 100);
}

TEST(SchedulerTest, WaitsOnItsClock) {
    FakeClock clock;
    acme::Scheduler scheduler(&clock);
    scheduler.wait(5);
    EXPECT_EQ(clock.now(), 105);
}

TEST(TimerTest, OwnsItsClock) {
    std::unique_ptr<FakeClock> clock(new FakeClock);
    acme::Timer timer(std::move(clock));
    EXPECT_EQ(timer.elapsed(), 0);
}
