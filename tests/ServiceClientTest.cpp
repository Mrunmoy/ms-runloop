#include "rpc/Client.h"
#include "rpc/FrameCodec.h"
#include "rpc/Service.h"
#include "rpc/Types.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "gtest/gtest.h"

namespace rpc {

namespace {

std::vector<uint8_t> toBytes(uint32_t v) {
    std::vector<uint8_t> out(sizeof(v));
    std::memcpy(out.data(), &v, sizeof(v));
    return out;
}

uint32_t fromBytes(const std::vector<uint8_t>& bytes) {
    uint32_t v = 0;
    std::memcpy(&v, bytes.data(), sizeof(v));
    return v;
}

} // namespace

TEST(ServiceClientTest, BasicConnectionAndRpc) {
    Service service("svc_basic");
    service.setRequestHandler([](uint32_t methodId,
                                 const std::vector<uint8_t>& request,
                                 std::vector<uint8_t>* response) {
        if (methodId != 7) {
            return RPC_ERR_INVALID_METHOD;
        }
        uint32_t x = fromBytes(request);
        *response = toBytes(x + 1);
        return RPC_SUCCESS;
    });
    ASSERT_TRUE(service.start());

    Client client("svc_basic");
    ASSERT_TRUE(client.connect());

    std::vector<uint8_t> response;
    int status = client.call(1, 7, toBytes(41), &response);
    EXPECT_EQ(status, RPC_SUCCESS);
    EXPECT_EQ(fromBytes(response), 42u);

    client.disconnect();
    service.stop();
}

TEST(ServiceClientTest, VersionMismatchRejected) {
    Service service("svc_version");
    ASSERT_TRUE(service.start());

    Client client("svc_version");
    EXPECT_FALSE(client.connect(PROTOCOL_VERSION + 1, 10, 10));

    service.stop();
}

TEST(ServiceClientTest, BroadcastNotificationToMultipleClients) {
    Service service("svc_notify");
    ASSERT_TRUE(service.start());

    Client c1("svc_notify");
    Client c2("svc_notify");
    ASSERT_TRUE(c1.connect());
    ASSERT_TRUE(c2.connect());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::atomic<int> seen{0};
    c1.setNotifyHandler([&](uint32_t notifyId, const std::vector<uint8_t>& payload) {
        EXPECT_EQ(notifyId, 99u);
        EXPECT_EQ(fromBytes(payload), 123u);
        seen.fetch_add(1);
    });
    c2.setNotifyHandler([&](uint32_t notifyId, const std::vector<uint8_t>& payload) {
        EXPECT_EQ(notifyId, 99u);
        EXPECT_EQ(fromBytes(payload), 123u);
        seen.fetch_add(1);
    });

    ASSERT_EQ(service.notify(1, 99, toBytes(123)), RPC_SUCCESS);

    for (int i = 0; i < 50 && seen.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(seen.load(), 2);

    c1.disconnect();
    c2.disconnect();
    service.stop();
}

TEST(ServiceClientTest, FrameHeaderCodecRoundTrip) {
    FrameHeader input{};
    input.version = PROTOCOL_VERSION;
    input.flags = FRAME_REQUEST;
    input.serviceId = 3;
    input.messageId = 8;
    input.seq = 11;
    input.payloadBytes = 17;
    input.aux = 55;

    std::vector<uint8_t> encoded = encodeFrameHeader(input);
    FrameHeader output{};
    ASSERT_TRUE(decodeFrameHeader(encoded.data(), static_cast<uint32_t>(encoded.size()), &output));
    EXPECT_EQ(output.version, input.version);
    EXPECT_EQ(output.flags, input.flags);
    EXPECT_EQ(output.serviceId, input.serviceId);
    EXPECT_EQ(output.messageId, input.messageId);
    EXPECT_EQ(output.seq, input.seq);
    EXPECT_EQ(output.payloadBytes, input.payloadBytes);
    EXPECT_EQ(output.aux, input.aux);
}

} // namespace rpc
