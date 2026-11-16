// platform/shell on Linux: the file manager's org.freedesktop.FileManager1
// (Files, Dolphin, Nemo, Thunar...) and GtkFileLauncher (the OpenURI
// portal in a sandbox), GIO's trash, the display's beep. Each starts on
// the main thread, where GLib answers.
#include "asuka_linux.h"
#include "shell.asuka.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// there fails call unless something is at path (a link counts, for
// reveal and trash).
static bool there(const char *path, bool link, asuka_call *call) {
	struct stat st;
	if ((link ? lstat(path, &st) : stat(path, &st)) == 0) return true;
	asuka_error e = {0};
	asuka_error_failed(&e, "shell", 0, "no file at %s", path);
	asuka_call_fail(call, &e);
	return false;
}

static void answer(asuka_call *call, GError *e, bool no_app) {
	asuka_error err = {0};
	if (no_app) {
		if (e) G(g_error_free)(e);
		asuka_error_declared(&err, shell_ShellError_noApp);
		asuka_call_fail(call, &err);
	} else if (e) {
		asuka_linux_fail(&err, e, NULL);
		asuka_call_fail(call, &err);
	} else {
		shell_Desktop_open_done(call);  // every method answers nothing
	}
}

static void launched(GObject *src, GAsyncResult *res, gpointer call) {
	GError *e = NULL;
	bool ok = G(gtk_file_launcher_launch_finish)((GtkFileLauncher *)src, res, &e);
	G(g_object_unref)(src);
	if (ok || asuka_linux_cancelled(e)) {  // cancelled: the portal's app chooser closed
		if (e) G(g_error_free)(e);
		answer(call, NULL, false);
		return;
	}
	// G_IO_ERROR_NOT_SUPPORTED: GIO's "no application is registered".
	bool none = e && G(g_io_error_quark) && e->domain == G(g_io_error_quark)() && e->code == 15;
	answer(call, e, none);
}

// launch opens path in its app (a folder in the file manager), owned by
// the window the user works in.
static void launch(const char *path, asuka_call *call) {
	if (!G(gtk_file_launcher_new)) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "no GtkFileLauncher (GTK 4.10)");
		asuka_call_fail(call, &e);
		return;
	}
	GFile *f = G(g_file_new_for_path)(path);
	gpointer l = G(gtk_file_launcher_new)(f);
	G(g_object_unref)(f);
	G(gtk_file_launcher_launch)(l, asuka_desktop_window(0), NULL, launched, call);
}

typedef struct {
	char *dir;
	asuka_call *call;
} revealing;

static void shown(GObject *src, GAsyncResult *res, gpointer p) {
	revealing *r = p;
	GError *e = NULL;
	GVariant *v = G(g_dbus_connection_call_finish)((GDBusConnection *)src, res, &e);
	if (v) {
		G(g_variant_unref)(v);
		answer(r->call, NULL, false);
	} else {
		if (e) G(g_error_free)(e);
		launch(r->dir, r->call);  // no file manager: the folder, in its app
	}
	free(r->dir);
	free(r);
}

static void d_reveal(shell_Desktop *self, const char *path, asuka_call *call) {
	if (!there(path, true, call)) return;
	char abs[PATH_MAX];
	if (!realpath(path, abs)) snprintf(abs, sizeof abs, "%s", path);
	revealing *r = malloc(sizeof *r);
	char *slash = strrchr(abs, '/');
	r->dir = slash && slash != abs ? asuka_strndup(abs, (size_t)(slash - abs)) : asuka_strdup("/");
	r->call = call;
	GDBusConnection *c = asuka_linux_bus(G_BUS_TYPE_SESSION, NULL);
	if (!c) {
		launch(r->dir, call);
		free(r->dir);
		free(r);
		return;
	}
	GFile *f = G(g_file_new_for_path)(abs);
	gchar *uri = G(g_file_get_uri)(f);
	G(g_object_unref)(f);
	GVariantBuilder b;
	G(g_variant_builder_init)(&b, G_VARIANT_TYPE("as"));
	G(g_variant_builder_add)(&b, "s", uri);
	G(g_free)(uri);
	G(g_dbus_connection_call)(c, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", "ShowItems",
	                          G(g_variant_new)("(ass)", &b, ""), NULL, 0, 5000, NULL, shown, r);
}

static void d_open(shell_Desktop *self, const char *path, asuka_call *call) {
	if (there(path, false, call)) launch(path, call);
}

static void trashed(GObject *src, GAsyncResult *res, gpointer call) {
	GError *e = NULL;
	bool ok = G(g_file_trash_finish)((GFile *)src, res, &e);
	G(g_object_unref)(src);
	answer(call, ok ? NULL : e, false);
}

static void d_trash(shell_Desktop *self, const char *path, asuka_call *call) {
	if (!there(path, true, call)) return;
	G(g_file_trash_async)(G(g_file_new_for_path)(path), G_PRIORITY_DEFAULT, NULL, trashed, call);
}

static void d_beep(shell_Desktop *self, asuka_error *err) {
	GdkDisplay *d = G(gdk_display_get_default) ? G(gdk_display_get_default)() : NULL;
	if (d) G(gdk_display_beep)(d);
}

static const shell_Desktop_vtable vt = {.reveal = d_reveal, .open = d_open, .trash = d_trash, .beep = d_beep};

shell_Desktop *shell_make_Desktop(asuka_error *err) {
	if (!asuka_linux()) {
		asuka_error_unsupported(err, "no GLib");
		return NULL;
	}
	shell_Desktop *d = malloc(sizeof *d);
	d->vt = &vt;
	return d;
}
