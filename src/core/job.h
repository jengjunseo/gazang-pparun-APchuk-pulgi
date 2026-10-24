#pragma once
#include <atomic>
#include <functional>
#include <cstdint>
namespace hx {
struct JobControl {
    std::atomic<bool> cancelled{false};
    std::atomic<uint64_t> completed{0}, total{0};
};
}
