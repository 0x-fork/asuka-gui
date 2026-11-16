// recent.c: asuka/recent on Linux: the user's recently used files
// (recently-used.xbel in the XDG data directory, what GtkRecentManager
// keeps and the file chooser's and the file manager's Recent show), the
// app's under its name (g_set_application_name: GTK's own file chooser
// records what the user picks in it under the same). Written with GLib's
// GBookmarkFile, at once, so a list right after an add has it; the
// GtkRecentManagers of the user's apps (this one's too) watch the file.
// Clear takes the app off each file it recorded: one only it recorded
// goes, the user's other recent files stay.
//
// Also the desktop entries' quoting the host shares (login.c).
#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// lx_recent_file is the user's recently used files' XBEL file (tests set
// it).
char *lx_recent_file;

static char *recent_file(void) { return lx_recent_file ? str_dup(ak_cstr(lx_recent_file)) : dsk_join(g_get_user_data_dir(), "recently-used.xbel"); }

#define RECENT_MAX 10  // how many documents list returns at most

// lx_desktop_exec appends a command line as a desktop entry's Exec (and a
// recent file's exec) has it: each argument with a reserved character
// quoted ("...", with ", `, $ and \ escaped; % doubled). A desktop file's
// value escapes its \ again (lx_desktop_value).
void lx_desktop_exec(ak_buf *b, const char *const *args, size_t n) {
	for (size_t i = 0; i < n; i++) {
		const char *a = args[i];
		if (i) ak_buf_u8(b, ' ');
		if (*a && !strpbrk(a, " \t\n\"'\\><~|&;$*?#()`=%")) {
			ak_buf_put(b, a, strlen(a));
			continue;
		}
		ak_buf_u8(b, '"');
		for (; *a; a++) {
			if (*a == '"' || *a == '`' || *a == '$' || *a == '\\') ak_buf_u8(b, '\\');
			if (*a == '%') ak_buf_u8(b, '%');  // %% is a literal %
			ak_buf_u8(b, (uint8_t)*a);
		}
		ak_buf_u8(b, '"');
	}
}

// lx_desktop_value appends s as a desktop file's value.
void lx_desktop_value(ak_buf *b, const char *s) {
	for (; *s; s++) {
		switch (*s) {
		case '\\': ak_buf_put(b, "\\\\", 2); break;
		case '\n': ak_buf_put(b, "\\n", 2); break;
		case '\r': ak_buf_put(b, "\\r", 2); break;
		case '\t': ak_buf_put(b, "\\t", 2); break;
		default: ak_buf_u8(b, (uint8_t)*s);
		}
	}
}

// load reads the bookmarks (none yet: empty); NULL with the error.
static GBookmarkFile *load(const char *path, char **err) {
	GBookmarkFile *b = g_bookmark_file_new();
	GError *e = NULL;
	if (!g_bookmark_file_load_from_file(b, path, &e)) {
		bool missing = e && e->domain == g_file_error_quark() && e->code == 4;  // G_FILE_ERROR_NOENT
		if (!missing) {
			*err = g_strdup(e ? e->message : "can't read the recent files");
			if (e) g_error_free(e);
			g_bookmark_file_free(b);
			return NULL;
		}
		g_error_free(e);
	}
	return b;
}

static bool save(GBookmarkFile *b, const char *path, char **err) {
	char *dir = dsk_dir(path);
	dsk_mkdirs(dir);
	free(dir);
	GError *e = NULL;
	if (g_bookmark_file_to_file(b, path, &e)) return true;
	*err = g_strdup(e ? e->message : "can't write the recent files");
	if (e) g_error_free(e);
	return false;
}

static void answer(reply *r, char *err) {
	if (err)
		reply_err(r, "%s", err);
	else
		reply_ok(r, NULL, 0);
	g_free(err);
}

