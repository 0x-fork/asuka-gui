// fetch.c (platform/desktop/src): asuka_desktop.h's asuka_fetch (WinHTTP
// on Windows, libsoup 3 on Linux, both with the system's proxies and
// certificates) and asuka_desktop_local_file.
#ifdef _WIN32
#include "asuka_windows.h"
#include <winhttp.h>
#define strncasecmp _strnicmp
#else
#include "asuka_linux.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "asuka_desktop.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void asuka_response_free(asuka_response *r) {
	free(r->url);
	for (size_t i = 0; i < r->nheaders; i++) {
		free(r->names[i]);
		free(r->values[i]);
	}
	free(r->names);
	free(r->values);
	free(r->body.data);
	*r = (asuka_response){0};
}

// add_header adds a response header, joining a name's values.
static void add_header(asuka_response *r, const char *name, size_t nn, const char *value, size_t vn) {
	char *n = asuka_strndup(name, nn);
	for (char *p = n; *p; p++) *p = (char)tolower((unsigned char)*p);
	while (vn > 0 && (value[0] == ' ' || value[0] == '\t')) value++, vn--;
	while (vn > 0 && (value[vn - 1] == ' ' || value[vn - 1] == '\t' || value[vn - 1] == '\r')) vn--;
	for (size_t i = 0; i < r->nheaders; i++) {
		if (strcmp(r->names[i], n) == 0) {
			char *v = asuka_sprintf("%s, %.*s", r->values[i], (int)vn, value);
			free(r->values[i]);
			r->values[i] = v;
			free(n);
			return;
		}
	}
	r->names = realloc(r->names, (r->nheaders + 1) * sizeof *r->names);
	r->values = realloc(r->values, (r->nheaders + 1) * sizeof *r->values);
	r->names[r->nheaders] = n;
	r->values[r->nheaders++] = asuka_strndup(value, vn);
}

static const char *header_of(const asuka_response *r, const char *name) {
	for (size_t i = 0; i < r->nheaders; i++)
		if (strcmp(r->names[i], name) == 0) return r->values[i];
	return NULL;
}

#ifdef _WIN32

typedef struct {
	HINTERNET request;
	SRWLOCK mu;
	bool done;
} cancel_ctx;

static void on_cancel(void *p) {
	cancel_ctx *c = p;
	AcquireSRWLockExclusive(&c->mu);
	if (!c->done && c->request) WinHttpCloseHandle(c->request), c->request = NULL;
	ReleaseSRWLockExclusive(&c->mu);
}

