// update.c: the fake host's "update" module (DESIGN.md, "Updates": the
// module a desktop host implements for asuka/update), for an app's tests.
// It has no module until a test sets one up, as the fake host has no
// installed app: until then every method answers "no module", and the
// package says updates aren't available. A test (asuka/update's
// fakeHost) makes it answer as an installed app's host would, with
// methods no real host has:
//
//	fake {…info…}            info's answer from now on; "installError"
//	                         makes install fail with it
//	serve {url, file, status, error, hold}
//	                         what a download of url gets: file's bytes
//	                         (none: a 404), or status, or an error; hold
//	                         keeps it at 0 bytes until cancelled
//	log                      {downloads: [url], installed: [staged],
//	                         canceled: n}
//
// and those of the module: info, download {url, to} -> {id}, next {id}
// -> {downloaded, total, done, error, status} (a quarter of the file at a
// time), cancel {id}, install {staged}. Downloads are copies of files,
// written to `to` as a host writes what it downloads.
#include "fake/fake.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
	ak_buf url, file, error;
	int status;
	bool hold;
} served;

typedef struct {
	uint32_t id;
	ak_buf to, error, data;
	int status;
	size_t done;
	bool finished, canceled, hold;
	// A next held (hold): its call and root.
	bool waiting;
	uint32_t call;
	ak_id root;
} download;

struct ak_fake_update {
	bool on;
	ak_buf info, install_error;
	AK_VEC(served *) served;
	AK_VEC(download *) downloads;
	ak_buf log_downloads, log_installed;  // JSON strings, a comma before each but the first
	uint32_t next_id, canceled;
};

static void answer(ak_fake *f, ak_id root, uint32_t call, bool ok, const char *p, size_t n) {
	ak_buf res = {0};
	ak_buf_u32(&res, call);
	ak_buf_u8(&res, ok ? 1 : 0);
	ak_buf_put(&res, p, n);
	ak_fake_post_event(f, &(ak_event){.id = root, .kind = AK_EVENT_RESULT, .s = {(const char *)res.data, (uint32_t)res.len}});
	ak_buf_free(&res);
}

static void answer_buf(ak_fake *f, ak_id root, uint32_t call, bool ok, const ak_buf *b) { answer(f, root, call, ok, (const char *)b->data, b->len); }
static void answer_str(ak_fake *f, ak_id root, uint32_t call, bool ok, const char *s) { answer(f, root, call, ok, s, strlen(s)); }

void ak_fake_update_free(ak_fake *f) {
	struct ak_fake_update *u = f->update;
	if (!u) return;
	for (uint32_t i = 0; i < u->served.len; i++) {
		served *s = u->served.data[i];
		ak_buf_free(&s->url), ak_buf_free(&s->file), ak_buf_free(&s->error);
		ak_free(s);
	}
	for (uint32_t i = 0; i < u->downloads.len; i++) {
		download *d = u->downloads.data[i];
		ak_buf_free(&d->to), ak_buf_free(&d->error), ak_buf_free(&d->data);
		ak_free(d);
	}
	ak_vec_free(&u->served);
	ak_vec_free(&u->downloads);
	ak_buf_free(&u->info), ak_buf_free(&u->install_error);
	ak_buf_free(&u->log_downloads), ak_buf_free(&u->log_installed);
	ak_free(u);
	f->update = NULL;
}

// ---- The JSON of the calls (what asuka/json writes: no spaces) ----

// find returns where the value of key starts in args, or NULL.
static const char *find(ak_span args, const char *key) {
	char k[40];
	int kn = snprintf(k, sizeof k, "\"%s\":", key);
	for (uint32_t i = 0; i + (uint32_t)kn <= args.len; i++)
		if (memcmp(args.p + i, k, (size_t)kn) == 0) return args.p + i + kn;
	return NULL;
}

