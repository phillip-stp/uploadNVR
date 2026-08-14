#pragma once

#include <vector>
#include <cstdint>

struct FrameData {
    std::vector<uint8_t> pixels;
    int64_t timestamp;
    uint8_t camera_id;

    static constexpr uint16_t width = 300;
    static constexpr uint16_t height = 300;
};