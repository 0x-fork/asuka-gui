// asuka/dialog on Linux: GtkFileDialog (GTK's own dialog, or the
// desktop's file chooser portal in a sandbox), modal to the window the
// user works in, shown on the main thread. GTK's dialog has its own
// toggle for hidden files (Ctrl+H).
#include "asuka_linux.h"
#include "dialog.asuka.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
	gpointer d;
	asuka_call *call;
	int kind;  // 0 open, 1 save, 2 folder
} showing;

static void finish(showing *s, char **paths, size_t n, GError *e) {
	G(g_object_unref)(s->d);
	if (e && !asuka_linux_cancelled(e)) {
		asuka_error err = {0};
		asuka_linux_fail(&err, e, "the file dialog");
		asuka_call_fail(s->call, &err);
		for (size_t i = 0; i < n; i++) free(paths[i]);
		free(paths);
	} else {
		if (e) G(g_error_free)(e);  // cancelled: nothing chosen
		if (s->kind == 1) {
			dialog_Chooser_save_done(s->call, n ? paths[0] : NULL);
			for (size_t i = 1; i < n; i++) free(paths[i]);
			free(paths);
		} else {
			dialog_list_string l = {paths ? paths : calloc(1, sizeof(char *)), n};
			if (s->kind == 0) dialog_Chooser_open_done(s->call, l);
			else dialog_Chooser_folder_done(s->call, l);
		}
	}
	free(s);
}

static void add(char ***paths, size_t *n, GFile *f) {
	gchar *p = f ? G(g_file_get_path)(f) : NULL;
	if (p && *p) {
		*paths = realloc(*paths, (*n + 1) * sizeof **paths);
		(*paths)[(*n)++] = asuka_strdup(p);
	}
	G(g_free)(p);
}

typedef GFile *(*one_fin)(gpointer, GAsyncResult *, GError **);
typedef GListModel *(*many_fin)(gpointer, GAsyncResult *, GError **);

static void one(GObject *src, GAsyncResult *res, gpointer p) {
	showing *s = p;
	GError *e = NULL;
	one_fin fin = s->kind == 1 ? G(gtk_file_dialog_save_finish) : s->kind == 2 ? G(gtk_file_dialog_select_folder_finish) : G(gtk_file_dialog_open_finish);
	GFile *f = fin(src, res, &e);
	char **paths = NULL;
	size_t n = 0;
	add(&paths, &n, f);
	if (f) G(g_object_unref)(f);
	finish(s, paths, n, e);
}

static void many(GObject *src, GAsyncResult *res, gpointer p) {
	showing *s = p;
	GError *e = NULL;
	many_fin fin = s->kind == 2 ? G(gtk_file_dialog_select_multiple_folders_finish) : G(gtk_file_dialog_open_multiple_finish);
	GListModel *list = fin(src, res, &e);
	char **paths = NULL;
	size_t n = 0;
	if (list) {
		guint k = G(g_list_model_get_n_items)(list);
		for (guint i = 0; i < k; i++) {
			GFile *f = G(g_list_model_get_item)(list, i);
			add(&paths, &n, f);
			if (f) G(g_object_unref)(f);
		}
		G(g_object_unref)(list);
	}
	finish(s, paths, n, e);
}

static void set_folder(gpointer d, const char *dir) {
	GFile *f = G(g_file_new_for_path)(dir);
	G(gtk_file_dialog_set_initial_folder)(d, f);
	G(g_object_unref)(f);
}

static void show(int kind, const dialog_Request *r, asuka_call *call) {
	if (!asuka_linux() || !G(gtk_file_dialog_new)) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "no GtkFileDialog (GTK 4.10)");
		asuka_call_fail(call, &e);
		return;
	}
	showing *s = calloc(1, sizeof *s);
	s->call = call;
	s->kind = kind;
	gpointer d = s->d = G(gtk_file_dialog_new)();
	G(gtk_file_dialog_set_modal)(d, true);
	if (*r->title) G(gtk_file_dialog_set_title)(d, r->title);
	if (*r->button) G(gtk_file_dialog_set_accept_label)(d, r->button);
	if (*r->start) {
		struct stat st;
		if (stat(r->start, &st) == 0 && S_ISDIR(st.st_mode)) {
			set_folder(d, r->start);
		} else {
			// A file: its folder. GTK's open dialog (and the portal's)
			// selects nothing in it; save suggests its name.
			const char *slash = strrchr(r->start, '/');
			char *dir = slash ? asuka_strndup(r->start, (size_t)(slash - r->start)) : asuka_strdup(".");
			set_folder(d, *dir ? dir : "/");
			free(dir);
			if (kind == 1 && !*r->name) G(gtk_file_dialog_set_initial_name)(d, slash ? slash + 1 : r->start);
		}
	}
	if (kind == 1 && *r->name) G(gtk_file_dialog_set_initial_name)(d, r->name);
	if (kind != 2 && r->filters.len > 0) {
		gpointer store = G(g_list_store_new)(G(gtk_file_filter_get_type)());
		for (size_t i = 0; i < r->filters.len; i++) {
			const dialog_Filter *f = &r->filters.data[i];
			gpointer ff = G(gtk_file_filter_new)();
			G(gtk_file_filter_set_name)(ff, f->name);
			for (size_t k = 0; k < f->types.len; k++) {
				const char *t = f->types.data[k];
				if (strcmp(t, "*/*") == 0 || strcmp(t, "*") == 0) G(gtk_file_filter_add_pattern)(ff, "*");
				else G(gtk_file_filter_add_mime_type)(ff, t);  // "image/*" too
			}
			for (size_t k = 0; k < f->extensions.len; k++) {
				const char *x = f->extensions.data[k];
				if (strcmp(x, "*") == 0) G(gtk_file_filter_add_pattern)(ff, "*");
				else G(gtk_file_filter_add_suffix)(ff, x[0] == '.' ? x + 1 : x);
			}
			G(g_list_store_append)(store, ff);
			if (i == 0) G(gtk_file_dialog_set_default_filter)(d, ff);
			G(g_object_unref)(ff);
		}
		G(gtk_file_dialog_set_filters)(d, store);
		G(g_object_unref)(store);
	}
	GtkWindow *parent = asuka_desktop_window(0);
	if (kind == 1) G(gtk_file_dialog_save)(d, parent, NULL, one, s);
	else if (kind == 2 && r->multiple) G(gtk_file_dialog_select_multiple_folders)(d, parent, NULL, many, s);
	else if (kind == 2) G(gtk_file_dialog_select_folder)(d, parent, NULL, one, s);
	else if (r->multiple) G(gtk_file_dialog_open_multiple)(d, parent, NULL, many, s);
	else G(gtk_file_dialog_open)(d, parent, NULL, one, s);
}

static void c_open(dialog_Chooser *self, const dialog_Request *r, asuka_call *call) { show(0, r, call); }
static void c_save(dialog_Chooser *self, const dialog_Request *r, asuka_call *call) { show(1, r, call); }
static void c_folder(dialog_Chooser *self, const dialog_Request *r, asuka_call *call) { show(2, r, call); }

static const dialog_Chooser_vtable vt = {.open = c_open, .save = c_save, .folder = c_folder};

dialog_Chooser *dialog_make_Chooser(asuka_error *err) {
	dialog_Chooser *c = malloc(sizeof *c);
	c->vt = &vt;
	return c;
}
