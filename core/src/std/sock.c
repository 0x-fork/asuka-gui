// sock.c: sock.h's sockets, waker and map.
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif

#if !defined(__wasm__)

#include "std/sock.h"
#include "base/thread.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
static ak_mutex startup_mu = AK_MUTEX_INIT;
static int started;  // 0 not yet, 1 started, -1 failed
#endif

bool ak_sock_startup(void) {
#if defined(_WIN32)
	ak_mutex_lock(&startup_mu);
	if (!started) {
		WSADATA wd;
		started = WSAStartup(MAKEWORD(2, 2), &wd) == 0 ? 1 : -1;
	}
	bool ok = started == 1;
	ak_mutex_unlock(&startup_mu);
	return ok;
#else
	return true;
#endif
}

void ak_sock_close(ak_sock s) {
#if defined(_WIN32)
	closesocket(s);
#else
	close(s);
#endif
}

int ak_sock_poll(ak_pollfd *p, size_t n, int timeout_ms) {
#if defined(_WIN32)
	return WSAPoll(p, (ULONG)n, timeout_ms);
#else
	return poll(p, (nfds_t)n, timeout_ms);
#endif
}

int ak_sock_err(void) {
#if defined(_WIN32)
	return WSAGetLastError();
#else
	return errno;
#endif
}

void ak_sock_no_inherit(ak_sock s) {
#if defined(_WIN32)
	SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
}

void ak_sock_nonblocking(ak_sock s) {
#if defined(_WIN32)
	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);
#else
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
#endif
}

void ak_sock_nosigpipe(ak_sock s) {
#if defined(SO_NOSIGPIPE)
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
	AK_UNUSED(s);
#endif
}

void ak_sock_address(const struct sockaddr *a, char *out, size_t cap) {
	char ip[INET6_ADDRSTRLEN] = "";
	if (a->sa_family == AF_INET) {
		const struct sockaddr_in *v4 = (const struct sockaddr_in *)a;
		inet_ntop(AF_INET, (void *)&v4->sin_addr, ip, sizeof ip);
		snprintf(out, cap, "%s:%d", ip, ntohs(v4->sin_port));
		return;
	}
	if (a->sa_family == AF_INET6) {
		const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)a;
		int port = ntohs(v6->sin6_port);
		const uint8_t *b = (const uint8_t *)&v6->sin6_addr;
		static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
		if (memcmp(b, mapped, 12) == 0) {
			snprintf(out, cap, "%d.%d.%d.%d:%d", b[12], b[13], b[14], b[15], port);
			return;
		}
		inet_ntop(AF_INET6, (void *)&v6->sin6_addr, ip, sizeof ip);
		snprintf(out, cap, "[%s]:%d", ip, port);
		return;
	}
	snprintf(out, cap, "?");
}

// ---- The waker ----

bool ak_waker_open(ak_waker *w) {
#if defined(_WIN32)
	w->s = AK_BAD_SOCK;
	if (!ak_sock_startup()) return false;
	ak_sock u = socket(AF_INET, SOCK_DGRAM, 0);
	if (u == INVALID_SOCKET) return false;
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	int al = sizeof a;
	if (bind(u, (struct sockaddr *)&a, sizeof a) != 0 || getsockname(u, (struct sockaddr *)&a, &al) != 0 || connect(u, (struct sockaddr *)&a, sizeof a) != 0) {
		closesocket(u);
		return false;
	}
	ak_sock_no_inherit(u);
	ak_sock_nonblocking(u);
	w->s = u;
	return true;
#else
	int fds[2];
	w->r = w->w = -1;
	if (pipe(fds) != 0) return false;
	for (int i = 0; i < 2; i++) {
		fcntl(fds[i], F_SETFD, FD_CLOEXEC);
		fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
	}
	w->r = fds[0];
	w->w = fds[1];
	return true;
#endif
}

void ak_waker_close(ak_waker *w) {
#if defined(_WIN32)
	if (w->s != AK_BAD_SOCK) closesocket(w->s);
	w->s = AK_BAD_SOCK;
#else
	if (w->r >= 0) close(w->r);
	if (w->w >= 0) close(w->w);
	w->r = w->w = -1;
#endif
}

ak_sock ak_waker_fd(const ak_waker *w) {
#if defined(_WIN32)
	return w->s;
#else
	return w->r;
#endif
}

void ak_waker_wake(ak_waker *w) {
	char b = 1;
#if defined(_WIN32)
	send(w->s, &b, 1, 0);
#else
	ssize_t r;
	do {
		r = write(w->w, &b, 1);
	} while (r < 0 && errno == EINTR);
#endif
}

void ak_waker_drain(ak_waker *w) {
	char buf[256];
#if defined(_WIN32)
	while (recv(w->s, buf, sizeof buf, 0) > 0) {
	}
#else
	while (read(w->r, buf, sizeof buf) > 0) {
	}
#endif
}

// ---- The map ----

static uint32_t id_hash(int64_t k) {
	uint64_t x = (uint64_t)k * 0x9e3779b97f4a7c15ull;
	return (uint32_t)(x >> 32);
}

void *ak_idmap_get(const ak_idmap *m, int64_t k) {
	if (!m->cap) return NULL;
	for (uint32_t i = id_hash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) return m->vals[i];
		if (m->keys[i] == 0) return NULL;
	}
}

static void idmap_grow(ak_idmap *m) {
	ak_idmap n = {.cap = m->cap ? m->cap * 2 : 16};
	// Removed entries take room too: a map that churns rehashes at its
	// size, not larger.
	uint32_t live = 0;
	for (uint32_t i = 0; i < m->cap; i++) live += m->keys[i] > 0;
	if (m->cap && live * 4 < m->cap) n.cap = m->cap;
	n.keys = ak_alloc(sizeof(int64_t) * n.cap);
	n.vals = ak_alloc(sizeof(void *) * n.cap);
	for (uint32_t i = 0; i < m->cap; i++)
		if (m->keys[i] > 0) ak_idmap_put(&n, m->keys[i], m->vals[i]);
	ak_free(m->keys);
	ak_free(m->vals);
	*m = n;
}

void ak_idmap_put(ak_idmap *m, int64_t k, void *v) {
	if ((m->used + 1) * 2 > m->cap) idmap_grow(m);
	for (uint32_t i = id_hash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) {
			m->vals[i] = v;
			return;
		}
		if (m->keys[i] == 0) {
			m->keys[i] = k;
			m->vals[i] = v;
			m->used++;
			return;
		}
	}
}

void ak_idmap_del(ak_idmap *m, int64_t k) {
	if (!m->cap) return;
	for (uint32_t i = id_hash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
		if (m->keys[i] == k) {
			m->keys[i] = -1;
			m->vals[i] = NULL;
			return;
		}
		if (m->keys[i] == 0) return;
	}
}

void ak_idmap_free(ak_idmap *m) {
	ak_free(m->keys);
	ak_free(m->vals);
	*m = (ak_idmap){0};
}

#else

typedef int ak_sock_unused;  // ISO C: a file has a declaration

#endif
