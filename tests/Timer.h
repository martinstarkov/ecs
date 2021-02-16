#pragma once

#include <chrono> // std::chrono
#include <cstdint> // std::int64_t

// Monotonic clock to prevent time variations if system time is changed.
// With modifications to: https://gist.github.com/mcleary/b0bf4fa88830ff7c882d

class Timer {
public:
    Timer() = default;
	~Timer() = default;
    // This works if wanting to restart an active timer.
    void Start() {
        start_time_ = std::chrono::steady_clock::now();
        running_ = true;
    }
    void Stop() {
        stop_time_ = std::chrono::steady_clock::now();
        running_ = false;
    }
    template <typename Type = std::int64_t>
    Type ElapsedMilliseconds() const {
        return ElapsedTime<Type, std::chrono::milliseconds::period>().count();
    }
    template <typename Type = double>
    Type ElapsedSeconds() const {
        return ElapsedTime<Type, std::chrono::seconds::period>().count();
    }
private:
    template <typename Type, typename Ratio>
    std::chrono::duration<Type, Ratio> ElapsedTime() const {
        std::chrono::time_point<std::chrono::steady_clock> end_time;
        if (running_) {
            end_time = std::chrono::steady_clock::now();
        } else {
            end_time = stop_time_;
        }
        return std::chrono::duration_cast<std::chrono::duration<Type, Ratio>>(end_time - start_time_);
    }
    std::chrono::time_point<std::chrono::steady_clock> start_time_;
    std::chrono::time_point<std::chrono::steady_clock> stop_time_;
    bool running_ = false;
};