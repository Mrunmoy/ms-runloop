#include <gtest/gtest.h>
#include "rpc/RingBuffer.h"
#include "rpc/Types.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace rpc;

// Use a small ring buffer for tests (4 KB) to make wraparound easy to trigger.
static constexpr uint32_t kTestSize = 4096;
using TestRing = RingBuffer<kTestSize>;

class RingBufferTest : public ::testing::Test {
protected:
    TestRing ring;
};

// ═══════════════════════════════════════════════════════════════════════
// Test 1: Single write/read — write one frame, read it back, verify
//         contents match.
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, SingleWriteRead) {
    FrameHeader hdr{};
    hdr.version      = PROTOCOL_VERSION;
    hdr.flags        = FRAME_REQUEST;
    hdr.serviceId    = 42;
    hdr.messageId    = 0;
    hdr.seq          = 1;
    hdr.payloadBytes = 4;
    hdr.aux          = 0;

    uint32_t payload = 0xDEADBEEF;

    // Write header + payload
    ASSERT_TRUE(ring.write(&hdr, sizeof(hdr)));
    ASSERT_TRUE(ring.write(&payload, sizeof(payload)));

    // Read header + payload
    FrameHeader readHdr{};
    ASSERT_TRUE(ring.read(&readHdr, sizeof(readHdr)));
    EXPECT_EQ(readHdr.version, PROTOCOL_VERSION);
    EXPECT_EQ(readHdr.flags, FRAME_REQUEST);
    EXPECT_EQ(readHdr.serviceId, 42u);
    EXPECT_EQ(readHdr.seq, 1u);
    EXPECT_EQ(readHdr.payloadBytes, 4u);

    uint32_t readPayload = 0;
    ASSERT_TRUE(ring.read(&readPayload, sizeof(readPayload)));
    EXPECT_EQ(readPayload, 0xDEADBEEF);

    EXPECT_TRUE(ring.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Test 2: Multiple sequential writes — write N frames, read N frames,
//         verify order and contents.
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, MultipleSequentialWriteRead) {
    constexpr int N = 50;

    // Write N frames
    for (int i = 0; i < N; ++i) {
        FrameHeader hdr{};
        hdr.version      = PROTOCOL_VERSION;
        hdr.flags        = FRAME_REQUEST;
        hdr.serviceId    = 1;
        hdr.messageId    = static_cast<uint32_t>(i);
        hdr.seq          = static_cast<uint32_t>(i);
        hdr.payloadBytes = sizeof(uint32_t);
        hdr.aux          = 0;

        uint32_t payload = static_cast<uint32_t>(i * 100);

        ASSERT_TRUE(ring.write(&hdr, sizeof(hdr)));
        ASSERT_TRUE(ring.write(&payload, sizeof(payload)));
    }

    // Read N frames and verify order
    for (int i = 0; i < N; ++i) {
        FrameHeader hdr{};
        ASSERT_TRUE(ring.read(&hdr, sizeof(hdr)));
        EXPECT_EQ(hdr.messageId, static_cast<uint32_t>(i));
        EXPECT_EQ(hdr.seq, static_cast<uint32_t>(i));

        uint32_t payload = 0;
        ASSERT_TRUE(ring.read(&payload, sizeof(payload)));
        EXPECT_EQ(payload, static_cast<uint32_t>(i * 100));
    }

    EXPECT_TRUE(ring.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Test 3: Wraparound — fill buffer past capacity boundary, verify
//         correct wraparound behavior.
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, Wraparound) {
    // Frame size = 24 (header) + 4 (payload) = 28 bytes
    // Buffer = 4096 bytes
    // Fill most of the buffer, read it, then write more to force wraparound.

    constexpr uint32_t frameSize = sizeof(FrameHeader) + sizeof(uint32_t);

    // Phase 1: Fill ~3/4 of the buffer
    uint32_t count1 = (kTestSize * 3 / 4) / frameSize;
    for (uint32_t i = 0; i < count1; ++i) {
        FrameHeader hdr{};
        hdr.seq          = i;
        hdr.payloadBytes = sizeof(uint32_t);
        uint32_t payload = i;
        ASSERT_TRUE(ring.write(&hdr, sizeof(hdr)));
        ASSERT_TRUE(ring.write(&payload, sizeof(payload)));
    }

    // Phase 2: Read all — frees space but head is near end of buffer
    for (uint32_t i = 0; i < count1; ++i) {
        FrameHeader hdr{};
        uint32_t payload = 0;
        ASSERT_TRUE(ring.read(&hdr, sizeof(hdr)));
        ASSERT_TRUE(ring.read(&payload, sizeof(payload)));
        EXPECT_EQ(hdr.seq, i);
        EXPECT_EQ(payload, i);
    }

    // Phase 3: Write more — this will wrap around the end of the data array
    uint32_t count2 = (kTestSize * 3 / 4) / frameSize;
    for (uint32_t i = 0; i < count2; ++i) {
        FrameHeader hdr{};
        hdr.seq          = 1000 + i;
        hdr.payloadBytes = sizeof(uint32_t);
        uint32_t payload = 1000 + i;
        ASSERT_TRUE(ring.write(&hdr, sizeof(hdr)));
        ASSERT_TRUE(ring.write(&payload, sizeof(payload)));
    }

    // Phase 4: Read and verify the wrapped data
    for (uint32_t i = 0; i < count2; ++i) {
        FrameHeader hdr{};
        uint32_t payload = 0;
        ASSERT_TRUE(ring.read(&hdr, sizeof(hdr)));
        ASSERT_TRUE(ring.read(&payload, sizeof(payload)));
        EXPECT_EQ(hdr.seq, 1000 + i);
        EXPECT_EQ(payload, 1000 + i);
    }

    EXPECT_TRUE(ring.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Test 4: Full buffer — write until ring buffer is full, verify
//         write returns false (RPC_ERR_RING_FULL equivalent).
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, FullBuffer) {
    // Fill the entire buffer with 1-byte writes
    std::vector<uint8_t> data(kTestSize, 0xAA);
    ASSERT_TRUE(ring.write(data.data(), kTestSize));

    // Buffer should be full
    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.writeAvailable(), 0u);

    // Further write should fail
    uint8_t extra = 0xFF;
    EXPECT_FALSE(ring.write(&extra, 1));

    // Read everything back
    std::vector<uint8_t> readback(kTestSize);
    ASSERT_TRUE(ring.read(readback.data(), kTestSize));
    EXPECT_EQ(data, readback);

    // Buffer should be empty again
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.writeAvailable(), kTestSize);
}

// ═══════════════════════════════════════════════════════════════════════
// Test 5: Empty buffer read — attempt read from empty buffer, verify
//         no data returned.
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, EmptyBufferRead) {
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.readAvailable(), 0u);

    uint8_t buf[64];
    EXPECT_FALSE(ring.read(buf, 1));
    EXPECT_FALSE(ring.read(buf, sizeof(buf)));

    // Peek should also fail
    EXPECT_FALSE(ring.peek(buf, 1));
}

