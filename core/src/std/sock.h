// sock.h: what the core's sockets share (std/http's server,
// httpserver.c, and std/net, net.c): BSD sockets and Winsock behind one
// set of names, a way to wake a thread that waits in poll(), and a map
// of ids to pointers. Not for the web, which has no sockets: a file
// includes it only outside __wasm__.
#ifndef AK_SOCK_H
#define AK_SOCK_H

#if defined(_WIN32)
#include <winsock2.h>  // before windows.h (base/thread.h's)
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "base/base.h"

#if defined(_WIN32)
typedef SOCKET ak_sock;
typedef WSAPOLLFD ak_pollfd;
#define AK_BAD_SOCK INVALID_SOCKET
#define AK_SHUT_WR SD_SEND
#define AK_SOCK_AGAIN(e) ((e) == WSAEWOULDBLOCK)
#define AK_SOCK_INTR(e) ((e) == WSAEINTR)
#define AK_SOCK_INPROGRESS(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#else
typedef int ak_sock;
typedef struct pollfd ak_pollfd;
#define AK_BAD_SOCK (-1)
#define AK_SHUT_WR SHUT_WR
#define AK_SOCK_AGAIN(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define AK_SOCK_INTR(e) ((e) == EINTR)
#define AK_SOCK_INPROGRESS(e) ((e) == EINPROGRESS)
#endif
#if defined(MSG_NOSIGNAL)
#define AK_SEND_FLAGS MSG_NOSIGNAL
#else
#define AK_SEND_FLAGS 0
#endif
#if !defined(AI_NUMERICSERV)
#define AI_NUMERICSERV 0
#endif

// ak_sock_startup readies the system's sockets (WSAStartup, once per
// process, on Windows; nothing elsewhere): false when there are none.
bool ak_sock_startup(void);
void ak_sock_close(ak_sock s);
// ak_sock_poll is poll() (WSAPoll); its timeout in milliseconds, -1 none.
int ak_sock_poll(ak_pollfd *p, size_t n, int timeout_ms);
// ak_sock_err is the last socket call's error (errno, WSAGetLastError).
int ak_sock_err(void);
// A socket that isn't inherited by the programs the process starts, and
// one whose calls never block.
void ak_sock_no_inherit(ak_sock s);
void ak_sock_nonblocking(ak_sock s);
// ak_sock_nosigpipe keeps a write to a connection the other side closed
// from raising SIGPIPE where MSG_NOSIGNAL doesn't (Apple's SO_NOSIGPIPE).
void ak_sock_nosigpipe(ak_sock s);
// ak_sock_address writes a's address and port as text: "203.0.113.7:80",
// "[2001:db8::1]:80"; an IPv4 address mapped into IPv6 (a dual-stack
// socket's IPv4 peer) as IPv4.
void ak_sock_address(const struct sockaddr *a, char *out, size_t cap);

// A waker wakes a thread that waits in ak_sock_poll: its fd is polled
// for POLLIN, wake makes it readable (any thread), drain empties it. A
// pipe, or on Windows a UDP socket connected to itself.
typedef struct {
#if defined(_WIN32)
	ak_sock s;
#else
	int r, w;
#endif
} ak_waker;

bool ak_waker_open(ak_waker *w);
void ak_waker_close(ak_waker *w);
ak_sock ak_waker_fd(const ak_waker *w);
void ak_waker_wake(ak_waker *w);
void ak_waker_drain(ak_waker *w);

// A map of ids (> 0) to pointers, open addressing: what a thread finds
// its connections and requests by.
typedef struct {
	int64_t *keys;  // 0 empty, -1 removed
	void **vals;
	uint32_t cap, used;  // used: live and removed
} ak_idmap;

void *ak_idmap_get(const ak_idmap *m, int64_t k);
void ak_idmap_put(ak_idmap *m, int64_t k, void *v);
void ak_idmap_del(ak_idmap *m, int64_t k);
void ak_idmap_free(ak_idmap *m);

#endif
