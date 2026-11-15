#define _POSIX_C_SOURCE 200809L
#include "t.h"
#include "abi/dev.h"
#include "rt/sched.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

static uint8_t got[64];
static size_t got_len;

static void reload(const uint8_t *p, size_t n) {
	memcpy(got, p, n < sizeof got ? n : sizeof got);
	got_len = n;
}

static bool read_frame(int fd, uint8_t *kind, uint8_t *buf, size_t cap, size_t *n) {
	uint8_t h[5];
	size_t off = 0;
	while (off < 5) {
		ssize_t r = recv(fd, h + off, 5 - off, 0);
		if (r <= 0) return false;
		off += (size_t)r;
	}
	uint32_t len = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
	*kind = h[4];
	*n = len - 1;
	off = 0;
	while (off < *n && off < cap) {
		ssize_t r = recv(fd, buf + off, *n - off, 0);
		if (r <= 0) return false;
		off += (size_t)r;
	}
	return true;
}

TEST(module_reaches_the_main_thread) {
	int ln = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001)};
	REQUIRE(bind(ln, (struct sockaddr *)&a, sizeof a) == 0);
	socklen_t al = sizeof a;
	getsockname(ln, (struct sockaddr *)&a, &al);
	listen(ln, 1);
	ak_dev_set_reload(reload);
	char err[128];
	REQUIRE(ak_dev_connect("127.0.0.1", ntohs(a.sin_port), "{\"platform\":\"test\"}", false, err, sizeof err));
	int c = accept(ln, NULL, NULL);
	REQUIRE(c >= 0);
	uint8_t kind, buf[64];
	size_t n;
	REQUIRE(read_frame(c, &kind, buf, sizeof buf, &n));
	CHECK_EQ_INT(kind, AK_DEV_HELLO);
	CHECK(n == 19 && memcmp(buf, "{\"platform\":\"test\"}", 19) == 0);
	uint8_t frame[] = {6, 0, 0, 0, AK_DEV_MODULE, 'A', 'K', 'B', 'C', '!'};
	send(c, frame, sizeof frame, 0);
	// Wait for it (a condition, not a time).
	for (int i = 0; i < 2000 && got_len == 0; i++) {
		ak_sched_run_posted();
		struct timespec ts = {0, 1000000};
		nanosleep(&ts, NULL);
	}
	CHECK_EQ_INT(got_len, 5);
	CHECK(memcmp(got, "AKBC!", 5) == 0);
	ak_dev_send(AK_DEV_RELOADED, "\x03\0\0\0\x10\0\0\0", 8);
	REQUIRE(read_frame(c, &kind, buf, sizeof buf, &n));
	CHECK_EQ_INT(kind, AK_DEV_RELOADED);
	close(c);
	close(ln);
}

T_MAIN()
