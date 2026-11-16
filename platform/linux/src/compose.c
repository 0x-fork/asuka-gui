// compose.c: asuka/compose on Linux. Mail: the email portal (the user's
// mail app's composer, with the attachments), else xdg-email, else the
// mailto: link (no attachments). Answered 3, unknown: the mail app doesn't
// say. No text messages.
//
// Also lx_local_files: the files sources name (an http(s) URL downloaded
// into the caches, a file:// URL, a path, an asset), off the main thread.
#include "host.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- Local files ----

typedef struct {
	char **sources, **types;
	size_t n;
	char **paths;
	char *err;
	void (*done)(void *ctx, char **paths, size_t n, const char *err);
	void *ctx;
} local_job;

// extension_of is a MIME type's first extension in the system's
// mime.types (as Go's mime package reads it), NULL if none.
static char *extension_of(const char *mime) {
	static const char *const files[] = {"/etc/mime.types", "/etc/apache2/mime.types", "/etc/apache/mime.types", "/etc/httpd/conf/mime.types"};
	for (size_t f = 0; f < AK_LEN(files); f++) {
		size_t n;
		char *text = dsk_read_file(files[f], &n);
		if (!text) continue;
		char *out = NULL;
		for (char *line = strtok(text, "\n"); line && !out; line = strtok(NULL, "\n")) {
			if (*line == '#') continue;
			char *sp = line + strcspn(line, " \t");
			if (!*sp || (size_t)(sp - line) != strlen(mime) || g_ascii_strncasecmp(line, mime, strlen(mime))) continue;
			sp += strspn(sp, " \t");
			size_t k = strcspn(sp, " \t\r");
			if (k) out = str_dup(ak_strn(sp, k));
		}
		free(text);
		if (out) return out;
	}
	return NULL;
}

// download fetches source into a folder of its own in the caches, named
// as the server says (else the URL's last part), with its type's
// extension if it has none.
static char *download(const char *source, const char *type, char **err) {
	if (!soup_session_new) {
		*err = g_strdup("libsoup 3 isn't installed to download it");
		return NULL;
	}
	SoupSession *s = soup_session_new();
	SoupMessage *m = soup_message_new("GET", source);
	GError *e = NULL;
	GBytes *b = m ? soup_session_send_and_read(s, m, NULL, &e) : NULL;
	char *out = NULL;
	if (!b || soup_message_get_status(m) / 100 != 2) {
		*err = g_strdup_printf("couldn't download %s: %s", source, e ? e->message : m ? S(soup_message_get_reason_phrase(m)) : "bad URL");
		goto done;
	}
	char *name = NULL;
	SoupMessageHeaders *hdrs = soup_message_get_response_headers(m);
	const char *cd = hdrs ? soup_message_headers_get_one(hdrs, "Content-Disposition") : NULL;
	const char *fn = cd ? strstr(cd, "filename=") : NULL;
	if (fn) {
		fn += 9;
		bool q = *fn == '"';
		if (q) fn++;
		size_t k = strcspn(fn, q ? "\"" : ";");
		name = g_strndup(fn, k);
	}
	if (!name) {
		GUri *u = g_uri_parse(source, 0, NULL);
		const char *p = u ? g_uri_get_path(u) : "";
		const char *slash = strrchr(p, '/');
		name = g_strdup(slash ? slash + 1 : p);
		if (u) g_uri_unref(u);
	}
	for (char *c = name; *c; c++)
		if (*c == '/' || *c == '\\' || *c == ':') *c = '_';
	g_strstrip(name);
	if (!*name || !strcmp(name, ".") || !strcmp(name, "_")) {
		g_free(name);
		name = g_strdup("file");
	}
	if (!strrchr(name, '.')) {
		const char *t = type && *type ? type : hdrs ? soup_message_headers_get_one(hdrs, "Content-Type") : NULL;
		if (t) {
			char *mime = g_strndup(t, strcspn(t, ";"));
			g_strstrip(mime);
			char *ext = extension_of(mime);
			if (ext) {
				char *t2 = g_strconcat(name, ".", ext, NULL);
				g_free(name);
				name = t2;
				free(ext);
			}
			g_free(mime);
		}
	}
	char dirname[64];
	snprintf(dirname, sizeof dirname, "asuka-files/%08x%08x", g_random_int(), g_random_int());
	char *dir = dsk_join(lx_cache_dir(), dirname), *p = dsk_join(dir, name);
	gsize n;
	const void *data = g_bytes_get_data(b, &n);
	if (dsk_mkdirs(dir) && dsk_write_file(p, data, n))
		out = p;
	else {
		*err = g_strdup_printf("can't write %s", p);
		free(p);
	}
	free(dir);
	g_free(name);
done:
	if (b) g_bytes_unref(b);
	if (e) g_error_free(e);
	if (m) g_object_unref(m);
	g_object_unref(s);
	return out;
}