// add: an absolute path.
static void recent_add(ak_span args, reply *r) {
	char *path = str_dup(args);
	if (path[0] != '/') {
		free(path);
		reply_err(r, "recent: an absolute path, please");
		return;
	}
	char *file = recent_file(), *err = NULL;
	GBookmarkFile *b = load(file, &err);
	if (b) {
		gchar *uri = g_filename_to_uri(path, NULL, NULL);
		char *mime = lx_file_mime(path);
		char *exe = dsk_exe_path();
		ak_buf exec = {0};
		const char *argv[] = {S(exe)};
		lx_desktop_exec(&exec, argv, 1);
		ak_buf_put(&exec, " %u", 3);
		g_bookmark_file_set_mime_type(b, uri, mime);
		g_bookmark_file_add_application(b, uri, S(H.app_name), lx_buf_cstr(&exec));
		save(b, file, &err);
		ak_buf_free(&exec);
		free(exe);
		free(mime);
		g_free(uri);
		g_bookmark_file_free(b);
	}
	free(file);
	free(path);
	answer(r, err);
}

typedef struct {
	char *path;
	int64_t stamp;
} recent_item;

static int newest(const void *a, const void *b) {
	const recent_item *x = a, *y = b;
	return x->stamp > y->stamp ? -1 : x->stamp < y->stamp;
}

// list: the app's recent files that still exist, newest first (JSON).
static void recent_list(ak_span args, reply *r) {
	(void)args;
	char *file = recent_file(), *err = NULL;
	GBookmarkFile *b = load(file, &err);
	free(file);
	if (!b) {
		answer(r, err);
		return;
	}
	gsize n = 0;
	gchar **uris = g_bookmark_file_get_uris(b, &n);
	AK_VEC(recent_item) items = {0};
	for (gsize i = 0; i < n; i++) {
		GDateTime *stamp = NULL;
		if (!g_bookmark_file_get_application_info(b, uris[i], S(H.app_name), NULL, NULL, &stamp, NULL)) continue;
		gchar *p = g_filename_from_uri(uris[i], NULL, NULL);
		struct stat st;
		if (p && stat(p, &st) == 0) {
			recent_item it = {str_dup(ak_cstr(p)), stamp ? g_date_time_to_unix(stamp) * 1000000 + g_date_time_get_microsecond(stamp) : 0};
			ak_vec_push(&items, it);
		}
		g_free(p);
		if (stamp) g_date_time_unref(stamp);
	}
	g_strfreev(uris);
	g_bookmark_file_free(b);
	qsort(items.data, items.len, sizeof *items.data, newest);
	dsk_jw w = {0};
	dsk_jw_arr(&w);
	for (uint32_t i = 0; i < items.len; i++) {
		if (i < RECENT_MAX) dsk_jw_str(&w, items.data[i].path);
		free(items.data[i].path);
	}
	dsk_jw_aend(&w);
	ak_vec_free(&items);
	size_t len;
	char *json = dsk_jw_take(&w, &len);
	reply_ok(r, json, len);
	free(json);
}

// clear: the app off each file it recorded.
static void recent_clear(ak_span args, reply *r) {
	(void)args;
	char *file = recent_file(), *err = NULL;
	GBookmarkFile *b = load(file, &err);
	if (b) {
		gsize n = 0;
		gchar **uris = g_bookmark_file_get_uris(b, &n);
		bool changed = false;
		for (gsize i = 0; i < n; i++) {
			gsize na = 0;
			gchar **apps = g_bookmark_file_get_applications(b, uris[i], &na, NULL);
			bool mine = false;
			for (gsize k = 0; k < na; k++) mine |= !strcmp(apps[k], S(H.app_name));
			if (mine) {
				changed = true;
				if (na == 1)
					g_bookmark_file_remove_item(b, uris[i], NULL);
				else
					g_bookmark_file_remove_application(b, uris[i], S(H.app_name), NULL);
			}
			g_strfreev(apps);
		}
		g_strfreev(uris);
		if (changed) save(b, file, &err);
		g_bookmark_file_free(b);
	}
	free(file);
	answer(r, err);
}

static const method_t recent_methods[] = {
	{"recent", "add", recent_add},
	{"recent", "list", recent_list},
	{"recent", "clear", recent_clear},
};

static void recent_modules(void) { lx_register_methods(recent_methods, AK_LEN(recent_methods)); }

const area lx_area_recent = {
	.name = "recent",
	.modules = recent_modules,
};
