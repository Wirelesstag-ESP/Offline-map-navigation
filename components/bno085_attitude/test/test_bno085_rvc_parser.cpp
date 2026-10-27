#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bno085_rvc_parser.hpp"

namespace {

constexpr uint8_t kDatasheetFrame[] = {
    0xAA, 0xAA, 0xDE, 0x01, 0x00, 0x92, 0xFF, 0x25, 0x08, 0x8D,
    0xFE, 0xEC, 0xFF, 0xD1, 0x03, 0x00, 0x00, 0x00, 0xE7,
};

int s_failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++s_failures;
    }
}

size_t feed(Bno085RvcParser &parser, const uint8_t *bytes, size_t length,
            bno085_rvc_sample_t *sample)
{
    size_t decoded = 0;
    for (size_t i = 0; i < length; ++i) {
        if (parser.push(bytes[i], sample)) {
            ++decoded;
        }
    }
    return decoded;
}

void test_datasheet_sample()
{
    Bno085RvcParser parser;
    bno085_rvc_sample_t sample = {};

    expect(feed(parser, kDatasheetFrame, sizeof(kDatasheetFrame), &sample) == 1,
           "datasheet frame should decode");
    expect(std::fabs(sample.yaw_deg - 0.01f) < 0.001f, "yaw should match datasheet");
    expect(std::fabs(sample.pitch_deg + 1.10f) < 0.001f, "pitch should match datasheet");
    expect(std::fabs(sample.roll_deg - 20.85f) < 0.001f, "roll should match datasheet");
    expect(parser.rejected_frames() == 0, "valid frame should not be rejected");
}

void test_bad_checksum_and_garbage()
{
    Bno085RvcParser parser;
    bno085_rvc_sample_t sample = {};
    const uint8_t garbage[] = {0x01, 0x55, 0xAA, 0x03};
    uint8_t bad_frame[sizeof(kDatasheetFrame)] = {};
    std::memcpy(bad_frame, kDatasheetFrame, sizeof(bad_frame));
    bad_frame[sizeof(bad_frame) - 1] ^= 0x01;

    expect(feed(parser, garbage, sizeof(garbage), &sample) == 0,
           "garbage should not decode");
    expect(feed(parser, bad_frame, sizeof(bad_frame), &sample) == 0,
           "bad checksum should not decode");
    expect(parser.rejected_frames() == 1, "bad checksum should increment rejection count");
    expect(feed(parser, kDatasheetFrame, sizeof(kDatasheetFrame), &sample) == 1,
           "parser should recover after a bad frame");
}

void test_consecutive_frames()
{
    Bno085RvcParser parser;
    bno085_rvc_sample_t sample = {};

    expect(feed(parser, kDatasheetFrame, sizeof(kDatasheetFrame), &sample) == 1,
           "first consecutive frame should decode");
    expect(feed(parser, kDatasheetFrame, sizeof(kDatasheetFrame), &sample) == 1,
           "second consecutive frame should decode");
}

void test_dropped_byte_resynchronizes()
{
    Bno085RvcParser parser;
    bno085_rvc_sample_t sample = {};

    uint8_t stream[sizeof(kDatasheetFrame) * 2 - 1] = {};
    constexpr size_t kDroppedIndex = 8;
    std::memcpy(stream, kDatasheetFrame, kDroppedIndex);
    std::memcpy(stream + kDroppedIndex, kDatasheetFrame + kDroppedIndex + 1,
                sizeof(kDatasheetFrame) - kDroppedIndex - 1);
    std::memcpy(stream + sizeof(kDatasheetFrame) - 1, kDatasheetFrame,
                sizeof(kDatasheetFrame));

    expect(feed(parser, stream, sizeof(stream), &sample) == 1,
           "parser should recover at the next frame after a dropped byte");
    expect(parser.rejected_frames() == 1, "truncated frame should be rejected once");
}

}  // namespace

int main()
{
    test_datasheet_sample();
    test_bad_checksum_and_garbage();
    test_consecutive_frames();
    test_dropped_byte_resynchronizes();

    if (s_failures != 0) {
        std::fprintf(stderr, "%d parser test(s) failed\n", s_failures);
        return 1;
    }
    std::puts("All BNO085 UART-RVC parser tests passed");
    return 0;
}
