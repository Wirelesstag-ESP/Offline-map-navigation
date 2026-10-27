#pragma once

#include <cstddef>
#include <cstdint>

struct bno085_rvc_sample_t {
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
};

class Bno085RvcParser {
public:
    bool push(uint8_t byte, bno085_rvc_sample_t *sample);
    void reset();
    uint32_t rejected_frames() const { return rejected_frames_; }

private:
    static constexpr size_t kFrameSize = 19;
    uint8_t frame_[kFrameSize] = {};
    size_t length_ = 0;
    uint32_t rejected_frames_ = 0;
};