// get_str appends the string value of key, its escapes undone; false if
// there's none.
static bool get_str(ak_span args, const char *key, ak_buf *out) {
	const char *p = find(args, key), *end = args.p + args.len;
	if (!p || p >= end || *p != '"') return false;
	for (p++; p < end && *p != '"'; p++) {
		char c = *p;
		if (c == '\\' && p + 1 < end) {
			c = *++p;
			if (c == 'n') c = '\n';
			else if (c == 't') c = '\t';
			else if (c == 'u' && p + 4 < end) {
				char hex[5] = {p[1], p[2], p[3], p[4], 0};
				c = (char)strtol(hex, NULL, 16);  // ASCII, in tests
				p += 4;
			}
		}
		ak_buf_put(out, &c, 1);
	}
	return true;
}

static int64_t get_int(ak_span args, const char *key, int64_t def) {
	const char *p = find(args, key);
	if (!p) return def;
	return strtoll(p, NULL, 10);
}

static bool get_bool(ak_span args, const char *key) {
	const char *p = find(args, key);
	return p && (size_t)(args.p + args.len - p) >= 4 && memcmp(p, "true", 4) == 0;
}

static void put_json_str(ak_buf *b, const char *s, size_t n) {
	ak_buf_put(b, "\"", 1);
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c == '"' || c == '\\') ak_buf_put(b, "\\", 1);
		if ((unsigned char)c < 0x20) {
			ak_buf_printf(b, "\\u%04x", c);
			continue;
		}
		ak_buf_put(b, &c, 1);
	}
	ak_buf_put(b, "\"", 1);
}

static bool span_is(ak_span s, const char *c) { return ak_span_eq(s, ak_cstr(c)); }

static served *served_at(struct ak_fake_update *u, const ak_buf *url) {
	for (uint32_t i = u->served.len; i-- > 0;) {
		served *s = u->served.data[i];
		if (s->url.len == url->len && memcmp(s->url.data, url->data, url->len) == 0) return s;
	}
	return NULL;
}

static download *download_of(struct ak_fake_update *u, uint32_t id) {
	for (uint32_t i = 0; i < u->downloads.len; i++)
		if (u->downloads.data[i]->id == id) return u->downloads.data[i];
	return NULL;
}

static void progress(const download *d, ak_buf *out) {
	ak_buf_printf(out, "{\"downloaded\":%zu,\"total\":%zu,\"done\":%s,\"status\":%d,\"error\":", d->done, d->data.len, d->finished ? "true" : "false", d->status);
	put_json_str(out, (const char *)d->error.data, d->error.len);
	ak_buf_put(out, "}", 1);
}

// step moves d on by a quarter of its file, writing it to its file.
static void step(download *d) {
	if (d->finished) return;
	if (d->status != 200 || d->error.len) {
		d->finished = true;
		return;
	}
	size_t q = d->data.len / 4 + 1;
	size_t k = d->data.len - d->done < q ? d->data.len - d->done : q;
	ak_buf_put(&d->to, "", 1);
	FILE *out = fopen((const char *)d->to.data, d->done ? "ab" : "wb");
	d->to.len--;
	if (!out || (k && fwrite(d->data.data + d->done, 1, k, out) != k)) {
		if (out) fclose(out);
		ak_buf_printf(&d->error, "can't write %.*s", (int)d->to.len, (const char *)d->to.data);
		d->finished = true;
		return;
	}
	fclose(out);
	d->done += k;
	if (d->done == d->data.len) d->finished = true;
}

static bool read_file(const ak_buf *path, ak_buf *out) {
	ak_buf p = {0};
	ak_buf_put(&p, path->data, path->len);
	ak_buf_put(&p, "", 1);
	FILE *f = fopen((const char *)p.data, "rb");
	ak_buf_free(&p);
	if (!f) return false;
	uint8_t buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) ak_buf_put(out, buf, n);
	fclose(f);
	return true;
}

