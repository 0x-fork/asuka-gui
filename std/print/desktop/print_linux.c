// asuka/print on Linux: a file goes to the print portal (its dialog, then
// the printer: what CUPS takes, PDFs and images among them), or, without
// the portal, to GTK's print dialog and a print job; markup prints with
// WebKit (its print operation, GTK's dialog), and a PDF is the markup
// laid out by WebKit and printed to a file (GTK's "Print to File").
#include "asuka_linux.h"
#include "print.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
	bool tried;
	const char *missing;
	// WebKitGTK 6's.
	GtkWidget *(*view_new)(void);
	void (*load_html)(GtkWidget *, const char *, const char *);
	const char *(*get_title)(GtkWidget *);
	void (*evaluate)(GtkWidget *, const char *, gssize, const char *, const char *, GCancellable *, GAsyncReadyCallback, gpointer);
	gpointer (*evaluate_finish)(GtkWidget *, GAsyncResult *, GError **);
	char *(*to_string)(gpointer);
	gpointer (*op_new)(GtkWidget *);
	int (*op_run_dialog)(gpointer, GtkWindow *);
	void (*op_print)(gpointer);
	void (*op_set_settings)(gpointer, gpointer);
	void (*op_set_setup)(gpointer, gpointer);
	// GTK 4's.
	gpointer (*settings_new)(void);
	void (*settings_set)(gpointer, const char *, const char *);
	void (*settings_set_printer)(gpointer, const char *);
	gpointer (*setup_new)(void);
	void (*set_orientation)(gpointer, int);
	void (*set_paper_size)(gpointer, gpointer);
	void (*set_top)(gpointer, double, int), (*set_bottom)(gpointer, double, int), (*set_left)(gpointer, double, int), (*set_right)(gpointer, double, int);
	gpointer (*paper_new_custom)(const char *, const char *, double, double, int);
	void (*paper_free)(gpointer);
	GtkWidget *(*dialog_new)(const char *, GtkWindow *);
	gpointer (*dialog_printer)(GtkWidget *);
	gpointer (*dialog_settings)(GtkWidget *);
	gpointer (*dialog_setup)(GtkWidget *);
	void (*dialog_set_caps)(GtkWidget *, int);
	gpointer (*job_new)(const char *, gpointer, gpointer, gpointer);
	gboolean (*job_set_source_file)(gpointer, const char *, GError **);
	void (*job_send)(gpointer, void (*)(gpointer, gpointer, const GError *), gpointer, GDestroyNotify);
	void (*window_set_modal)(GtkWidget *, gboolean);
	void (*window_destroy)(GtkWidget *);
} pk;

enum { load_finished = 3, unit_points = 1, response_ok = -5, caps_preview = 1 << 6, orientation_landscape = 1 };