// resolve finds the file a source names (any thread).
static char *resolve(const char *source, const char *type, char **err) {
	if (g_str_has_prefix(source, "http://") || g_str_has_prefix(source, "https://")) return download(source, type, err);
	char *p;
	if (g_str_has_prefix(source, "file://")) {
		p = g_filename_from_uri(source, NULL, NULL);
		if (!p) {
			*err = g_strdup_printf("no file at %s", source);
			return NULL;
		}
		char *t = str_dup(ak_cstr(p));
		g_free(p);
		p = t;
	} else if (source[0] == '/') {
		p = str_dup(ak_cstr(source));
	} else {
		p = dsk_join(S(H.assets), source);
	}
	struct stat st;
	if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) {
		*err = g_strdup_printf("no file at %s", source);
		free(p);
		return NULL;
	}
	return p;
}

static gboolean local_done(gpointer data) {
	local_job *j = data;
	j->done(j->ctx, j->paths, j->n, j->err);
	for (size_t i = 0; i < j->n; i++) {
		free(j->sources[i]);
		free(j->types[i]);
		free(j->paths[i]);
	}
	free(j->sources);
	free(j->types);
	free(j->paths);
	g_free(j->err);
	free(j);
	return 0;
}

static gpointer local_thread(gpointer data) {
	local_job *j = data;
	for (size_t i = 0; i < j->n && !j->err; i++) j->paths[i] = resolve(j->sources[i], j->types[i], &j->err);
	g_idle_add_full(G_PRIORITY_DEFAULT, local_done, j, NULL);
	return NULL;
}

// lx_local_files finds the files sources name (with their types, for a
// download), off the main thread, and calls done with them on it.
void lx_local_files(const char *const *sources, const char *const *types, size_t n, void (*done)(void *ctx, char **paths, size_t n, const char *err), void *ctx) {
	lx_lib_load(AK_LIB_SOUP);  // on the main thread: dlopen once
	local_job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->n = n;
	j->sources = calloc(n ? n : 1, sizeof *j->sources);
	j->types = calloc(n ? n : 1, sizeof *j->types);
	j->paths = calloc(n ? n : 1, sizeof *j->paths);
	if (!j->sources || !j->types || !j->paths) abort();
	for (size_t i = 0; i < n; i++) {
		j->sources[i] = str_dup(ak_cstr(sources[i]));
		j->types[i] = str_dup(ak_cstr(types ? S(types[i]) : ""));
	}
	j->done = done;
	j->ctx = ctx;
	g_thread_unref(g_thread_new("asuka-files", local_thread, j));
}

// ---- Mail ----

typedef struct {
	dsk_json *args;
	reply *r;
	char **paths;
	size_t n;
	int *fds;
} mail_t;

static void mail_free(mail_t *m) {
	for (size_t i = 0; i < m->n; i++) {
		free(m->paths[i]);
		if (m->fds && m->fds[i] >= 0) close(m->fds[i]);
	}
	free(m->paths);
	free(m->fds);
	dsk_json_free(m->args);
	free(m);
}

static void strs_of(const dsk_json *a, const char *key, GVariantBuilder *b, bool *any) {
	const dsk_json *l = NULL;
	for (size_t i = 0; a && a->type == DSK_JOBJ && i < a->n; i++)
		if (!g_ascii_strcasecmp(a->keys[i], key)) l = &a->kids[i];
	g_variant_builder_init(b, G_VARIANT_TYPE("as"));
	*any = false;
	for (size_t i = 0; l && l->type == DSK_JARR && i < l->n; i++)
		if (l->kids[i].type == DSK_JSTR) {
			g_variant_builder_add(b, "s", l->kids[i].str);
			*any = true;
		}
}

static const char *jstr(const dsk_json *a, const char *key) {
	for (size_t i = 0; a && a->type == DSK_JOBJ && i < a->n; i++)
		if (!g_ascii_strcasecmp(a->keys[i], key) && a->kids[i].type == DSK_JSTR) return a->kids[i].str;
	return "";
}

static const dsk_json *jget(const dsk_json *a, const char *key) {
	for (size_t i = 0; a && a->type == DSK_JOBJ && i < a->n; i++)
		if (!g_ascii_strcasecmp(a->keys[i], key)) return &a->kids[i];
	return NULL;
}

static void mail_ran(void *ctx, bool ok) {
	mail_t *m = ctx;
	if (ok)
		reply_ok(m->r, "\3", 1);
	else
		reply_err(m->r, "unavailable");
	mail_free(m);
}