// ═══════════════════════════════════════════════════════════════════════
// Additional: Peek does not consume data
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, PeekDoesNotConsume) {
    uint32_t value = 42;
    ASSERT_TRUE(ring.write(&value, sizeof(value)));

    uint32_t peeked = 0;
    ASSERT_TRUE(ring.peek(&peeked, sizeof(peeked)));
    EXPECT_EQ(peeked, 42u);

    // Data should still be available
    EXPECT_EQ(ring.readAvailable(), sizeof(uint32_t));

    uint32_t readVal = 0;
    ASSERT_TRUE(ring.read(&readVal, sizeof(readVal)));
    EXPECT_EQ(readVal, 42u);
    EXPECT_TRUE(ring.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Additional: Skip advances read pointer
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, SkipAdvancesReadPointer) {
    FrameHeader hdr{};
    hdr.seq          = 99;
    hdr.payloadBytes = sizeof(uint32_t);
    uint32_t payload = 0xCAFE;

    ASSERT_TRUE(ring.write(&hdr, sizeof(hdr)));
    ASSERT_TRUE(ring.write(&payload, sizeof(payload)));

    // Skip the header
    ASSERT_TRUE(ring.skip(sizeof(FrameHeader)));

    // Read just the payload
    uint32_t readPayload = 0;
    ASSERT_TRUE(ring.read(&readPayload, sizeof(readPayload)));
    EXPECT_EQ(readPayload, 0xCAFE);
    EXPECT_TRUE(ring.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// Additional: Capacity and writeAvailable/readAvailable consistency
// ═══════════════════════════════════════════════════════════════════════

TEST_F(RingBufferTest, CapacityConsistency) {
    EXPECT_EQ(TestRing::capacity(), kTestSize);
    EXPECT_EQ(ring.writeAvailable(), kTestSize);
    EXPECT_EQ(ring.readAvailable(), 0u);

    uint8_t data[100];
    std::memset(data, 0, sizeof(data));
    ASSERT_TRUE(ring.write(data, sizeof(data)));

    EXPECT_EQ(ring.writeAvailable(), kTestSize - 100);
    EXPECT_EQ(ring.readAvailable(), 100u);
}