static bool load(asuka_error *err) {
	if (!pk.tried) {
		pk.tried = true;
		void *g = asuka_linux() ? dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_GLOBAL) : NULL;
		void *l = g ? dlopen("libwebkitgtk-6.0.so.4", RTLD_NOW | RTLD_GLOBAL) : NULL;
		void *j = l ? dlopen("libjavascriptcoregtk-6.0.so.1", RTLD_NOW | RTLD_GLOBAL) : NULL;
		if (!g) {
			pk.missing = "GTK 4 isn't installed: printing needs it";
		} else if (!l) {
			pk.missing = "WebKitGTK 6.0 isn't installed: install it to print (Ubuntu, Debian: libwebkitgtk-6.0-4; Fedora: webkitgtk6.0; Arch: webkitgtk-6.0)";
		} else {
#define W(field, name) *(void **)&pk.field = dlsym(l, name)
#define K(field, name) *(void **)&pk.field = dlsym(g, name)
			W(view_new, "webkit_web_view_new");
			W(load_html, "webkit_web_view_load_html");
			W(get_title, "webkit_web_view_get_title");
			W(evaluate, "webkit_web_view_evaluate_javascript");
			W(evaluate_finish, "webkit_web_view_evaluate_javascript_finish");
			if (j) *(void **)&pk.to_string = dlsym(j, "jsc_value_to_string");
			W(op_new, "webkit_print_operation_new");
			W(op_run_dialog, "webkit_print_operation_run_dialog");
			W(op_print, "webkit_print_operation_print");
			W(op_set_settings, "webkit_print_operation_set_print_settings");
			W(op_set_setup, "webkit_print_operation_set_page_setup");
			K(settings_new, "gtk_print_settings_new");
			K(settings_set, "gtk_print_settings_set");
			K(settings_set_printer, "gtk_print_settings_set_printer");
			K(setup_new, "gtk_page_setup_new");
			K(set_orientation, "gtk_page_setup_set_orientation");
			K(set_paper_size, "gtk_page_setup_set_paper_size");
			K(set_top, "gtk_page_setup_set_top_margin");
			K(set_bottom, "gtk_page_setup_set_bottom_margin");
			K(set_left, "gtk_page_setup_set_left_margin");
			K(set_right, "gtk_page_setup_set_right_margin");
			K(paper_new_custom, "gtk_paper_size_new_custom");
			K(paper_free, "gtk_paper_size_free");
			K(dialog_new, "gtk_print_unix_dialog_new");
			K(dialog_printer, "gtk_print_unix_dialog_get_selected_printer");
			K(dialog_settings, "gtk_print_unix_dialog_get_settings");
			K(dialog_setup, "gtk_print_unix_dialog_get_page_setup");
			K(dialog_set_caps, "gtk_print_unix_dialog_set_manual_capabilities");
			K(job_new, "gtk_print_job_new");
			K(job_set_source_file, "gtk_print_job_set_source_file");
			K(job_send, "gtk_print_job_send");
			K(window_set_modal, "gtk_window_set_modal");
			K(window_destroy, "gtk_window_destroy");
#undef W
#undef K
			if (!pk.view_new || !pk.load_html || !pk.op_new || !pk.op_run_dialog || !pk.op_print || !pk.settings_new || !pk.setup_new ||
			    !pk.paper_new_custom || !pk.dialog_new || !pk.job_new || !pk.job_send)
				pk.missing = "WebKitGTK 6.0 or GTK 4 is too old to print";
		}
	}
	if (pk.missing) asuka_error_declared(err, print_PrintError_unsupported);
	return !pk.missing;
}

static GtkWindow *window(void) { return (GtkWindow *)asuka_desktop_window(0); }

// ---- A page that's never shown ----

typedef struct page page;
typedef void (*page_done)(print_task *t, GtkWidget *view, asuka_error *err);

struct page {
	print_task *t;
	GtkWidget *view;
	gulong handlers[2];
	bool ended;
	page_done done;
	asuka_error err;
};

static gboolean page_end(gpointer p) {
	page *pg = p;
	for (int i = 0; i < 2; i++) G(g_signal_handler_disconnect)(pg->view, pg->handlers[i]);
	if (pg->err.status) {
		G(g_object_unref)(pg->view);
		pg->done(pg->t, NULL, &pg->err);
	} else {
		pg->done(pg->t, pg->view, NULL);
	}
	free(pg);
	return false;
}

static void on_load_changed(GtkWidget *v, int ev, gpointer p) {
	page *pg = p;
	if (ev != load_finished || pg->ended) return;
	pg->ended = true;
	// Printing runs a dialog's loop: not inside the signal.
	G(g_idle_add_full)(G_PRIORITY_DEFAULT, page_end, pg, NULL);
}

static gboolean on_load_failed(GtkWidget *v, int ev, const char *uri, GError *e, gpointer p) {
	page *pg = p;
	if (pg->ended) return true;
	pg->ended = true;
	asuka_error_failed(&pg->err, "print", 0, "print: the page couldn't load: %s", e ? e->message : "");
	G(g_idle_add_full)(G_PRIORITY_DEFAULT, page_end, pg, NULL);
	return true;
}

// load_page loads markup in a web view that's never shown; done gets it
// (a reference) once it's loaded.
static void load_page(print_task *t, const char *html, page_done done) {
	page *pg = calloc(1, sizeof *pg);
	pg->t = t;
	pg->done = done;
	pg->view = G(g_object_ref_sink)(pk.view_new());
	pg->handlers[0] = g_signal_connect(pg->view, "load-changed", on_load_changed, pg);
	pg->handlers[1] = g_signal_connect(pg->view, "load-failed", on_load_failed, pg);
	pk.load_html(pg->view, html, NULL);
}

