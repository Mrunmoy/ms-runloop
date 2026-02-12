#pragma once

#include "rpc/Types.h"

#include <cstdint>
#include <vector>

namespace rpc {

std::vector<uint8_t> encodeFrameHeader(const FrameHeader& header);
bool decodeFrameHeader(const uint8_t* bytes, uint32_t len, FrameHeader* out);

} // namespace rpc
