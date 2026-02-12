#pragma once

#include <cstdint>
#include <string>

namespace rpc::platform {

int createServerSocket(const std::string& serviceName);
int connectClientSocket(const std::string& serviceName);
std::string endpointFor(const std::string& serviceName);

int createSharedMemory(uint32_t bytes);
int sendFdWithVersion(int socketFd, uint16_t version, int fdToSend);
int recvFdWithVersion(int socketFd, uint16_t* version, int* receivedFd);

int sendSignalByte(int socketFd);
int recvSignalByte(int socketFd);

} // namespace rpc::platform