// ---- html ----

typedef struct {
	gpointer op;
	GtkWidget *view;
	gulong finished;
} printing;

static gboolean printed_end(gpointer p) {
	printing *pr = p;
	G(g_signal_handler_disconnect)(pr->op, pr->finished);
	G(g_object_unref)(pr->op);
	G(g_object_unref)(pr->view);
	free(pr);
	return false;
}

static void on_printed(gpointer op, gpointer p) { G(g_idle_add_full)(G_PRIORITY_DEFAULT, printed_end, p, NULL); }

static void html_loaded(print_task *t, GtkWidget *view, asuka_error *err) {
	if (err) {
		print_fail(t, err);
		print_release(t);
		return;
	}
	gpointer op = pk.op_new(view);
	if (t->job.landscape) {
		gpointer setup = pk.setup_new();
		pk.set_orientation(setup, orientation_landscape);
		pk.op_set_setup(op, setup);
		G(g_object_unref)(setup);
	}
	bool printed = pk.op_run_dialog(op, window()) == 0;  // WEBKIT_PRINT_OPERATION_RESPONSE_PRINT
	if (printed) {
		// Printing goes on: the operation (and the page) until it's done.
		printing *pr = calloc(1, sizeof *pr);
		pr->op = op;
		pr->view = view;
		pr->finished = g_signal_connect(op, "finished", on_printed, pr);
	} else {
		G(g_object_unref)(op);
		G(g_object_unref)(view);
	}
	print_outcome(t, printed ? print_Outcome_printed : print_Outcome_cancelled);
	print_release(t);
}

void print_html(print_task *t) {
	asuka_error e = {0};
	if (!load(&e)) {
		print_fail(t, &e);
		print_release(t);
		return;
	}
	load_page(t, t->source, html_loaded);
}

// ---- pdf ----

typedef struct {
	print_task *t;
	gpointer op;
	GtkWidget *view;
	char *path;
	gulong handlers[2];
	bool answered;
	asuka_error err;
} making;

static gboolean pdf_end(gpointer p) {
	making *m = p;
	G(g_object_unref)(m->op);
	G(g_object_unref)(m->view);
	struct stat st;
	if (!m->err.status && (stat(m->path, &st) != 0 || st.st_size == 0)) asuka_error_failed(&m->err, "print", 0, "print: the PDF couldn't be written");
	if (m->err.status) {
		print_fail(m->t, &m->err);
		free(m->path);
	} else {
		print_path(m->t, m->path);
	}
	print_release(m->t);
	free(m);
	return false;
}

static void pdf_answer(making *m) {
	if (m->answered) return;
	m->answered = true;
	for (int i = 0; i < 2; i++) G(g_signal_handler_disconnect)(m->op, m->handlers[i]);
	G(g_idle_add_full)(G_PRIORITY_DEFAULT, pdf_end, m, NULL);
}

static void on_pdf_failed(gpointer op, GError *e, gpointer p) {
	making *m = p;
	if (!m->answered) asuka_error_failed(&m->err, "print", 0, "print: the PDF couldn't be made%s%s", e ? ": " : "", e ? e->message : "");
	pdf_answer(m);
}

static void on_pdf_finished(gpointer op, gpointer p) { pdf_answer(p); }

