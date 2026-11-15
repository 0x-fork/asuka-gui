#define _POSIX_C_SOURCE 200809L  // sockets, pipes and dup under -std=c17
#include "abi/dev.h"
#include "base/thread.h"
#include "rt/sched.h"

#include <stdio.h>

#if defined(__wasm__)

void ak_dev_set_reload(void (*reload)(const uint8_t *, size_t)) { AK_UNUSED(reload); }
bool ak_dev_connect(const char *host, int port, const char *hello, bool capture, char *err, size_t errcap) {
	AK_UNUSED(host);
	AK_UNUSED(port);
	AK_UNUSED(hello);
	AK_UNUSED(capture);
	snprintf(err, errcap, "the web reloads the page instead");
	return false;
}
void ak_dev_send(uint8_t kind, const void *p, size_t len) {
	AK_UNUSED(kind);
	AK_UNUSED(p);
	AK_UNUSED(len);
}
bool ak_dev_connected(void) { return false; }

#else

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define close_sock closesocket
#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define close_sock close
#endif

static void (*reload_fn)(const uint8_t *, size_t);
static sock_t sock = BAD_SOCK;
static ak_mutex send_mu = AK_MUTEX_INIT;

void ak_dev_set_reload(void (*reload)(const uint8_t *, size_t)) { reload_fn = reload; }

bool ak_dev_connected(void) { return sock != BAD_SOCK; }

static bool write_all(sock_t s, const uint8_t *p, size_t n) {
	while (n > 0) {
#if defined(_WIN32)
		int w = send(s, (const char *)p, (int)n, 0);
#else
		ssize_t w = send(s, p, n, 0);
#endif
		if (w <= 0) return false;
		p += w;
		n -= (size_t)w;
	}
	return true;
}

static bool read_all(sock_t s, uint8_t *p, size_t n) {
	while (n > 0) {
#if defined(_WIN32)
		int r = recv(s, (char *)p, (int)n, 0);
#else
		ssize_t r = recv(s, p, n, 0);
#endif
		if (r <= 0) return false;
		p += r;
		n -= (size_t)r;
	}
	return true;
}

void ak_dev_send(uint8_t kind, const void *p, size_t len) {
	if (sock == BAD_SOCK) return;
	uint8_t h[5] = {(uint8_t)(len + 1), (uint8_t)((len + 1) >> 8), (uint8_t)((len + 1) >> 16), (uint8_t)((len + 1) >> 24), kind};
	ak_mutex_lock(&send_mu);
	if (!write_all(sock, h, 5) || !write_all(sock, p, len)) {
		// The CLI went away: the app keeps running as it is.
	}
	ak_mutex_unlock(&send_mu);
}

typedef struct {
	uint8_t *p;
	size_t len;
} module_t;

static void deliver(void *ctx) {
	module_t *m = ctx;
	if (reload_fn) reload_fn(m->p, m->len);
	ak_free(m->p);
	ak_free(m);
}

static void reader(void *arg) {
	AK_UNUSED(arg);
	for (;;) {
		uint8_t h[5];
		if (!read_all(sock, h, 5)) break;
		uint32_t n = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
		if (n == 0 || n > (256u << 20)) break;
		uint8_t *p = ak_alloc(n);
		if (!read_all(sock, p, n - 1)) {
			ak_free(p);
			break;
		}
		if (h[4] == AK_DEV_MODULE) {
			module_t *m = ak_alloc(sizeof *m);
			m->p = p;
			m->len = n - 1;
			ak_post(deliver, m);
		} else {
			ak_free(p);
		}
	}
	fprintf(stderr, "asuka: the dev connection closed; the app keeps running as it is\n");
}

#if !defined(_WIN32)
// Logs: what the process writes to stdout and stderr goes through pipes
// read on threads that send it to the CLI, and to where it went before.
typedef struct {
	int fd, orig;
	uint8_t which;  // 1 stdout, 2 stderr
} pipe_t;

static void pump(void *arg) {
	pipe_t *p = arg;
	char buf[4096];
	for (;;) {
		ssize_t n = read(p->fd, buf + 1, sizeof buf - 1);
		if (n <= 0) break;
		if (write(p->orig, buf + 1, (size_t)n) < 0) {
		}
		buf[0] = (char)p->which;
		ak_dev_send(AK_DEV_LOG, buf, (size_t)n + 1);
	}
}

static void capture_fd(int fd) {
	int fds[2];
	if (pipe(fds) != 0) return;
	pipe_t *p = ak_alloc(sizeof *p);
	p->orig = dup(fd);
	p->fd = fds[0];
	p->which = fd == STDERR_FILENO ? 2 : 1;
	dup2(fds[1], fd);
	close(fds[1]);
	if (fd == STDOUT_FILENO) setvbuf(stdout, NULL, _IOLBF, 0);
	ak_thread_start(pump, p);
}
#endif

bool ak_dev_connect(const char *host, int port, const char *hello, bool capture, char *err, size_t errcap) {
#if defined(_WIN32)
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	char ports[16];
	snprintf(ports, sizeof ports, "%d", port);
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int rc = getaddrinfo(host, ports, &hints, &res);
	if (rc != 0 || !res) {
		snprintf(err, errcap, "asuka dev at %s:%d: %s", host, port, gai_strerror(rc));
		return false;
	}
	sock_t s = BAD_SOCK;
	for (struct addrinfo *a = res; a; a = a->ai_next) {
		s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (s == BAD_SOCK) continue;
		if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
		close_sock(s);
		s = BAD_SOCK;
	}
	freeaddrinfo(res);
	if (s == BAD_SOCK) {
		snprintf(err, errcap, "asuka dev at %s:%d isn't answering", host, port);
		return false;
	}
	int one = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
	sock = s;
	if (hello) ak_dev_send(AK_DEV_HELLO, hello, strlen(hello));
#if !defined(_WIN32)
	if (capture) {
		capture_fd(STDOUT_FILENO);
		capture_fd(STDERR_FILENO);
	}
#else
	AK_UNUSED(capture);
#endif
	if (!ak_thread_start(reader, NULL)) {
		snprintf(err, errcap, "no thread for the dev connection");
		return false;
	}
	return true;
}

#endif
