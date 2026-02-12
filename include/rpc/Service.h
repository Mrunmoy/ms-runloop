#pragma once

#include "rpc/RingBuffer.h"
#include "rpc/Types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rpc {

struct SharedRegion {
    RingBuffer<RING_BUFFER_SIZE> clientToServer;
    RingBuffer<RING_BUFFER_SIZE> serverToClient;
};

class Service {
public:
    using RequestHandler = std::function<int(uint32_t messageId,
                                             const std::vector<uint8_t>& request,
                                             std::vector<uint8_t>* response)>;

    explicit Service(std::string serviceName);
    ~Service();

    bool start();
    void stop();

    void setRequestHandler(RequestHandler handler);
    int notify(uint32_t serviceId, uint32_t notifyId, const std::vector<uint8_t>& payload);

private:
    struct Connection {
        int socketFd{-1};
        int shmFd{-1};
        SharedRegion* region{nullptr};
        std::thread thread;
    };

    void acceptLoop();
    void connectionLoop(Connection* connection);
    void closeConnection(Connection* connection);

    std::string m_serviceName;
    int m_listenFd{-1};

    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;

    std::mutex m_connectionsMutex;
    std::vector<std::unique_ptr<Connection>> m_connections;

    std::mutex m_handlerMutex;
    RequestHandler m_handler;
};

} // namespace rpc