static void xdg_email_done(GObject *src, GAsyncResult *res, gpointer data) {
	GError *e = NULL;
	bool ok = g_subprocess_wait_check_finish((GSubprocess *)src, res, &e);
	if (e) g_error_free(e);
	g_object_unref(src);
	mail_ran(data, ok);
}

// mail_fallback: xdg-email (xdg-utils) with the attachments, else the
// mailto: link.
static void mail_fallback(mail_t *m) {
	gchar *exe = g_find_program_in_path("xdg-email");
	if (exe) {
		GPtrArray *argv = g_ptr_array_new();
		g_ptr_array_add(argv, exe);
		g_ptr_array_add(argv, "--utf8");
		static const char *const lists[][2] = {{"cc", "--cc"}, {"bcc", "--bcc"}};
		for (int k = 0; k < 2; k++) {
			const dsk_json *l = jget(m->args, lists[k][0]);
			for (size_t i = 0; l && l->type == DSK_JARR && i < l->n; i++) {
				g_ptr_array_add(argv, (gpointer)lists[k][1]);
				g_ptr_array_add(argv, l->kids[i].str);
			}
		}
		if (*jstr(m->args, "subject")) {
			g_ptr_array_add(argv, "--subject");
			g_ptr_array_add(argv, (gpointer)jstr(m->args, "subject"));
		}
		if (*jstr(m->args, "text")) {
			g_ptr_array_add(argv, "--body");
			g_ptr_array_add(argv, (gpointer)jstr(m->args, "text"));
		}
		for (size_t i = 0; i < m->n; i++) {
			g_ptr_array_add(argv, "--attach");
			g_ptr_array_add(argv, m->paths[i]);
		}
		const dsk_json *to = jget(m->args, "to");
		for (size_t i = 0; to && to->type == DSK_JARR && i < to->n; i++) g_ptr_array_add(argv, to->kids[i].str);
		g_ptr_array_add(argv, NULL);
		GSubprocess *p = g_subprocess_newv((const gchar *const *)argv->pdata, G_SUBPROCESS_FLAGS_NONE, NULL);
		g_ptr_array_free(argv, 1);
		g_free(exe);
		if (p) {
			g_subprocess_wait_check_async(p, NULL, xdg_email_done, m);
			return;
		}
	}
	const char *mailto = jstr(m->args, "mailto");
	GAppInfo *app = g_app_info_get_default_for_uri_scheme("mailto");
	if (m->n || !*mailto || !app) {
		if (app) g_object_unref(app);
		mail_ran(m, false);
		return;
	}
	g_object_unref(app);
	lx_open_url(mailto);
	mail_ran(m, true);
}

static GVariant *mail_params(const char *token, void *ctx) {
	mail_t *m = ctx;
	GVariantBuilder o, l;
	g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string(token));
	bool any;
	strs_of(m->args, "to", &l, &any);
	const dsk_json *to = jget(m->args, "to");
	if (any) {
		g_variant_builder_add(&o, "{sv}", "addresses", g_variant_builder_end(&l));
		g_variant_builder_add(&o, "{sv}", "address", g_variant_new_string(to->kids[0].str));
	} else {
		g_variant_unref(g_variant_ref_sink(g_variant_builder_end(&l)));
	}
	strs_of(m->args, "cc", &l, &any);
	if (any)
		g_variant_builder_add(&o, "{sv}", "cc", g_variant_builder_end(&l));
	else
		g_variant_unref(g_variant_ref_sink(g_variant_builder_end(&l)));
	strs_of(m->args, "bcc", &l, &any);
	if (any)
		g_variant_builder_add(&o, "{sv}", "bcc", g_variant_builder_end(&l));
	else
		g_variant_unref(g_variant_ref_sink(g_variant_builder_end(&l)));
	if (*jstr(m->args, "subject")) g_variant_builder_add(&o, "{sv}", "subject", g_variant_new_string(jstr(m->args, "subject")));
	if (*jstr(m->args, "text")) g_variant_builder_add(&o, "{sv}", "body", g_variant_new_string(jstr(m->args, "text")));
	if (m->n) {
		GVariantBuilder h;
		g_variant_builder_init(&h, G_VARIANT_TYPE("ah"));
		for (size_t i = 0; i < m->n; i++) g_variant_builder_add(&h, "h", (gint32)i);
		g_variant_builder_add(&o, "{sv}", "attachment_fds", g_variant_builder_end(&h));
	}
	return g_variant_new("(sa{sv})", "", &o);
}

