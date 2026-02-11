#pragma once

#include <cstdint>

namespace rpc {

// ── Error Codes ─────────────────────────────────────────────────────
// Negative = framework errors, 0 = success, positive = user-defined
enum RpcError : int {
    RPC_SUCCESS              =  0,
    RPC_ERR_DISCONNECTED     = -1,
    RPC_ERR_TIMEOUT          = -2,
    RPC_ERR_INVALID_SERVICE  = -3,
    RPC_ERR_INVALID_METHOD   = -4,
    RPC_ERR_VERSION_MISMATCH = -5,
    RPC_ERR_RING_FULL        = -6,
    RPC_ERR_STOPPED          = -7,
};

// ── Frame Flags ─────────────────────────────────────────────────────
enum FrameFlags : uint16_t {
    FRAME_REQUEST  = 0x0001,
    FRAME_RESPONSE = 0x0002,
    FRAME_NOTIFY   = 0x0004,
};

// ── Frame Header (24 bytes) ─────────────────────────────────────────
// All multi-byte fields are little-endian on the wire.
struct FrameHeader {
    uint16_t version;
    uint16_t flags;
    uint32_t serviceId;
    uint32_t messageId;
    uint32_t seq;
    uint32_t payloadBytes;
    uint32_t aux;
};

static_assert(sizeof(FrameHeader) == 24, "FrameHeader must be 24 bytes");

// ── Protocol Constants ──────────────────────────────────────────────
constexpr uint16_t PROTOCOL_VERSION = 1;
constexpr uint32_t RING_BUFFER_SIZE = 256 * 1024;  // 256 KB per direction

} // namespace rpc