bool ak_fake_update_call(ak_fake *f, ak_id root, uint32_t call, ak_span module, ak_span method, ak_span args) {
	if (!span_is(module, "update")) return false;
	if (!f->update) {
		f->update = ak_alloc(sizeof *f->update);
		memset(f->update, 0, sizeof *f->update);
	}
	struct ak_fake_update *u = f->update;
	ak_buf out = {0};
	if (span_is(method, "fake")) {
		u->on = true;
		u->info.len = 0;
		ak_buf_put(&u->info, args.p, args.len);
		u->install_error.len = 0;
		get_str(args, "installError", &u->install_error);
		answer_str(f, root, call, true, "{}");
	} else if (span_is(method, "serve")) {
		served *s = ak_alloc(sizeof *s);
		memset(s, 0, sizeof *s);
		get_str(args, "url", &s->url);
		get_str(args, "file", &s->file);
		get_str(args, "error", &s->error);
		s->status = (int)get_int(args, "status", 200);
		s->hold = get_bool(args, "hold");
		ak_vec_push(&u->served, s);
		answer_str(f, root, call, true, "{}");
	} else if (span_is(method, "log")) {
		ak_buf_printf(&out, "{\"downloads\":[%.*s],\"installed\":[%.*s],\"canceled\":%u}", (int)u->log_downloads.len, (const char *)u->log_downloads.data,
		              (int)u->log_installed.len, (const char *)u->log_installed.data, u->canceled);
		answer_buf(f, root, call, true, &out);
	} else if (!u->on) {
		answer_str(f, root, call, false, "no module \"update\"");
	} else if (span_is(method, "info")) {
		answer_buf(f, root, call, true, &u->info);
	} else if (span_is(method, "download")) {
		download *d = ak_alloc(sizeof *d);
		memset(d, 0, sizeof *d);
		d->id = ++u->next_id;
		ak_buf url = {0};
		get_str(args, "url", &url);
		get_str(args, "to", &d->to);
		if (u->log_downloads.len) ak_buf_put(&u->log_downloads, ",", 1);
		put_json_str(&u->log_downloads, (const char *)url.data, url.len);
		served *s = served_at(u, &url);
		if (!s) {
			d->status = 404;
		} else {
			d->status = s->status;
			d->hold = s->hold;
			ak_buf_put(&d->error, s->error.data, s->error.len);
			if (d->status == 200 && !d->error.len && s->file.len && !read_file(&s->file, &d->data)) ak_buf_printf(&d->error, "the test's file %.*s is missing", (int)s->file.len, (const char *)s->file.data);
		}
		ak_buf_free(&url);
		ak_vec_push(&u->downloads, d);
		ak_buf_printf(&out, "{\"id\":%u}", d->id);
		answer_buf(f, root, call, true, &out);
	} else if (span_is(method, "next")) {
		download *d = download_of(u, (uint32_t)get_int(args, "id", 0));
		if (!d) {
			answer_str(f, root, call, false, "no such download");
		} else if (d->hold && !d->canceled) {
			d->waiting = true, d->call = call, d->root = root;
		} else {
			step(d);
			progress(d, &out);
			answer_buf(f, root, call, true, &out);
		}
	} else if (span_is(method, "cancel")) {
		download *d = download_of(u, (uint32_t)get_int(args, "id", 0));
		if (d && !d->canceled && !d->finished) {
			d->canceled = d->finished = true;
			u->canceled++;
			ak_buf_printf(&d->error, "canceled");
			if (d->waiting) {
				d->waiting = false;
				progress(d, &out);
				answer_buf(f, d->root, d->call, true, &out);
				out.len = 0;
			}
		}
		answer_str(f, root, call, true, "{}");
	} else if (span_is(method, "install")) {
		ak_buf staged = {0};
		get_str(args, "staged", &staged);
		if (u->install_error.len) {
			answer_buf(f, root, call, false, &u->install_error);
		} else {
			if (u->log_installed.len) ak_buf_put(&u->log_installed, ",", 1);
			put_json_str(&u->log_installed, (const char *)staged.data, staged.len);
			answer_str(f, root, call, true, "{}");
		}
		ak_buf_free(&staged);
	} else {
		ak_buf_printf(&out, "no module method update.%.*s", (int)method.len, method.p);
		answer_buf(f, root, call, false, &out);
	}
	ak_buf_free(&out);
	return true;
}
