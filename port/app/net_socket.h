// [N] TCP socket helpers for netplay (net_socket.cpp). Sockets are intptr_t handles, -1 = none.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rm8net {
bool sockInit();
intptr_t listenTcp(int port, std::string &err);		// non-blocking listening socket on every interface (dual stack when possible)
intptr_t acceptTcp(intptr_t lfd, std::string &peer);	// -1 = nobody waiting
bool connectStart(const std::string &host, int port);	// name lookup + connect on a worker thread (8 s timeout)
int connectPoll(intptr_t &fd, std::string &err);	// 0 pending, 1 connected (fd set, non-blocking), -1 failed (err)
void connectCancel();
int sendSome(intptr_t fd, const void *p, int n);	// bytes sent, 0 = would block, -1 = error
int recvSome(intptr_t fd, void *p, int n);		// bytes read, 0 = nothing now, -1 = closed / error
void closeSock(intptr_t fd);
std::vector<std::string> localIPv4();			// the LAN addresses to show in the host lobby
bool splitAddr(const std::string &in, std::string &host, int &port, int defPort);	// "1.2.3.4:47000", "frp.example.com:12345", "[::1]:47000"
}
