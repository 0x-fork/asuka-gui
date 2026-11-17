// windows_test.c: asuka_windows.h's helpers that need Windows itself
// (platform/windows' TestSharedC builds it with the shared C and runs it
// in Windows): URLs cracked for WinHTTP, any length.
#include "asuka_windows.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failed;
#define CHECK(c) do { if (!(c)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); failed++; } } while (0)

// target cracks url, checking its parts against host, object, port and https.
static void target(const char *url, const wchar_t *host, const wchar_t *object, unsigned short port, bool https) {
	wchar_t *h = NULL, *o = NULL;
	unsigned short p = 0;
	bool s = false;
	bool ok = asuka_win_http_target(url, &h, &o, &p, &s);
	if (!host) {
		CHECK(!ok && !h && !o);
		if (ok) printf("%s: cracked, wanted refused\n", url);
		return;
	}
	CHECK(ok && h && o && !wcscmp(h, host) && !wcscmp(o, object) && p == port && s == https);
	if (!ok || wcscmp(h, host) || wcscmp(o, object)) printf("%.80s: got %ls %.80ls\n", url, h ? h : L"-", o ? o : L"-");
	free(h);
	free(o);
}

int main(void) {
	target("https://example.com/a/b?x=1#frag", L"example.com", L"/a/b?x=1", 443, true);
	target("http://127.0.0.1:8080", L"127.0.0.1", L"/", 8080, false);
	target("http://h?q=1", L"h", L"/?q=1", 80, false);
	target("http://h/#only", L"h", L"/", 80, false);
	target("ftp://h/x", NULL, NULL, 0, false);
	target("not a url", NULL, NULL, 0, false);
	// Past the fixed buffers it once had: a 300-character host, a path and
	// a query of 10000 each.
	enum { n = 10000 };
	char *url = malloc(3 * n + 400);
	wchar_t *host = malloc(301 * sizeof(wchar_t)), *object = malloc((2 * n + 8) * sizeof(wchar_t));
	char *u = url + sprintf(url, "https://");
	for (int i = 0; i < 300; i++) *u++ = i % 64 == 63 ? '.' : 'a', host[i] = i % 64 == 63 ? L'.' : L'a';
	host[300] = 0;
	*u++ = '/', object[0] = L'/';
	for (int i = 0; i < n; i++) *u++ = 'p', object[1 + i] = L'p';
	*u++ = '?', object[1 + n] = L'?';
	for (int i = 0; i < n; i++) *u++ = 'q', object[2 + n + i] = L'q';
	object[2 + 2 * n] = 0;
	strcpy(u, "#fragment");
	target(url, host, object, 443, true);
	free(url), free(host), free(object);
	printf("%d failures\n", failed);
	return failed ? 1 : 0;
}
