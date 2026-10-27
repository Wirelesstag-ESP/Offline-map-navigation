#include "bno085_rvc_parser.hpp"

#include <cstring>

namespace {

int16_t read_i16_le(const uint8_t *value)
{
    return static_cast<int16_t>(static_cast<uint16_t>(value[0]) |
                                (static_cast<uint16_t>(value[1]) << 8));
}

}  // namespace

bool Bno085RvcParser::push(uint8_t byte, bno085_rvc_sample_t *sample)
{
    if (!sample) {
        reset();
        return false;
    }

    if (length_ == 0) {
        if (byte == 0xAA) {
            frame_[length_++] = byte;
        }
        return false;
    }

    if (length_ == 1) {
        if (byte == 0xAA) {
            frame_[length_++] = byte;
        } else {
            reset();
        }
        return false;
    }

    frame_[length_++] = byte;
    if (length_ < kFrameSize) {
        return false;
    }

    uint8_t checksum = 0;
    for (size_t i = 2; i < kFrameSize - 1; ++i) {
        checksum = static_cast<uint8_t>(checksum + frame_[i]);
    }

    const bool valid = checksum == frame_[kFrameSize - 1];
    if (valid) {
        sample->yaw_deg = static_cast<float>(read_i16_le(&frame_[3])) * 0.01f;
        sample->pitch_deg = static_cast<float>(read_i16_le(&frame_[5])) * 0.01f;
        sample->roll_deg = static_cast<float>(read_i16_le(&frame_[7])) * 0.01f;
        reset();
    } else {
        ++rejected_frames_;

        // A dropped or inserted byte can make this frame consume the start of
        // the next one. Preserve the newest embedded header so parsing resumes
        // without discarding another complete report.
        size_t header = kFrameSize;
        for (size_t i = 1; i + 1 < kFrameSize; ++i) {
            if (frame_[i] == 0xAA && frame_[i + 1] == 0xAA) {
                header = i;
            }
        }

        if (header < kFrameSize) {
            length_ = kFrameSize - header;
            memmove(frame_, &frame_[header], length_);
        } else if (frame_[kFrameSize - 1] == 0xAA) {
            frame_[0] = 0xAA;
            length_ = 1;
        } else {
            reset();
        }
    }
    return valid;
}

void Bno085RvcParser::reset()
{
    length_ = 0;
}
