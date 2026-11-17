// platform/webauth on Windows and Linux (macOS runs the package's Swift):
// what both share.
#include "webauth.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool webauth_is_callback(const char *url, const char *scheme) {
	size_t n = strlen(scheme);
	if (!n) return false;
	for (size_t i = 0; i < n; i++)
		if (tolower((unsigned char)url[i]) != scheme[i]) return false;  // scheme is lower case
	return url[n] == ':';
}

char *webauth_host(const char *url) {
	const char *p = strstr(url, "://");
	if (!p) return asuka_strdup("");
	p += 3;
	size_t n = strcspn(p, "/?#");
	const char *at = NULL;
	for (size_t i = 0; i < n; i++)
		if (p[i] == '@') at = p + i;
	if (at) {
		n -= (size_t)(at + 1 - p);
		p = at + 1;
	}
	return asuka_strndup(p, n);
}

void webauth_finish(signin *s, const char *to, asuka_error *err) {
	if (s->done) {
		if (err) asuka_error_free(err);
		return;
	}
	s->done = true;
	if (to) {
		webauth_Sheet_open_done(s->call, asuka_strdup(to));
	} else {
		asuka_error e = {0};
		if (err) e = *err;
		else asuka_error_declared(&e, webauth_WebAuthError_canceled);
		asuka_call_fail(s->call, &e);
	}
	webauth_close(s);
}

void webauth_free(signin *s) {
	free(s->link);
	free(s->host);
	free(s->scheme);
	free(s);
}

static void cancelled(void *p) {
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	webauth_finish(p, NULL, &e);
}

static void open_(webauth_Sheet *self, const char *url, const char *scheme, bool ephemeral, asuka_call *call) {
	bool web = (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0);
	char *host = webauth_host(url);
	if (!web || !*host) {
		free(host);
		asuka_error e = {0};
		asuka_error_failed(&e, "webauth", 0, "webauth: not a web URL: %s", url);
		asuka_call_fail(call, &e);
		return;
	}
	signin *s = calloc(1, sizeof *s);
	s->link = asuka_strdup(url);
	s->host = host;
	s->scheme = asuka_strdup(scheme);
	size_t n = strlen(s->scheme);
	if (n && s->scheme[n - 1] == ':') s->scheme[n - 1] = 0;
	for (char *p = s->scheme; *p; p++) *p = (char)tolower((unsigned char)*p);
	s->ephemeral = ephemeral;
	s->call = call;
	asuka_call_on_cancel(call, cancelled, s);
	webauth_show(s);
}

static const webauth_Sheet_vtable vt = {.open = open_};

webauth_Sheet *webauth_make_Sheet(asuka_error *err) {
	webauth_Sheet *s = malloc(sizeof *s);
	s->vt = &vt;
	return s;
}
