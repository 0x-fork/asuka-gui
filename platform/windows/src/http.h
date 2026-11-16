// http.h: a GET into a file (modutil.c's, WinHTTP), for the modules: the
// files calls name, the updater's downloads.
#ifndef ASUKA_WIN_HTTP_H
#define ASUKA_WIN_HTTP_H

#include "modules.h"

typedef struct wh_http wh_http;

struct wh_http {
	const char *url;    // http or https
	const char *to;     // the file the body goes in, made or emptied
	const char *agent;  // User-Agent (NULL: "Asuka")
	bool any_2xx;       // keep a 2xx's body, not 200's only
	// What's known as it goes: the response's status, its
	// Content-Length (0: unknown) and the bytes written.
	int status;
	uint64_t total, downloaded;
	// progress, if set, is called on the downloading thread once the
	// status is known and as bytes come.
	void (*progress)(wh_http *h);
	void *ctx;
	// To cancel from another thread: under lock, set *cancel and close
	// *request (WinHttpCloseHandle) if it's set.
	SRWLOCK *lock;
	volatile bool *cancel;
	void **request;
	// Out: the error ("canceled" when cancelled), the response's
	// Content-Disposition and Content-Type, when asked for (free them).
	char **err;
	char **disposition, **content_type;
};

// wh_http_get runs the request, blocking (never on the main thread),
// following redirects through the system's proxy settings: false with
// *err set when the network or the file failed. A status but 2xx is no
// failure: its body isn't kept.
bool wh_http_get(wh_http *h);

#endif