bool asuka_fetch(const char *method, const char *url, const asuka_header *headers, size_t nheaders, const void *body, size_t len,
                 int timeout_ms, asuka_call *cancel, asuka_response *out, asuka_error *err) {
	*out = (asuka_response){0};
	wchar_t *wurl = asuka_wide(url), *wmethod = asuka_wide(method && *method ? method : "GET");
	URL_COMPONENTS u;
	memset(&u, 0, sizeof u);
	u.dwStructSize = sizeof u;
	wchar_t host[256], path[4096];
	u.lpszHostName = host, u.dwHostNameLength = 256;
	u.lpszUrlPath = path, u.dwUrlPathLength = 4096;
	wchar_t extra[4096];
	u.lpszExtraInfo = extra, u.dwExtraInfoLength = 4096;
	HINTERNET session = NULL, conn = NULL, req = NULL;
	bool ok = false;
	cancel_ctx *cc = NULL;
	if (!WinHttpCrackUrl(wurl, 0, 0, &u)) {
		asuka_error_failed(err, "http", 0, "%s isn't a URL", url);
		goto done;
	}
	session = WinHttpOpen(L"Asuka", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) session = WinHttpOpen(L"Asuka", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) goto fail;
	if (timeout_ms > 0) WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
	conn = WinHttpConnect(session, host, u.nPort, 0);
	if (!conn) goto fail;
	size_t pn = wcslen(path), en = wcslen(extra);
	wchar_t *object = malloc((pn + en + 2) * sizeof(wchar_t));
	wcscpy(object, pn ? path : L"/");
	wcscat(object, extra);
	req = WinHttpOpenRequest(conn, wmethod, object, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
	free(object);
	if (!req) goto fail;
	DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
	WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof decompress);
	for (size_t i = 0; i < nheaders; i++) {
		char *line = asuka_sprintf("%s: %s", headers[i].name, headers[i].value);
		wchar_t *w = asuka_wide(line);
		WinHttpAddRequestHeaders(req, w, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
		free(w);
		free(line);
	}
	if (cancel) {
		cc = calloc(1, sizeof *cc);
		InitializeSRWLock(&cc->mu);
		cc->request = req;
		asuka_call_on_cancel(cancel, on_cancel, cc);
	}
	if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (void *)body, (DWORD)len, (DWORD)len, 0) || !WinHttpReceiveResponse(req, NULL)) goto fail;
	DWORD status = 0, n = sizeof status;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX);
	out->status = (int)status;
	n = 0;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &n, WINHTTP_NO_HEADER_INDEX);
	if (n) {
		wchar_t *raw = malloc(n + sizeof(wchar_t));
		if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw, &n, WINHTTP_NO_HEADER_INDEX)) {
			char *all = asuka_narrow(raw, (int)(n / sizeof(wchar_t)));
			char *line = strchr(all, '\n');  // past the status line
			while (line && *++line) {
				char *end = strchr(line, '\n');
				char *colon = memchr(line, ':', end ? (size_t)(end - line) : strlen(line));
				if (colon) add_header(out, line, (size_t)(colon - line), colon + 1, end ? (size_t)(end - colon - 1) : strlen(colon + 1));
				line = end;
			}
			free(all);
		}
		free(raw);
	}
	n = 0;
	WinHttpQueryOption(req, WINHTTP_OPTION_URL, NULL, &n);
	if (n) {
		wchar_t *final = malloc(n + sizeof(wchar_t));
		if (WinHttpQueryOption(req, WINHTTP_OPTION_URL, final, &n)) out->url = asuka_narrow(final, (int)(n / sizeof(wchar_t)));
		free(final);
	}
	if (!out->url) out->url = asuka_strdup(url);
	asuka_writer b = {0};
	for (;;) {
		DWORD avail = 0, got = 0;
		if (!WinHttpQueryDataAvailable(req, &avail)) {
			asuka_writer_free(&b);
			goto fail;
		}
		if (avail == 0) break;
		uint8_t *buf = malloc(avail);
		if (!WinHttpReadData(req, buf, avail, &got)) {
			free(buf);
			asuka_writer_free(&b);
			goto fail;
		}
		asuka_writer_put(&b, buf, got);
		free(buf);
		if (got == 0) break;
	}
	out->body = (asuka_bytes){b.data ? b.data : malloc(1), b.len};
	ok = true;
	goto done;
fail:;
	DWORD code = GetLastError();
	if (cancel && asuka_call_cancelled(cancel)) asuka_error_cancelled(err);
	else if (code == ERROR_WINHTTP_TIMEOUT) asuka_error_failed(err, "http", 0, "%s: timed out", url);
	else asuka_win_fail(err, HRESULT_FROM_WIN32(code), url);
	asuka_response_free(out);
done:
	if (cc) {
		AcquireSRWLockExclusive(&cc->mu);
		cc->done = true;
		req = cc->request;  // NULL if the cancel closed it
		ReleaseSRWLockExclusive(&cc->mu);
		// cc stays: asuka_call_on_cancel's ctx must outlive the call, which
		// answers after this returns. It's small; the call is one request.
	}
	if (req) WinHttpCloseHandle(req);
	if (conn) WinHttpCloseHandle(conn);
	if (session) WinHttpCloseHandle(session);
	free(wurl);
	free(wmethod);
	return ok;
}

#else  // Linux: libsoup 3, resolved at run time.

