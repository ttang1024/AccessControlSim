#include "net/frame_codec.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace acs::net {
namespace {

using namespace std::string_literals;

TEST(FrameCodecTest, EncodeFrameHeader_Value_IsBigEndian) {
    EXPECT_EQ(encodeFrameHeader(0x01020304U), (FrameHeader{0x01, 0x02, 0x03, 0x04}));
}

TEST(FrameCodecTest, DecodeFrameHeader_EncodedValue_RoundTrips) {
    for (const std::uint32_t size :
         {0U, 1U, 255U, 256U, kMaxPayloadSize, std::numeric_limits<std::uint32_t>::max()}) {
        EXPECT_EQ(decodeFrameHeader(encodeFrameHeader(size)), size) << size;
    }
}

TEST(FrameCodecTest, EncodeFrame_Payload_PrefixesLength) {
    EXPECT_EQ(encodeFrame("abc"), "\0\0\0\3abc"s);
}

TEST(FrameCodecTest, EncodeFrame_EmptyPayload_IsHeaderOnly) {
    EXPECT_EQ(encodeFrame(""), "\0\0\0\0"s);
}

}  // namespace
}  // namespace acs::net
