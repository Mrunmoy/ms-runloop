#include "rpc/Platform.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace rpc::platform {

std::string endpointFor(const std::string& serviceName) {
    return std::string("rpc_") + serviceName;
}

int createServerSocket(const std::string& serviceName) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string endpoint = endpointFor(serviceName);
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, endpoint.c_str(), endpoint.size());

    socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + endpoint.size());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int connectClientSocket(const std::string& serviceName) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string endpoint = endpointFor(serviceName);
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, endpoint.c_str(), endpoint.size());
    socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + endpoint.size());

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int createSharedMemory(uint32_t bytes) {
    int fd = static_cast<int>(syscall(SYS_memfd_create, "rpc_shm", MFD_CLOEXEC));
    if (fd < 0) {
        return -1;
    }

    if (ftruncate(fd, bytes) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int sendFdWithVersion(int socketFd, uint16_t version, int fdToSend) {
    msghdr msg{};

    iovec iov{};
    iov.iov_base = &version;
    iov.iov_len = sizeof(version);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fdToSend, sizeof(int));

    return static_cast<int>(sendmsg(socketFd, &msg, 0));
}

int recvFdWithVersion(int socketFd, uint16_t* version, int* receivedFd) {
    msghdr msg{};

    iovec iov{};
    iov.iov_base = version;
    iov.iov_len = sizeof(*version);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    int n = static_cast<int>(recvmsg(socketFd, &msg, 0));
    if (n <= 0) {
        return n;
    }

    *receivedFd = -1;
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            std::memcpy(receivedFd, CMSG_DATA(cmsg), sizeof(int));
            break;
        }
    }

    return n;
}

int sendSignalByte(int socketFd) {
    uint8_t byte = 1;
    return static_cast<int>(send(socketFd, &byte, sizeof(byte), 0));
}

int recvSignalByte(int socketFd) {
    uint8_t byte = 0;
    return static_cast<int>(recv(socketFd, &byte, sizeof(byte), 0));
}

} // namespace rpc::platform
