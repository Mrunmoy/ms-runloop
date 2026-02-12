#include "rpc/Service.h"

#include "rpc/FrameCodec.h"
#include "rpc/Platform.h"

#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rpc {

Service::Service(std::string serviceName)
    : m_serviceName(std::move(serviceName)) {}

Service::~Service() {
    stop();
}

bool Service::start() {
    m_listenFd = platform::createServerSocket(m_serviceName);
    if (m_listenFd < 0) {
        return false;
    }

    m_running.store(true);
    m_acceptThread = std::thread([this] { acceptLoop(); });
    return true;
}

void Service::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_listenFd >= 0) {
        shutdown(m_listenFd, SHUT_RDWR);
        close(m_listenFd);
        m_listenFd = -1;
    }

    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }

    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    for (auto& c : m_connections) {
        if (c->socketFd >= 0) {
            shutdown(c->socketFd, SHUT_RDWR);
        }
    }
    for (auto& c : m_connections) {
        if (c->thread.joinable()) {
            c->thread.join();
        }
        closeConnection(c.get());
    }
    m_connections.clear();
}

void Service::setRequestHandler(RequestHandler handler) {
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    m_handler = std::move(handler);
}

int Service::notify(uint32_t serviceId, uint32_t notifyId, const std::vector<uint8_t>& payload) {
    FrameHeader header{};
    header.version = PROTOCOL_VERSION;
    header.flags = FRAME_NOTIFY;
    header.serviceId = serviceId;
    header.messageId = notifyId;
    header.payloadBytes = static_cast<uint32_t>(payload.size());

    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    for (auto& c : m_connections) {
        if (c->region == nullptr) {
            continue;
        }

        auto encoded = encodeFrameHeader(header);
        if (!c->region->serverToClient.write(encoded.data(), encoded.size()) ||
            !c->region->serverToClient.write(payload.data(), header.payloadBytes)) {
            return RPC_ERR_RING_FULL;
        }

        if (platform::sendSignalByte(c->socketFd) <= 0) {
            return RPC_ERR_DISCONNECTED;
        }
    }

    return RPC_SUCCESS;
}

void Service::acceptLoop() {
    while (m_running.load()) {
        int clientFd = accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (!m_running.load()) {
                break;
            }
            continue;
        }

        uint16_t version = 0;
        int shmFd = -1;
        if (platform::recvFdWithVersion(clientFd, &version, &shmFd) <= 0 || shmFd < 0) {
            close(clientFd);
            continue;
        }

        uint8_t ack = (version == PROTOCOL_VERSION) ? 1 : 0;
        send(clientFd, &ack, sizeof(ack), 0);
        if (ack == 0) {
            close(shmFd);
            close(clientFd);
            continue;
        }

        void* mem = mmap(nullptr,
                         sizeof(SharedRegion),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         shmFd,
                         0);
        if (mem == MAP_FAILED) {
            close(shmFd);
            close(clientFd);
            continue;
        }

        auto connection = std::make_unique<Connection>();
        connection->socketFd = clientFd;
        connection->shmFd = shmFd;
        connection->region = static_cast<SharedRegion*>(mem);
        connection->thread = std::thread([this, ptr = connection.get()] { connectionLoop(ptr); });

        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        m_connections.push_back(std::move(connection));
    }
}

void Service::connectionLoop(Connection* connection) {
    while (m_running.load()) {
        int n = platform::recvSignalByte(connection->socketFd);
        if (n <= 0) {
            break;
        }

        while (true) {
            FrameHeader wire{};
            if (!connection->region->clientToServer.peek(&wire, sizeof(wire))) {
                break;
            }

            auto raw = encodeFrameHeader(wire);
            FrameHeader header{};
            if (!decodeFrameHeader(raw.data(), static_cast<uint32_t>(raw.size()), &header)) {
                break;
            }

            if (connection->region->clientToServer.readAvailable() < sizeof(FrameHeader) + header.payloadBytes) {
                break;
            }

            connection->region->clientToServer.skip(sizeof(FrameHeader));
            std::vector<uint8_t> payload(header.payloadBytes);
            if (header.payloadBytes > 0) {
                connection->region->clientToServer.read(payload.data(), header.payloadBytes);
            }

            if (header.flags & FRAME_REQUEST) {
                RequestHandler handler;
                {
                    std::lock_guard<std::mutex> lock(m_handlerMutex);
                    handler = m_handler;
                }

                std::vector<uint8_t> responsePayload;
                int status = RPC_ERR_INVALID_METHOD;
                if (handler) {
                    status = handler(header.messageId, payload, &responsePayload);
                }

                FrameHeader response{};
                response.version = PROTOCOL_VERSION;
                response.flags = FRAME_RESPONSE;
                response.serviceId = header.serviceId;
                response.messageId = header.messageId;
                response.seq = header.seq;
                response.payloadBytes = static_cast<uint32_t>(responsePayload.size());
                response.aux = static_cast<uint32_t>(status);

                auto encoded = encodeFrameHeader(response);
                if (!connection->region->serverToClient.write(encoded.data(), encoded.size()) ||
                    !connection->region->serverToClient.write(responsePayload.data(), response.payloadBytes)) {
                    continue;
                }
                platform::sendSignalByte(connection->socketFd);
            }
        }
    }
}

void Service::closeConnection(Connection* connection) {
    if (connection->region != nullptr) {
        munmap(connection->region, sizeof(SharedRegion));
        connection->region = nullptr;
    }
    if (connection->shmFd >= 0) {
        close(connection->shmFd);
        connection->shmFd = -1;
    }
    if (connection->socketFd >= 0) {
        close(connection->socketFd);
        connection->socketFd = -1;
    }
}

} // namespace rpc
