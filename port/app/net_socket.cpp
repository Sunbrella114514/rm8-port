// [N] TCP sockets for netplay (net.cpp): Winsock on Windows, BSD sockets on Android / Linux. Non-blocking, polled from the main loop; only the
// name lookup + connect of a join runs on a short-lived thread (getaddrinfo blocks, and a Sakura FRP address is usually a host name).
#include "net_socket.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <memory>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Native;
typedef int socklen_t;
static bool bad(Native s){ return s == INVALID_SOCKET; }
static int lastErr(){ return WSAGetLastError(); }
static bool wouldBlock(int e){ return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
static void closeNative(Native s){ closesocket(s); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
typedef int Native;
static bool bad(Native s){ return s < 0; }
static int lastErr(){ return errno; }
static bool wouldBlock(int e){ return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EALREADY; }
static void closeNative(Native s){ ::close(s); }
#endif

void rm8Log(const char *fmt, ...);

namespace rm8net {

static Native nat(intptr_t s){ return (Native)s; }
static intptr_t wrap(Native s){ return bad(s) ? -1 : (intptr_t)s; }

bool sockInit()
{
#ifdef _WIN32
	static bool done = false, ok = false;
	if(done) return ok;
	done = true;
	WSADATA w; ok = WSAStartup(MAKEWORD(2, 2), &w) == 0;
	if(!ok) rm8Log("net: WSAStartup failed");
	return ok;
#else
	return true;
#endif
}

static void setNonBlocking(Native s)
{
#ifdef _WIN32
	u_long one = 1; ioctlsocket(s, FIONBIO, &one);
#else
	int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}
static void tune(Native s)
{
	int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);	// small command messages: no Nagle delay
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

intptr_t listenTcp(int port, std::string &err)
{
	if(!sockInit()){ err = "socket init"; return -1; }
	// a dual-stack IPv6 socket (IPv4 peers arrive as mapped addresses) when the system has one, else plain IPv4
	Native s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if(!bad(s)){
		int off = 0, one = 1;
		setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);
#ifndef _WIN32
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);	// (Windows: SO_REUSEADDR would let a second instance steal the port)
#else
		(void)one;
#endif
		sockaddr_in6 a; memset(&a, 0, sizeof a); a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any; a.sin6_port = htons((unsigned short)port);
		if(bind(s, (sockaddr*)&a, sizeof a) != 0){ closeNative(s); s = (Native)-1; }
	}
	if(bad(s)){
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(bad(s)){ err = "socket()"; return -1; }
#ifndef _WIN32
		int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#endif
		sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)port);
		if(bind(s, (sockaddr*)&a, sizeof a) != 0){ char b[96]; snprintf(b, sizeof b, "端口 %d 已被占用或不可用（错误 %d）", port, lastErr()); err = b; closeNative(s); return -1; }
	}
	if(listen(s, 8) != 0){ err = "listen()"; closeNative(s); return -1; }
	setNonBlocking(s);
	return wrap(s);
}

