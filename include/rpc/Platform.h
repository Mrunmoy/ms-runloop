#pragma once

#include <cstdint>
#include <string>

namespace rpc {
namespace platform {

// ── Shared Memory ───────────────────────────────────────────────────

// Create an anonymous shared memory region.
// Returns the file descriptor, or -1 on failure.
// Linux: uses memfd_create.
// macOS (future): will use shm_open + shm_unlink.
int shmCreate(const char* name, uint32_t size);

// ── Unix Domain Sockets ─────────────────────────────────────────────

// Create a server (listening) UDS socket bound to the given service name.
// Linux: SOCK_SEQPACKET, abstract namespace \0rpc_<serviceName>.
// macOS (future): SOCK_STREAM, filesystem path.
// Returns the listening socket fd, or -1 on failure.
int udsListen(const char* serviceName);

// Connect to a server UDS socket for the given service name.
// Returns the connected socket fd, or -1 on failure.
int udsConnect(const char* serviceName);

// Accept a client connection on a listening socket.
// Returns the accepted socket fd, or -1 on failure.
int udsAccept(int listenFd);

// Send a file descriptor over a UDS socket using SCM_RIGHTS.
// Also sends `dataLen` bytes of ancillary data (e.g., protocol version).
// Returns 0 on success, -1 on failure.
int udsSendFd(int sockFd, int fdToSend, const void* data, uint32_t dataLen);

// Receive a file descriptor over a UDS socket using SCM_RIGHTS.
// Also receives up to `dataLen` bytes of ancillary data.
// Returns 0 on success, -1 on failure. Sets *receivedFd.
int udsRecvFd(int sockFd, int* receivedFd, void* data, uint32_t dataLen);

// Send raw bytes over a UDS socket.
// Returns 0 on success, -1 on failure.
int udsSend(int sockFd, const void* data, uint32_t dataLen);

// Receive raw bytes from a UDS socket.
// Returns number of bytes received, or -1 on failure.
int udsRecv(int sockFd, void* data, uint32_t dataLen);

// ── Epoll ───────────────────────────────────────────────────────────

// Create an epoll instance. Returns the epoll fd, or -1 on failure.
int epollCreate();

// Add a file descriptor to the epoll set for read events.
// `userData` is an opaque pointer stored with the event.
// Returns 0 on success, -1 on failure.
int epollAdd(int epollFd, int fd, void* userData);

// Remove a file descriptor from the epoll set.
// Returns 0 on success, -1 on failure.
int epollRemove(int epollFd, int fd);

// ── Socket Path ─────────────────────────────────────────────────────

// Build the abstract namespace socket path for a service name.
// Linux: "\0rpc_<serviceName>"
std::string socketPath(const char* serviceName);

// ── Close ───────────────────────────────────────────────────────────

void closeFd(int fd);

} // namespace platform
} // namespace rpc
