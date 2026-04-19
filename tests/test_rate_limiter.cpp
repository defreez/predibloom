#include <gtest/gtest.h>
#include "api/rate_limiter.hpp"

using namespace predibloom::api;

TEST(RateLimiterTest, TryAcquire_InitiallySucceeds) {
    RateLimiter limiter(1000);
    EXPECT_TRUE(limiter.try_acquire());
}

TEST(RateLimiterTest, TryAcquire_ExhaustsTokens) {
    RateLimiter limiter(5);
    // Consume all 5 tokens
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.try_acquire());
    }
    // Next should fail
    EXPECT_FALSE(limiter.try_acquire());
}

TEST(RateLimiterTest, TryAcquire_MultipleAcquisitions) {
    RateLimiter limiter(100);
    int acquired = 0;
    for (int i = 0; i < 200; ++i) {
        if (limiter.try_acquire()) {
            acquired++;
        }
    }
    // Should get roughly 100 tokens (initial bucket)
    EXPECT_GE(acquired, 100);
    EXPECT_LE(acquired, 110);  // Small tolerance for refill during loop
}

TEST(RateLimiterTest, WaitForToken_Succeeds) {
    RateLimiter limiter(1000);
    // Should return quickly with high RPS
    limiter.wait_for_token();
    // If we get here without hanging, the test passes
    SUCCEED();
}

TEST(RateLimiterTest, TryAcquire_RefillsOverTime) {
    RateLimiter limiter(1000);  // 1000/sec = 1ms per token
    // Exhaust tokens
    for (int i = 0; i < 1000; ++i) {
        limiter.try_acquire();
    }
    EXPECT_FALSE(limiter.try_acquire());

    // Wait a bit for refill
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Should have refilled some tokens
    EXPECT_TRUE(limiter.try_acquire());
}
