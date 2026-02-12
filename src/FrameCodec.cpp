#include "rpc/FrameCodec.h"

#include <endian.h>
#include <cstring>

namespace rpc {

std::vector<uint8_t> encodeFrameHeader(const FrameHeader& header) {
    std::vector<uint8_t> out(sizeof(FrameHeader));
    FrameHeader wire{};
    wire.version = htole16(header.version);
    wire.flags = htole16(header.flags);
    wire.serviceId = htole32(header.serviceId);
    wire.messageId = htole32(header.messageId);
    wire.seq = htole32(header.seq);
    wire.payloadBytes = htole32(header.payloadBytes);
    wire.aux = htole32(header.aux);

    std::memcpy(out.data(), &wire, sizeof(wire));
    return out;
}

bool decodeFrameHeader(const uint8_t* bytes, uint32_t len, FrameHeader* out) {
    if (len < sizeof(FrameHeader) || out == nullptr) {
        return false;
    }

    FrameHeader wire{};
    std::memcpy(&wire, bytes, sizeof(wire));
    out->version = le16toh(wire.version);
    out->flags = le16toh(wire.flags);
    out->serviceId = le32toh(wire.serviceId);
    out->messageId = le32toh(wire.messageId);
    out->seq = le32toh(wire.seq);
    out->payloadBytes = le32toh(wire.payloadBytes);
    out->aux = le32toh(wire.aux);
    return true;
}

} // namespace rpc
