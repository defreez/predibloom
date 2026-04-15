#pragma once

#include <chrono>
#include <mutex>
#include <thread>

namespace predibloom::api {

class RateLimiter {
public:
    explicit RateLimiter(int requests_per_second = 10)
        : max_tokens_(requests_per_second)
        , tokens_(requests_per_second)
        , refill_interval_(std::chrono::milliseconds(1000 / requests_per_second))
        , last_refill_(std::chrono::steady_clock::now()) {}

    void wait_for_token() {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();

        while (tokens_ < 1.0) {
            auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                refill_interval_ * (1.0 - tokens_));
            mutex_.unlock();
            std::this_thread::sleep_for(wait_time);
            mutex_.lock();
            refill();
        }

        tokens_ -= 1.0;
    }

    bool try_acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        refill();

        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        return false;
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
        tokens_ = std::min(static_cast<double>(max_tokens_),
                          tokens_ + elapsed * max_tokens_);
        last_refill_ = now;
    }

    int max_tokens_;
    double tokens_;
    std::chrono::milliseconds refill_interval_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

} // namespace predibloom::api