// pdf_titled makes the PDF of a page whose title is known.
static void pdf_titled(print_task *t, GtkWidget *view, const char *title) {
	asuka_error e = {0};
	char *path = print_new_pdf(title, &e);
	if (!path) {
		G(g_object_unref)(view);
		print_fail(t, &e);
		print_release(t);
		return;
	}
	const print_Layout *l = &t->layout;
	making *m = calloc(1, sizeof *m);
	m->t = t;
	m->view = view;
	m->path = path;
	m->op = pk.op_new(view);
	gpointer settings = pk.settings_new();
	pk.settings_set_printer(settings, "Print to File");
	pk.settings_set(settings, "output-file-format", "pdf");
	GFile *f = G(g_file_new_for_path)(path);
	gchar *uri = G(g_file_get_uri)(f);
	pk.settings_set(settings, "output-uri", uri);
	G(g_free)(uri);
	G(g_object_unref)(f);
	gpointer setup = pk.setup_new();
	gpointer size = pk.paper_new_custom("asuka", "Page", l->width, l->height, unit_points);
	pk.set_paper_size(setup, size);
	pk.paper_free(size);
	pk.set_orientation(setup, 0);  // the size says it
	pk.set_top(setup, l->top, unit_points);
	pk.set_bottom(setup, l->bottom, unit_points);
	pk.set_left(setup, l->left, unit_points);
	pk.set_right(setup, l->right, unit_points);
	pk.op_set_settings(m->op, settings);
	pk.op_set_setup(m->op, setup);
	G(g_object_unref)(settings);
	G(g_object_unref)(setup);
	m->handlers[0] = g_signal_connect(m->op, "failed", on_pdf_failed, m);
	m->handlers[1] = g_signal_connect(m->op, "finished", on_pdf_finished, m);
	pk.op_print(m->op);
}

typedef struct {
	print_task *t;
	GtkWidget *view;
} titling;

static void title_read(GObject *src, GAsyncResult *res, gpointer p) {
	titling *ti = p;
	gpointer v = pk.evaluate_finish(ti->view, res, NULL);
	char *s = v && pk.to_string ? pk.to_string(v) : NULL;
	pdf_titled(ti->t, ti->view, s ? s : "");
	G(g_free)(s);
	if (v) G(g_object_unref)(v);
	free(ti);
}

static void pdf_loaded(print_task *t, GtkWidget *view, asuka_error *err) {
	if (err) {
		print_fail(t, err);
		print_release(t);
		return;
	}
	// The page's title, as its script sees it (WebKit's title property
	// comes after the load, from the web process).
	const char *title = pk.get_title ? pk.get_title(view) : NULL;
	if ((title && *title) || !pk.evaluate || !pk.evaluate_finish || !pk.to_string) {
		pdf_titled(t, view, title ? title : "");
		return;
	}
	titling *ti = malloc(sizeof *ti);
	*ti = (titling){t, view};
	pk.evaluate(view, "document.title", -1, NULL, NULL, NULL, title_read, ti);
}

void print_pdf(print_task *t) {
	asuka_error e = {0};
	if (!load(&e)) {
		print_fail(t, &e);
		print_release(t);
		return;
	}
	load_page(t, t->layout.html, pdf_loaded);
}

// ---- file ----

// A portal's request: its method answers through a Request object's
// Response signal.
typedef struct request request;
typedef void (*request_done)(request *r, guint32 code, GVariant *results, GError *e);

struct request {
	GDBusConnection *bus;
	char *path;
	guint sub;
	bool answered;
	request_done done;
	void *ctx;
};

static void request_end(request *r, guint32 code, GVariant *results, GError *e) {
	if (r->answered) {
		if (e) G(g_error_free)(e);
		return;
	}
	r->answered = true;
	if (r->sub) G(g_dbus_connection_signal_unsubscribe)(r->bus, r->sub);
	r->sub = 0;
	r->done(r, code, results, e);
}

static void on_response(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params,
                        gpointer p) {
	guint32 code = 2;
	GVariant *results = NULL;
	if (G(g_variant_is_of_type)(params, G_VARIANT_TYPE("(ua{sv})"))) G(g_variant_get)(params, "(u@a{sv})", &code, &results);
	request_end(p, code, results, NULL);
	if (results) G(g_variant_unref)(results);
}

static void listen_at(request *r, const char *path) {
	if (r->sub) G(g_dbus_connection_signal_unsubscribe)(r->bus, r->sub);
	free(r->path);
	r->path = asuka_strdup(path);
	r->sub = G(g_dbus_connection_signal_subscribe)(r->bus, ASUKA_PORTAL, "org.freedesktop.portal.Request", "Response", r->path, NULL,
	                                               G_DBUS_SIGNAL_FLAGS_NONE, on_response, r, NULL);
}

