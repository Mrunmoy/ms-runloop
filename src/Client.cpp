#include "rpc/Client.h"

#include "rpc/FrameCodec.h"
#include "rpc/Platform.h"

#include <chrono>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rpc {

Client::Client(std::string serviceName)
    : m_serviceName(std::move(serviceName)) {}

Client::~Client() {
    disconnect();
}

bool Client::connect(uint16_t version, int retryMs, int maxAttempts) {
    for (int i = 0; i < maxAttempts; ++i) {
        m_socketFd = platform::connectClientSocket(m_serviceName);
        if (m_socketFd >= 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(retryMs));
    }
    if (m_socketFd < 0) {
        return false;
    }

    m_shmFd = platform::createSharedMemory(sizeof(SharedRegion));
    if (m_shmFd < 0) {
        close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    void* mem = mmap(nullptr,
                     sizeof(SharedRegion),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     m_shmFd,
                     0);
    if (mem == MAP_FAILED) {
        close(m_shmFd);
        close(m_socketFd);
        m_shmFd = -1;
        m_socketFd = -1;
        return false;
    }

    m_region = static_cast<SharedRegion*>(mem);
    m_region->clientToServer.reset();
    m_region->serverToClient.reset();

    if (platform::sendFdWithVersion(m_socketFd, version, m_shmFd) <= 0) {
        disconnect();
        return false;
    }

    uint8_t ack = 0;
    if (recv(m_socketFd, &ack, sizeof(ack), 0) <= 0 || ack == 0) {
        disconnect();
        return false;
    }

    m_running.store(true);
    m_receiverThread = std::thread([this] { receiverLoop(); });
    return true;
}

void Client::disconnect() {
    if (!m_running.exchange(false)) {
        if (m_region != nullptr) {
            munmap(m_region, sizeof(SharedRegion));
            m_region = nullptr;
        }
        if (m_shmFd >= 0) {
            close(m_shmFd);
            m_shmFd = -1;
        }
        if (m_socketFd >= 0) {
            close(m_socketFd);
            m_socketFd = -1;
        }
        return;
    }

    if (m_socketFd >= 0) {
        shutdown(m_socketFd, SHUT_RDWR);
    }
    if (m_receiverThread.joinable()) {
        m_receiverThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        for (auto& [_, pending] : m_pending) {
            pending->status = RPC_ERR_STOPPED;
            pending->done = true;
            pending->cv.notify_one();
        }
        m_pending.clear();
    }

    if (m_region != nullptr) {
        munmap(m_region, sizeof(SharedRegion));
        m_region = nullptr;
    }
    if (m_shmFd >= 0) {
        close(m_shmFd);
        m_shmFd = -1;
    }
    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
}

int Client::call(uint32_t serviceId,
                 uint32_t methodId,
                 const std::vector<uint8_t>& request,
                 std::vector<uint8_t>* response,
                 uint32_t timeoutMs) {
    if (!m_running.load() || m_region == nullptr) {
        return RPC_ERR_DISCONNECTED;
    }

    uint32_t seq = m_nextSeq.fetch_add(1);

    FrameHeader header{};
    header.version = PROTOCOL_VERSION;
    header.flags = FRAME_REQUEST;
    header.serviceId = serviceId;
    header.messageId = methodId;
    header.seq = seq;
    header.payloadBytes = static_cast<uint32_t>(request.size());

    auto encoded = encodeFrameHeader(header);
    if (!m_region->clientToServer.write(encoded.data(), static_cast<uint32_t>(encoded.size())) ||
        !m_region->clientToServer.write(request.data(), header.payloadBytes)) {
        return RPC_ERR_RING_FULL;
    }

    auto pending = std::make_shared<PendingCall>();
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pending[seq] = pending;
    }

    if (platform::sendSignalByte(m_socketFd) <= 0) {
        return RPC_ERR_DISCONNECTED;
    }

    std::unique_lock<std::mutex> lock(m_pendingMutex);
    if (!pending->cv.wait_for(lock,
                              std::chrono::milliseconds(timeoutMs),
                              [&] { return pending->done; })) {
        m_pending.erase(seq);
        return RPC_ERR_TIMEOUT;
    }

    int status = pending->status;
    if (status == RPC_SUCCESS && response != nullptr) {
        *response = pending->response;
    }

    m_pending.erase(seq);
    return status;
}

int Client::notify(uint32_t serviceId, uint32_t notifyId, const std::vector<uint8_t>& payload) {
    if (!m_running.load() || m_region == nullptr) {
        return RPC_ERR_DISCONNECTED;
    }

    FrameHeader header{};
    header.version = PROTOCOL_VERSION;
    header.flags = FRAME_NOTIFY;
    header.serviceId = serviceId;
    header.messageId = notifyId;
    header.payloadBytes = static_cast<uint32_t>(payload.size());

    auto encoded = encodeFrameHeader(header);
    if (!m_region->clientToServer.write(encoded.data(), static_cast<uint32_t>(encoded.size())) ||
        !m_region->clientToServer.write(payload.data(), header.payloadBytes)) {
        return RPC_ERR_RING_FULL;
    }

    return (platform::sendSignalByte(m_socketFd) > 0) ? RPC_SUCCESS : RPC_ERR_DISCONNECTED;
}

void Client::setNotifyHandler(NotifyHandler handler) {
    std::lock_guard<std::mutex> lock(m_notifyMutex);
    m_notifyHandler = std::move(handler);
}

void Client::receiverLoop() {
    while (m_running.load()) {
        int n = platform::recvSignalByte(m_socketFd);
        if (n <= 0) {
            break;
        }

        while (true) {
            FrameHeader wire{};
            if (!m_region->serverToClient.peek(&wire, sizeof(wire))) {
                break;
            }

            auto raw = encodeFrameHeader(wire);
            FrameHeader header{};
            if (!decodeFrameHeader(raw.data(), static_cast<uint32_t>(raw.size()), &header)) {
                break;
            }

            if (m_region->serverToClient.readAvailable() < sizeof(FrameHeader) + header.payloadBytes) {
                break;
            }

            m_region->serverToClient.skip(sizeof(FrameHeader));
            std::vector<uint8_t> payload(header.payloadBytes);
            if (header.payloadBytes > 0) {
                m_region->serverToClient.read(payload.data(), header.payloadBytes);
            }

            if (header.flags & FRAME_RESPONSE) {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                auto it = m_pending.find(header.seq);
                if (it != m_pending.end()) {
                    it->second->status = static_cast<int>(header.aux);
                    it->second->response = std::move(payload);
                    it->second->done = true;
                    it->second->cv.notify_one();
                }
            } else if (header.flags & FRAME_NOTIFY) {
                NotifyHandler handler;
                {
                    std::lock_guard<std::mutex> lock(m_notifyMutex);
                    handler = m_notifyHandler;
                }
                if (handler) {
                    handler(header.messageId, payload);
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    for (auto& [_, pending] : m_pending) {
        pending->status = RPC_ERR_DISCONNECTED;
        pending->done = true;
        pending->cv.notify_one();
    }
}

} // namespace rpc