static void mail_answered(void *ctx, uint32_t code, GVariant *results, const char *err) {
	(void)results;
	mail_t *m = ctx;
	if (lx_no_portal(err) || (!err && code > 1)) {
		mail_fallback(m);
		return;
	}
	if (err)
		reply_err(m->r, "%s", err);
	else
		reply_ok(m->r, code == 1 ? "\0" : "\3", 1);  // 1: the user cancelled the chooser
	mail_free(m);
}

// named_copy gives an attachment its name (a copy named so, when the
// file's isn't).
static char *named_copy(char *path, const char *name) {
	char *n = g_strstrip(g_strdup(name));
	if (!*n || !strcmp(dsk_base(n), dsk_base(path))) {
		g_free(n);
		return path;
	}
	char dirname[64];
	snprintf(dirname, sizeof dirname, "asuka-mail/%08x%08x", g_random_int(), g_random_int());
	char *dir = dsk_join(lx_cache_dir(), dirname), *to = dsk_join(dir, dsk_base(n));
	g_free(n);
	size_t len;
	char *data = dsk_read_file(path, &len);
	bool ok = data && dsk_mkdirs(dir) && dsk_write_file(to, data, len);
	free(data);
	free(dir);
	if (!ok) {
		free(to);
		return path;
	}
	free(path);
	return to;
}

static void attachments_found(void *ctx, char **paths, size_t n, const char *err) {
	mail_t *m = ctx;
	if (err) {
		reply_err(m->r, "%s", err);
		mail_free(m);
		return;
	}
	const dsk_json *att = jget(m->args, "attachments");
	m->n = n;
	m->paths = calloc(n ? n : 1, sizeof *m->paths);
	m->fds = calloc(n ? n : 1, sizeof *m->fds);
	if (!m->paths || !m->fds) abort();
	for (size_t i = 0; i < n; i++) {
		m->paths[i] = named_copy(str_dup(ak_cstr(paths[i])), jstr(&att->kids[i], "name"));
		m->fds[i] = -1;
	}
	GUnixFDList *fds = g_unix_fd_list_new();
	for (size_t i = 0; i < n; i++) {
		m->fds[i] = open(m->paths[i], O_RDONLY | O_CLOEXEC);
		if (m->fds[i] < 0 || g_unix_fd_list_append(fds, m->fds[i], NULL) < 0) {
			g_object_unref(fds);
			reply_err(m->r, "open %s: %s", m->paths[i], g_strerror(errno));
			mail_free(m);
			return;
		}
	}
	lx_portal_request_fds("org.freedesktop.portal.Email", "ComposeEmail", mail_params, fds, mail_answered, m);
	g_object_unref(fds);
}

// mail: {"to", "cc", "bcc", "subject", "text", "mailto", "attachments":
// [{"source", "type", "name"}]}: the mail to the user's mail app; answered
// 3, unknown, or 0 if the user cancelled the portal's chooser.
static void c_mail(ak_span args, reply *r) {
	dsk_json *a = dsk_json_parse(args.p, args.len);
	if (!a || a->type != DSK_JOBJ) {
		dsk_json_free(a);
		reply_err(r, "compose.mail: bad arguments");
		return;
	}
	mail_t *m = calloc(1, sizeof *m);
	if (!m) abort();
	m->args = a;
	m->r = r;
	const dsk_json *att = jget(a, "attachments");
	size_t n = att && att->type == DSK_JARR ? att->n : 0;
	const char **srcs = calloc(n ? n : 1, sizeof *srcs), **types = calloc(n ? n : 1, sizeof *types);
	if (!srcs || !types) abort();
	for (size_t i = 0; i < n; i++) {
		srcs[i] = jstr(&att->kids[i], "source");
		types[i] = jstr(&att->kids[i], "type");
	}
	lx_local_files(srcs, types, n, attachments_found, m);
	free(srcs);
	free(types);
}

// can: u8 mail, u8 message, two more (Linux: none).
static void c_can(ak_span args, reply *r) {
	(void)args;
	uint8_t b[4] = {0};
	GAppInfo *app = g_app_info_get_default_for_uri_scheme("mailto");
	if (app) {
		g_object_unref(app);
		b[0] = 1;
	} else if (lx_in_flatpak()) {
		b[0] = 1;  // the portal asks the host's
	}
	reply_ok(r, b, 4);
}

static void c_message(ak_span args, reply *r) {
	(void)args;
	reply_err(r, "unavailable");
}

static const method_t compose_methods[] = {
	{"compose", "can", c_can},
	{"compose", "mail", c_mail},
	{"compose", "message", c_message},
};

static void compose_modules(void) { lx_register_methods(compose_methods, AK_LEN(compose_methods)); }

const area lx_area_compose = {
	.name = "compose",
	.modules = compose_modules,
};