static void on_reply(GObject *src, GAsyncResult *res, gpointer p) {
	request *r = p;
	GError *e = NULL;
	GVariant *v = G(g_dbus_connection_call_with_unix_fd_list_finish)(r->bus, NULL, res, &e);
	if (!v) {
		request_end(r, 2, NULL, e);
		return;
	}
	// An older portal's request path isn't the token's.
	const gchar *path = NULL;
	if (!r->answered && G(g_variant_is_of_type)(v, G_VARIANT_TYPE("(o)"))) {
		G(g_variant_get)(v, "(&o)", &path);
		if (path && strcmp(path, r->path)) listen_at(r, path);
	}
	G(g_variant_unref)(v);
}

// portal_request calls the print portal's method; make makes its
// parameters with the request's handle token; fd, if not -1, goes with
// them (index 0). done gets the response (0 done, 1 cancelled by the
// user, 2 another end) and its results, on the main thread.
static void portal_request(const char *method, GVariant *(*make)(void *ctx, const char *token), int fd, request_done done, void *ctx) {
	static int tokens;
	request *r = calloc(1, sizeof *r);
	r->done = done;
	r->ctx = ctx;
	GError *e = NULL;
	r->bus = asuka_linux_bus(G_BUS_TYPE_SESSION, &e);
	if (!r->bus) {
		r->answered = true;
		done(r, 2, NULL, e);
		return;
	}
	char token[32];
	snprintf(token, sizeof token, "asuka%d", ++tokens);
	char *sender = asuka_strdup(G(g_dbus_connection_get_unique_name)(r->bus) + 1);
	for (char *s = sender; *s; s++)
		if (*s == '.') *s = '_';
	char *path = asuka_sprintf("/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
	free(sender);
	listen_at(r, path);
	free(path);
	GUnixFDList *fds = NULL;
	if (fd >= 0) {
		fds = G(g_unix_fd_list_new)();
		G(g_unix_fd_list_append)(fds, fd, NULL);
	}
	G(g_dbus_connection_call_with_unix_fd_list)(r->bus, ASUKA_PORTAL, ASUKA_PORTAL_PATH, "org.freedesktop.portal.Print", method, make(ctx, token),
	                                            G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, fds, NULL, on_reply, r);
	if (fds) G(g_object_unref)(fds);
}

static void request_free(request *r) {
	free(r->path);
	free(r);
}

// no_portal reports whether e says there's no portal (or no print
// portal).
static bool no_portal(const GError *e) {
	static const char *says[] = {"ServiceUnknown", "UnknownMethod", "UnknownInterface", "UnknownObject", "No such interface", "NameHasNoOwner"};
	if (!e || !e->message) return false;
	for (size_t i = 0; i < sizeof says / sizeof *says; i++)
		if (strstr(e->message, says[i])) return true;
	return false;
}

typedef struct {
	print_task *t;
	char *path, *title;
	int fd;
	guint32 token;
	GtkWidget *dialog;
	bool answered;
} filing;

static void filing_end(filing *f, bool printed, asuka_error *err) {
	if (f->fd >= 0) close(f->fd);
	f->fd = -1;
	if (err) print_fail(f->t, err);
	else print_outcome(f->t, printed ? print_Outcome_printed : print_Outcome_cancelled);
	print_release(f->t);
	free(f->path);
	free(f->title);
	free(f);
}

static void job_sent(gpointer job, gpointer p, const GError *e) {
	asuka_error err = {0};
	if (e) asuka_error_failed(&err, "print", e->code, "print: %s", e->message);
	G(g_object_unref)(job);
	filing_end(p, !e, e ? &err : NULL);
}

static void on_dialog_response(GtkWidget *d, int resp, gpointer p) {
	filing *f = p;
	if (f->answered) return;
	f->answered = true;
	if (resp != response_ok) {
		pk.window_destroy(d);
		filing_end(f, false, NULL);
		return;
	}
	gpointer printer = pk.dialog_printer(d);
	gpointer settings = pk.dialog_settings(d);
	gpointer setup = pk.dialog_setup(d);
	gpointer job = pk.job_new(f->title, printer, settings, setup);
	if (settings) G(g_object_unref)(settings);
	pk.window_destroy(d);
	GError *e = NULL;
	if (!pk.job_set_source_file(job, f->path, &e)) {
		G(g_object_unref)(job);
		asuka_error err = {0};
		asuka_linux_fail(&err, e, "print");
		filing_end(f, false, &err);
		return;
	}
	pk.job_send(job, job_sent, f, NULL);
}

// file_dialog prints a file with GTK's print dialog and a print job (no
// portal).
static void file_dialog(filing *f) {
	asuka_error e = {0};
	if (!load(&e)) {
		filing_end(f, false, &e);
		return;
	}
	GtkWidget *d = pk.dialog_new(f->title, window());
	if (pk.dialog_set_caps) pk.dialog_set_caps(d, caps_preview);
	if (pk.window_set_modal) pk.window_set_modal(d, true);
	g_signal_connect(d, "response", on_dialog_response, f);
	G(gtk_window_present)((GtkWindow *)d);
}

static GVariant *empty(void) {
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("a{sv}"));
	return G(g_variant_builder_end)(&b);
}

