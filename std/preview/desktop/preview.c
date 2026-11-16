// asuka/preview on Windows and Linux (macOS runs the package's Swift): the
// start file, found (an asset's, a download's) off the main thread, opens
// in the user's app for its type, over the window the user works in; the
// call answers once it's handed over.
//   Windows: ShellExecuteEx's open, which asks which app for a type that
//   has none.
//   Linux: GtkFileLauncher (the OpenURI portal in a sandbox).
#ifdef _WIN32
#include "asuka_windows.h"
#include <shellapi.h>
#else
#include "asuka_linux.h"
#endif
#include "preview.asuka.h"

#include <stdlib.h>

typedef struct {
	char *path;
	asuka_call *call;
} opening;

#ifdef _WIN32
static void open_main(void *p) {
	opening *o = p;
	wchar_t *file = asuka_wide(o->path);
	SHELLEXECUTEINFOW sei;
	memset(&sei, 0, sizeof sei);
	sei.cbSize = sizeof sei;
	sei.fMask = SEE_MASK_NOASYNC;
	sei.hwnd = (HWND)asuka_desktop_window(0);
	sei.lpFile = file;
	sei.nShow = SW_SHOWNORMAL;
	BOOL ok = ShellExecuteExW(&sei);
	DWORD code = ok ? 0 : GetLastError();
	free(file);
	if (ok || code == ERROR_CANCELLED) {  // cancelled: the user closed the dialog
		preview_Viewer_show_done(o->call);
	} else if ((INT_PTR)sei.hInstApp == SE_ERR_NOASSOC || code == ERROR_NO_ASSOCIATION || code == SE_ERR_NOASSOC) {
		asuka_error e = {0};
		asuka_error_declared(&e, preview_PreviewError_noViewer);
		asuka_call_fail(o->call, &e);
	} else {
		asuka_error e = {0};
		asuka_win_fail(&e, HRESULT_FROM_WIN32(code), o->path);
		asuka_call_fail(o->call, &e);
	}
	free(o->path);
	free(o);
}
#else
static void launched(GObject *source, GAsyncResult *res, gpointer p) {
	opening *o = p;
	GError *e = NULL;
	bool ok = G(gtk_file_launcher_launch_finish)(source, res, &e);
	G(g_object_unref)(source);
	if (ok || asuka_linux_cancelled(e)) {  // cancelled: the portal's app chooser closed
		if (e) G(g_error_free)(e);
		preview_Viewer_show_done(o->call);
	} else {
		if (e) G(g_error_free)(e);
		asuka_error err = {0};
		asuka_error_declared(&err, preview_PreviewError_noViewer);
		asuka_call_fail(o->call, &err);
	}
	free(o->path);
	free(o);
}

static void open_main(void *p) {
	opening *o = p;
	GFile *f = G(g_file_new_for_path)(o->path);
	gpointer l = G(gtk_file_launcher_new)(f);
	G(g_object_unref)(f);
	G(gtk_file_launcher_launch)(l, asuka_desktop_window(0), NULL, launched, o);
}
#endif

typedef struct {
	char *source;
	asuka_call *call;
} finding;

static void find(void *p) {
	finding *f = p;
	asuka_error e = {0};
	char *path = asuka_desktop_local_file(f->source, "", &e);
	if (!path) {
		asuka_call_fail(f->call, &e);
	} else {
		opening *o = malloc(sizeof *o);
		*o = (opening){path, f->call};
		asuka_desktop_post(open_main, o);
	}
	free(f->source);
	free(f);
}

static void show(preview_Viewer *self, preview_list_string files, int64_t start, bool edit, asuka_call *call) {
	asuka_error e = {0};
	if (files.len == 0) {
		asuka_error_declared(&e, preview_PreviewError_noViewer);
		asuka_call_fail(call, &e);
		return;
	}
#ifndef _WIN32
	if (!asuka_linux() || !G(gtk_file_launcher_new)) {
		asuka_error_unsupported(&e, "no GtkFileLauncher (GTK 4.10)");
		asuka_call_fail(call, &e);
		return;
	}
#endif
	size_t i = start < 0 ? 0 : (size_t)start >= files.len ? files.len - 1 : (size_t)start;
	finding *f = malloc(sizeof *f);
	*f = (finding){asuka_strdup(files.data[i]), call};
	if (!asuka_desktop_spawn(find, f)) {
		free(f->source);
		free(f);
		asuka_error_failed(&e, "preview", 0, "no thread");
		asuka_call_fail(call, &e);
	}
}

static const preview_Viewer_vtable vt = {.show = show};

preview_Viewer *preview_make_Viewer(asuka_error *err) {
	preview_Viewer *v = malloc(sizeof *v);
	v->vt = &vt;
	return v;
}