typedef struct _SoupSession SoupSession;
typedef struct _SoupMessage SoupMessage;
typedef struct _SoupMessageHeaders SoupMessageHeaders;
typedef struct _GUri GUri;

static struct {
	SoupSession *(*session_new)(void);
	void (*session_set_timeout)(SoupSession *, guint);
	SoupMessage *(*message_new)(const char *, const char *);
	SoupMessageHeaders *(*request_headers)(SoupMessage *);
	SoupMessageHeaders *(*response_headers)(SoupMessage *);
	void (*headers_append)(SoupMessageHeaders *, const char *, const char *);
	void (*headers_foreach)(SoupMessageHeaders *, void (*)(const char *, const char *, gpointer), gpointer);
	void (*set_request_body_from_bytes)(SoupMessage *, const char *, GBytes *);
	GBytes *(*send_and_read)(SoupSession *, SoupMessage *, GCancellable *, GError **);
	guint (*get_status)(SoupMessage *);
	GUri *(*get_uri)(SoupMessage *);
	char *(*uri_to_string)(GUri *);
	bool loaded;
} soup;

static void load_soup(void) {
	static bool tried;
	if (tried) return;
	tried = true;
	if (!asuka_linux()) return;
	void *l = dlopen("libsoup-3.0.so.0", RTLD_NOW | RTLD_GLOBAL);
	if (!l) return;
	*(void **)&soup.session_new = dlsym(l, "soup_session_new");
	*(void **)&soup.session_set_timeout = dlsym(l, "soup_session_set_timeout");
	*(void **)&soup.message_new = dlsym(l, "soup_message_new");
	*(void **)&soup.request_headers = dlsym(l, "soup_message_get_request_headers");
	*(void **)&soup.response_headers = dlsym(l, "soup_message_get_response_headers");
	*(void **)&soup.headers_append = dlsym(l, "soup_message_headers_append");
	*(void **)&soup.headers_foreach = dlsym(l, "soup_message_headers_foreach");
	*(void **)&soup.set_request_body_from_bytes = dlsym(l, "soup_message_set_request_body_from_bytes");
	*(void **)&soup.send_and_read = dlsym(l, "soup_session_send_and_read");
	*(void **)&soup.get_status = dlsym(l, "soup_message_get_status");
	*(void **)&soup.get_uri = dlsym(l, "soup_message_get_uri");
	*(void **)&soup.uri_to_string = dlsym(l, "g_uri_to_string");
	soup.loaded = soup.session_new && soup.message_new && soup.send_and_read && soup.get_status;
}

static void each_header(const char *name, const char *value, gpointer r) { add_header(r, name, strlen(name), value, strlen(value)); }

static void cancel_soup(void *c) { G(g_cancellable_cancel)(c); }