static GVariant *print_params(void *ctx, const char *token) {
	filing *f = ctx;
	GVariantBuilder o;
	G(g_variant_builder_init)(&o, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&o, "{sv}", "handle_token", G(g_variant_new_string)(token));
	G(g_variant_builder_add)(&o, "{sv}", "token", G(g_variant_new_uint32)(f->token));
	G(g_variant_builder_add)(&o, "{sv}", "modal", G(g_variant_new_boolean)(true));
	return G(g_variant_new)("(ssh@a{sv})", "", f->title, 0, G(g_variant_builder_end)(&o));
}

static void printed(request *r, guint32 code, GVariant *results, GError *e) {
	filing *f = r->ctx;
	request_free(r);
	if (e) {
		asuka_error err = {0};
		asuka_linux_fail(&err, e, "print");
		filing_end(f, false, &err);
		return;
	}
	filing_end(f, code == 0, NULL);
}

static GVariant *prepare_params(void *ctx, const char *token) {
	filing *f = ctx;
	GVariantBuilder setup, o;
	G(g_variant_builder_init)(&setup, G_VARIANT_TYPE("a{sv}"));
	if (f->t->job.landscape) G(g_variant_builder_add)(&setup, "{sv}", "Orientation", G(g_variant_new_string)("landscape"));
	G(g_variant_builder_init)(&o, G_VARIANT_TYPE("a{sv}"));
	G(g_variant_builder_add)(&o, "{sv}", "handle_token", G(g_variant_new_string)(token));
	G(g_variant_builder_add)(&o, "{sv}", "modal", G(g_variant_new_boolean)(true));
	return G(g_variant_new)("(ss@a{sv}@a{sv}@a{sv})", "", f->title, empty(), G(g_variant_builder_end)(&setup), G(g_variant_builder_end)(&o));
}

static void prepared(request *r, guint32 code, GVariant *results, GError *e) {
	filing *f = r->ctx;
	request_free(r);
	if (no_portal(e)) {
		G(g_error_free)(e);
		close(f->fd);
		f->fd = -1;
		file_dialog(f);
		return;
	}
	if (e || code != 0) {
		asuka_error err = {0};
		if (e) asuka_linux_fail(&err, e, "print");
		filing_end(f, false, e ? &err : code == 1 ? NULL : (asuka_error_failed(&err, "print", 0, "print: the print portal failed"), &err));
		return;
	}
	GVariant *tok = results ? G(g_variant_lookup_value)(results, "token", G_VARIANT_TYPE("u")) : NULL;
	if (tok) {
		f->token = G(g_variant_get_uint32)(tok);
		G(g_variant_unref)(tok);
	}
	portal_request("Print", print_params, f->fd, printed, f);
}

void print_file(print_task *t, const char *path) {
	filing *f = calloc(1, sizeof *f);
	f->t = t;
	f->path = asuka_strdup(path);
	const char *base = strrchr(path, '/');
	f->title = print_job_name(&t->job, base ? base + 1 : path);
	f->fd = open(path, O_RDONLY | O_CLOEXEC);
	if (f->fd < 0) {
		asuka_error err = {0};
		asuka_error_failed(&err, "print", 0, "print: can't open %s", path);
		filing_end(f, false, &err);
		return;
	}
	portal_request("PreparePrint", prepare_params, -1, prepared, f);
}