static std::string addrText(const sockaddr *sa)
{
	char h[INET6_ADDRSTRLEN] = "?";
	if(sa->sa_family == AF_INET) inet_ntop(AF_INET, (void*)&((const sockaddr_in*)sa)->sin_addr, h, sizeof h);
	else if(sa->sa_family == AF_INET6){
		const sockaddr_in6 *a6 = (const sockaddr_in6*)sa;
		const unsigned char *b = (const unsigned char*)&a6->sin6_addr;
		static const unsigned char mapped[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
		if(!memcmp(b, mapped, 12)) snprintf(h, sizeof h, "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
		else inet_ntop(AF_INET6, (void*)&a6->sin6_addr, h, sizeof h);
	}
	return h;
}

intptr_t acceptTcp(intptr_t lfd, std::string &peer)
{
	if(lfd < 0) return -1;
	sockaddr_storage a; socklen_t al = sizeof a;
	Native s = accept(nat(lfd), (sockaddr*)&a, &al);
	if(bad(s)) return -1;
	setNonBlocking(s); tune(s);
	peer = addrText((sockaddr*)&a);
	return wrap(s);
}

// ---- connect: a worker thread resolves the name and connects (8 s), the main loop polls the result
struct Pending { std::atomic<int> state{0}; std::atomic<bool> cancelled{false}; intptr_t fd = -1; std::string err; };	// state 0 running, 1 connected, -1 failed
static std::shared_ptr<Pending> g_pend;

static void connectWorker(std::shared_ptr<Pending> p, std::string host, int port)
{
	addrinfo hints; memset(&hints, 0, sizeof hints); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = nullptr; char ps[16]; snprintf(ps, sizeof ps, "%d", port);
	if(getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res){ p->err = "找不到主机 " + host; p->state = -1; return; }
	Native got = (Native)-1; std::string err = "连接失败（主机没有响应，或端口没有开放 / 映射）";
	int refused = 0, timedOut = 0;	// [N2] a clearer message: nobody listens there (refused) vs no answer at all (firewall, wrong address, FRP tunnel down)
	for(addrinfo *ai = res; ai && bad(got); ai = ai->ai_next){
		Native s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if(bad(s)) continue;
		setNonBlocking(s);
		bool ok = connect(s, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0;
		int so = 0;
		if(!ok && wouldBlock(lastErr())){
#ifdef _WIN32
			fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
			timeval tv = {8, 0};
			int n = select(0, nullptr, &w, &e, &tv);
			if(n > 0 && FD_ISSET(s, &w)) ok = true;
			else if(n == 0) timedOut++;
			else { int sl = sizeof so; getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so, &sl); if(so == WSAECONNREFUSED) refused++; }
#else
			pollfd pf = {s, POLLOUT, 0};
			int n = poll(&pf, 1, 8000);
			if(n > 0){ socklen_t sl = sizeof so; getsockopt(s, SOL_SOCKET, SO_ERROR, &so, &sl); ok = so == 0 && (pf.revents & POLLOUT); if(so == ECONNREFUSED) refused++; }
			else if(n == 0) timedOut++;
#endif
		}
#ifdef _WIN32
		else if(!ok && lastErr() == WSAECONNREFUSED) refused++;
#else
		else if(!ok && lastErr() == ECONNREFUSED) refused++;
#endif
		if(ok){ got = s; break; }
		closeNative(s);
	}
	freeaddrinfo(res);
	if(refused) err = "连接被拒绝：那个地址上没有主机在等待（对方还没“建立主机”，或端口号不对）";
	else if(timedOut) err = "连接超时：主机没有响应（地址不对、防火墙拦截了端口，或 FRP 映射没有运行）";
	if(bad(got)){ p->err = err; p->state = -1; return; }
	tune(got);
	if(p->cancelled){ closeNative(got); p->state = -1; return; }
	p->fd = wrap(got); p->state = 1;
}

bool connectStart(const std::string &host, int port)
{
	if(!sockInit()) return false;
	connectCancel();
	auto p = std::make_shared<Pending>();
	g_pend = p;
	std::thread(connectWorker, p, host, port).detach();
	return true;
}

int connectPoll(intptr_t &fd, std::string &err)
{
	if(!g_pend) return -1;
	int st = g_pend->state.load();
	if(st == 0) return 0;
	if(st == 1){ fd = g_pend->fd; g_pend.reset(); return 1; }
	err = g_pend->err; g_pend.reset(); return -1;
}

void connectCancel()
{
	if(!g_pend) return;
	g_pend->cancelled = true;
	if(g_pend->state.load() == 1 && g_pend->fd >= 0) closeSock(g_pend->fd);	// connected but nobody wants it any more
	g_pend.reset();
}

int sendSome(intptr_t fd, const void *p, int n)
{
	if(fd < 0) return -1;
#ifdef _WIN32
	int r = send(nat(fd), (const char*)p, n, 0);
#elif defined(MSG_NOSIGNAL)
	int r = (int)::send(nat(fd), p, (size_t)n, MSG_NOSIGNAL);
#else
	int r = (int)::send(nat(fd), p, (size_t)n, 0);
#endif
	if(r >= 0) return r;
	return wouldBlock(lastErr()) ? 0 : -1;
}

int recvSome(intptr_t fd, void *p, int n)
{
	if(fd < 0) return -1;
#ifdef _WIN32
	int r = recv(nat(fd), (char*)p, n, 0);
#else
	int r = (int)::recv(nat(fd), p, (size_t)n, 0);
#endif
	if(r > 0) return r;
	if(r == 0) return -1;	// closed by the peer
	return wouldBlock(lastErr()) ? 0 : -1;
}

void closeSock(intptr_t fd){ if(fd >= 0) closeNative(nat(fd)); }

std::vector<std::string> localIPv4()
{
	std::vector<std::string> out;
	if(!sockInit()) return out;
#ifdef _WIN32
	char hn[256] = {0};
	if(gethostname(hn, sizeof hn) == 0){
		addrinfo hints; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; addrinfo *res = nullptr;
		if(getaddrinfo(hn, nullptr, &hints, &res) == 0){
			for(addrinfo *ai = res; ai; ai = ai->ai_next){ std::string s = addrText(ai->ai_addr); if(s != "127.0.0.1" && s.compare(0, 8, "169.254.") != 0) out.push_back(s); }
			freeaddrinfo(res);
		}
	}
#else
	ifaddrs *ifs = nullptr;
	if(getifaddrs(&ifs) == 0){
		for(ifaddrs *i = ifs; i; i = i->ifa_next){
			if(!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
			std::string s = addrText(i->ifa_addr); if(s.compare(0, 8, "169.254.") != 0) out.push_back(s);
		}
		freeifaddrs(ifs);
	}
#endif
	return out;
}

bool splitAddr(const std::string &in, std::string &host, int &port, int defPort)
{
	std::string s;
	for(size_t i = 0; i < in.size(); i++){
		unsigned char c = (unsigned char)in[i];
		if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		// full-width ：．。 of a Chinese input method -> ASCII
		if(c == 0xEF && i + 2 < in.size() && (unsigned char)in[i + 1] == 0xBC && (unsigned char)in[i + 2] == 0x9A){ s += ':'; i += 2; continue; }
		if(c == 0xEF && i + 2 < in.size() && (unsigned char)in[i + 1] == 0xBC && (unsigned char)in[i + 2] == 0x8E){ s += '.'; i += 2; continue; }
		if(c == 0xE3 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0x82){ s += '.'; i += 2; continue; }
		s += (char)c;
	}
	if(s.compare(0, 6, "tcp://") == 0) s = s.substr(6);
	port = defPort; host.clear();
	if(s.empty()) return false;
	if(s[0] == '['){	// [v6]:port
		size_t e = s.find(']'); if(e == std::string::npos) return false;
		host = s.substr(1, e - 1);
		if(e + 1 < s.size() && s[e + 1] == ':') port = atoi(s.c_str() + e + 2);
	}else{
		size_t c = s.rfind(':');
		if(c != std::string::npos && s.find(':') == c){ host = s.substr(0, c); port = atoi(s.c_str() + c + 1); }
		else host = s;	// no port, or a bare IPv6 address
	}
	return !host.empty() && host.size() < 256 && port > 0 && port < 65536;
}

}	// namespace rm8net