bool asuka_fetch(const char *method, const char *url, const asuka_header *headers, size_t nheaders, const void *body, size_t len,
                 int timeout_ms, asuka_call *cancel, asuka_response *out, asuka_error *err) {
	static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&mu);
	load_soup();
	pthread_mutex_unlock(&mu);
	*out = (asuka_response){0};
	if (!soup.loaded) {
		asuka_error_unsupported(err, "HTTP needs libsoup 3 (libsoup-3.0.so.0)");
		return false;
	}
	SoupMessage *msg = soup.message_new(method && *method ? method : "GET", url);
	if (!msg) {
		asuka_error_failed(err, "http", 0, "%s isn't a URL", url);
		return false;
	}
	SoupSession *s = soup.session_new();
	if (timeout_ms > 0 && soup.session_set_timeout) soup.session_set_timeout(s, (guint)((timeout_ms + 999) / 1000));
	SoupMessageHeaders *h = soup.request_headers(msg);
	const char *type = NULL;
	for (size_t i = 0; i < nheaders; i++) {
		if (strcasecmp(headers[i].name, "content-type") == 0) type = headers[i].value;
		else soup.headers_append(h, headers[i].name, headers[i].value);
	}
	if (len > 0 || type) {
		GBytes *b = G(g_bytes_new)(body, len);
		soup.set_request_body_from_bytes(msg, type ? type : "application/octet-stream", b);
		G(g_bytes_unref)(b);
	}
	GCancellable *c = G(g_cancellable_new)();
	if (cancel) {
		G(g_object_ref)(c);  // the cancel's, which may come after this returns
		asuka_call_on_cancel(cancel, cancel_soup, c);
	}
	GError *e = NULL;
	GBytes *got = soup.send_and_read(s, msg, c, &e);
	bool ok = got != NULL;
	if (ok) {
		out->status = (int)soup.get_status(msg);
		GUri *u = soup.get_uri ? soup.get_uri(msg) : NULL;
		char *us = u && soup.uri_to_string ? soup.uri_to_string(u) : NULL;
		out->url = asuka_strdup(us ? us : url);
		G(g_free)(us);
		if (soup.headers_foreach) soup.headers_foreach(soup.response_headers(msg), each_header, out);
		gsize n = 0;
		const void *p = G(g_bytes_get_data)(got, &n);
		out->body = asuka_bytes_copy(p ? p : "", n);
		G(g_bytes_unref)(got);
	} else if (cancel && asuka_call_cancelled(cancel)) {
		if (e) G(g_error_free)(e);
		asuka_error_cancelled(err);
	} else {
		asuka_linux_fail(err, e, url);
	}
	G(g_object_unref)(c);  // the cancel's reference stays until the process ends: one per request
	G(g_object_unref)(msg);
	G(g_object_unref)(s);
	return ok;
}

#endif

// ---- Local files ----

static bool is_file(const char *p) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(p);
	DWORD a = GetFileAttributesW(w);
	free(w);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
	struct stat st;
	return stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
#endif
}

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

// unescape is a URL's percent-escapes decoded.
static char *unescape(const char *s) {
	char *d = asuka_strdup(s), *o = d;
	for (const char *p = s; *p; p++) {
		if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
			char h[3] = {p[1], p[2], 0};
			*o++ = (char)strtol(h, NULL, 16);
			p += 2;
		} else {
			*o++ = *p;
		}
	}
	*o = 0;
	return d;
}

