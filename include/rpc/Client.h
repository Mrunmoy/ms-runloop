#pragma once

#include "rpc/Service.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rpc {

class Client {
public:
    explicit Client(std::string serviceName);
    ~Client();

    bool connect(uint16_t version = PROTOCOL_VERSION, int retryMs = 10, int maxAttempts = 200);
    void disconnect();

    int call(uint32_t serviceId,
             uint32_t methodId,
             const std::vector<uint8_t>& request,
             std::vector<uint8_t>* response,
             uint32_t timeoutMs = 2000);

    int notify(uint32_t serviceId, uint32_t notifyId, const std::vector<uint8_t>& payload);

    using NotifyHandler = std::function<void(uint32_t notifyId, const std::vector<uint8_t>& payload)>;
    void setNotifyHandler(NotifyHandler handler);

private:
    struct PendingCall {
        std::condition_variable cv;
        bool done{false};
        int status{RPC_SUCCESS};
        std::vector<uint8_t> response;
    };

    void receiverLoop();

    std::string m_serviceName;
    int m_socketFd{-1};
    int m_shmFd{-1};
    SharedRegion* m_region{nullptr};
    std::thread m_receiverThread;
    std::atomic<bool> m_running{false};
    std::atomic<uint32_t> m_nextSeq{1};

    std::mutex m_pendingMutex;
    std::unordered_map<uint32_t, std::shared_ptr<PendingCall>> m_pending;

    std::mutex m_notifyMutex;
    NotifyHandler m_notifyHandler;
};

} // namespace rpc