static const char *ext_of(const char *type) {
	static const char *table[][2] = {
	    {"application/pdf", ".pdf"},  {"image/png", ".png"},      {"image/jpeg", ".jpg"},      {"image/gif", ".gif"},
	    {"image/webp", ".webp"},      {"image/svg+xml", ".svg"},  {"image/heic", ".heic"},     {"text/plain", ".txt"},
	    {"text/html", ".html"},       {"text/csv", ".csv"},       {"application/json", ".json"}, {"application/zip", ".zip"},
	    {"audio/mpeg", ".mp3"},       {"audio/wav", ".wav"},      {"video/mp4", ".mp4"},       {"video/quicktime", ".mov"},
	    {"application/msword", ".doc"}, {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", ".docx"},
	    {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", ".xlsx"},
	    {"application/vnd.openxmlformats-officedocument.presentationml.presentation", ".pptx"},
	};
	if (!type) return NULL;
	size_t n = strcspn(type, ";");
	while (n > 0 && type[n - 1] == ' ') n--;
	for (size_t i = 0; i < sizeof table / sizeof *table; i++)
		if (strlen(table[i][0]) == n && strncasecmp(table[i][0], type, n) == 0) return table[i][1];
	return NULL;
}

static char *download(const char *source, const char *type, asuka_error *err) {
	asuka_response r;
	if (!asuka_fetch("GET", source, NULL, 0, NULL, 0, 0, NULL, &r, err)) return NULL;
	if (r.status / 100 != 2) {
		asuka_error_failed(err, "http", r.status, "couldn't download %s: %d", source, r.status);
		asuka_response_free(&r);
		return NULL;
	}
	char *name = NULL;
	const char *cd = header_of(&r, "content-disposition");
	const char *fn = cd ? strstr(cd, "filename=") : NULL;
	if (fn) {
		fn += 9;
		bool q = *fn == '"';
		if (q) fn++;
		name = asuka_strndup(fn, strcspn(fn, q ? "\"" : ";"));
	}
	if (!name || !*name) {
		free(name);
		const char *path = strstr(source, "://");
		path = path ? strchr(path + 3, '/') : NULL;
		size_t n = path ? strcspn(path, "?#") : 0;
		const char *base = path;
		for (size_t i = 0; path && i < n; i++)
			if (path[i] == '/') base = path + i + 1;
		char *raw = base ? asuka_strndup(base, n - (size_t)(base - path)) : asuka_strdup("");
		name = unescape(raw);
		free(raw);
	}
	for (char *p = name; *p; p++)
		if (*p == '/' || *p == '\\' || *p == ':') *p = '_';
	if (!*name || strcmp(name, ".") == 0 || strcmp(name, "_") == 0) {
		free(name);
		name = asuka_strdup("file");
	}
	if (!strrchr(name, '.')) {
		const char *e = ext_of(type && *type ? type : header_of(&r, "content-type"));
		if (e) {
			char *n = asuka_sprintf("%s%s", name, e);
			free(name);
			name = n;
		}
	}
	uint64_t id = 0;
	for (int i = 0; i < 4; i++) id = id << 16 ^ (uint64_t)rand();
	char *dir = asuka_sprintf("%s" SEP "asuka-files" SEP "%016llx", asuka_desktop_cache_dir(), (unsigned long long)id);
	char *path = asuka_sprintf("%s" SEP "%s", dir, name);
	free(name);
	bool ok = false;
#ifdef _WIN32
	char *parent = asuka_sprintf("%s" SEP "asuka-files", asuka_desktop_cache_dir());
	wchar_t *wp = asuka_wide(parent), *wd = asuka_wide(dir), *wf = asuka_wide(path);
	CreateDirectoryW(wp, NULL);
	CreateDirectoryW(wd, NULL);
	FILE *f = _wfopen(wf, L"wb");
	free(wp), free(wd), free(wf), free(parent);
#else
	char *parent = asuka_sprintf("%s" SEP "asuka-files", asuka_desktop_cache_dir());
	mkdir(parent, 0755);
	mkdir(dir, 0755);
	free(parent);
	FILE *f = fopen(path, "wb");
#endif
	if (f) {
		ok = fwrite(r.body.data, 1, r.body.len, f) == r.body.len;
		ok = fclose(f) == 0 && ok;
	}
	asuka_response_free(&r);
	free(dir);
	if (!ok) {
		asuka_error_failed(err, "file", 0, "couldn't write %s", path);
		free(path);
		return NULL;
	}
	return path;
}

char *asuka_desktop_local_file(const char *source, const char *type, asuka_error *err) {
	if (strncmp(source, "http://", 7) == 0 || strncmp(source, "https://", 8) == 0) return download(source, type, err);
	char *p;
	if (strncmp(source, "file://", 7) == 0) {
		const char *rest = source + 7;
		if (strncmp(rest, "localhost/", 10) == 0) rest += 9;
		p = unescape(rest);
#ifdef _WIN32
		if (p[0] == '/' && p[1] && p[2] == ':') memmove(p, p + 1, strlen(p));  // file:///C:/...
		for (char *q = p; *q; q++)
			if (*q == '/') *q = '\\';
#endif
	} else {
#ifdef _WIN32
		bool abs = (source[0] && source[1] == ':') || (source[0] == '\\' && source[1] == '\\');
#else
		bool abs = source[0] == '/';
#endif
		p = abs ? asuka_strdup(source) : asuka_sprintf("%s" SEP "%s", asuka_desktop_assets_dir(), source);
#ifdef _WIN32
		for (char *q = p; *q; q++)
			if (*q == '/') *q = '\\';
#endif
	}
	if (!is_file(p)) {
		asuka_error_failed(err, "file", 0, "no file at %s", source);
		free(p);
		return NULL;
	}
	return p;
}
