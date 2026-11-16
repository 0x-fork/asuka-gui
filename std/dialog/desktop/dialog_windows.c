// asuka/dialog on Windows: the common item dialogs (IFileOpenDialog,
// IFileSaveDialog), modal to the window the user works in, shown on the
// main thread. They run a modal loop, posted work going on meanwhile.
#include "asuka_windows.h"
#include <shobjidl.h>
#include "dialog.asuka.h"

#include <stdlib.h>
#include <string.h>

// families are the extensions of a family of MIME types, as Windows's own
// apps know them.
static const char *families[][2] = {
    {"image/*", "png jpg jpeg gif bmp webp heic heif tif tiff ico svg avif"},
    {"video/*", "mp4 m4v mov webm avi wmv mkv"},
    {"audio/*", "mp3 m4a aac wav flac ogg opus wma"},
    {"text/*", "txt csv md html htm xml json log"},
    {"font/*", "ttf otf woff woff2"},
};

static void add_pattern(asuka_writer *w, const char *ext) {
	if (*ext == '.') ext++;
	char *p = strcmp(ext, "*") == 0 ? asuka_strdup("*.*") : asuka_sprintf("*.%s", ext);
	// Once each.
	asuka_writer_u8(w, 0);
	w->len--;
	const char *all = (const char *)w->data;
	size_t pn = strlen(p);
	bool seen = false;
	for (const char *q = all; q && *q;) {
		size_t n = strcspn(q, ";");
		if (n == pn && strncmp(q, p, n) == 0) seen = true;
		q = q[n] ? q + n + 1 : NULL;
	}
	if (!seen) {
		if (w->len) asuka_writer_put(w, ";", 1);
		asuka_writer_put(w, p, pn);
	}
	free(p);
}

// registry_ext is a MIME type's extension as the registry knows it
// (HKCR\MIME\Database\Content Type\<type>'s Extension), or NULL.
static char *registry_ext(const char *type) {
	char *key = asuka_sprintf("MIME\\Database\\Content Type\\%s", type);
	wchar_t *wk = asuka_wide(key);
	free(key);
	wchar_t buf[64];
	DWORD n = sizeof buf;
	LSTATUS r = RegGetValueW(HKEY_CLASSES_ROOT, wk, L"Extension", RRF_RT_REG_SZ, NULL, buf, &n);
	free(wk);
	return r == ERROR_SUCCESS ? asuka_narrow(buf, -1) : NULL;
}

typedef struct {
	wchar_t *name, *spec;
} spec_t;

// filters are the dialog's file types (each filter's patterns: its
// extensions, its MIME types' as the registry gives them) and the
// extension a name typed without one gets (the first filter's first).
static size_t filters(const dialog_list_Filter *fs, spec_t **out, char **def) {
	*out = calloc(fs->len ? fs->len : 1, sizeof **out);
	*def = NULL;
	size_t n = 0;
	for (size_t i = 0; i < fs->len; i++) {
		const dialog_Filter *f = &fs->data[i];
		asuka_writer w = {0};
		for (size_t k = 0; k < f->extensions.len; k++) add_pattern(&w, f->extensions.data[k]);
		for (size_t k = 0; k < f->types.len; k++) {
			const char *t = f->types.data[k];
			if (strcmp(t, "*/*") == 0 || strcmp(t, "*") == 0 || strcmp(t, "application/*") == 0) {
				add_pattern(&w, "*");
				continue;
			}
			bool family = false;
			for (size_t m = 0; m < sizeof families / sizeof *families; m++) {
				if (strcmp(t, families[m][0]) != 0) continue;
				family = true;
				char *list = asuka_strdup(families[m][1]);
				for (char *e = strtok(list, " "); e; e = strtok(NULL, " ")) add_pattern(&w, e);
				free(list);
			}
			if (!family) {
				char *e = registry_ext(t);
				if (e) add_pattern(&w, e), free(e);
			}
		}
		if (!w.len) {
			asuka_writer_free(&w);
			continue;
		}
		asuka_writer_u8(&w, 0);
		const char *pats = (const char *)w.data;
		if (!*def && n == 0 && strncmp(pats, "*.*", 3) != 0) *def = asuka_strndup(pats + 2, strcspn(pats + 2, ";"));
		(*out)[n++] = (spec_t){asuka_wide(f->name), asuka_wide(pats)};
		asuka_writer_free(&w);
	}
	return n;
}

static char *item_path(IShellItem *item) {
	wchar_t *p = NULL;
	if (FAILED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &p)) || !p) return NULL;
	char *s = asuka_narrow(p, -1);
	CoTaskMemFree(p);
	return s;
}

static IShellItem *shell_item(const char *path) {
	wchar_t *w = asuka_wide(path);
	IShellItem *it = NULL;
	SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&it);
	free(w);
	return it;
}

static bool is_dir(const char *path) {
	wchar_t *w = asuka_wide(path);
	DWORD a = GetFileAttributesW(w);
	free(w);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool exists(const char *path) {
	wchar_t *w = asuka_wide(path);
	DWORD a = GetFileAttributesW(w);
	free(w);
	return a != INVALID_FILE_ATTRIBUTES;
}

static void show(int kind, const dialog_Request *r, asuka_call *call) {
	bool save = kind == 1, folders = kind == 2;
	IFileDialog *d = NULL;
	HRESULT hr = CoCreateInstance(save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
	                              save ? &IID_IFileSaveDialog : &IID_IFileOpenDialog, (void **)&d);
	asuka_error e = {0};
	if (FAILED(hr) || !d) {
		asuka_win_fail(&e, hr, "the file dialog isn't available");
		asuka_call_fail(call, &e);
		return;
	}
	FILEOPENDIALOGOPTIONS o = 0;
	IFileDialog_GetOptions(d, &o);
	o |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
	if (save) o |= FOS_OVERWRITEPROMPT;
	else if (folders) o |= FOS_PICKFOLDERS;
	else o |= FOS_FILEMUSTEXIST;
	if (r->multiple && !save) o |= FOS_ALLOWMULTISELECT;
	if (r->showHidden) o |= FOS_FORCESHOWHIDDEN;
	IFileDialog_SetOptions(d, o);
	wchar_t *w;
	if (*r->title) w = asuka_wide(r->title), IFileDialog_SetTitle(d, w), free(w);
	if (*r->button) w = asuka_wide(r->button), IFileDialog_SetOkButtonLabel(d, w), free(w);
	// The folder it starts in, and the name it suggests, from start: a
	// folder, or a file (in a folder that's there).
	char *dir = NULL, *name = NULL;
	if (*r->start) {
		if (is_dir(r->start)) {
			dir = asuka_strdup(r->start);
		} else {
			const char *sep = strrchr(r->start, '\\');
			const char *fs = strrchr(r->start, '/');
			if (fs > sep) sep = fs;
			dir = sep ? asuka_strndup(r->start, (size_t)(sep - r->start)) : NULL;
			// open: a file that's there is selected (its name in the name
			// field); save suggests its name.
			if (!folders && (save || exists(r->start))) name = asuka_strdup(sep ? sep + 1 : r->start);
		}
	}
	if (save && *r->name) free(name), name = asuka_strdup(r->name);
	if (dir) {
		IShellItem *f = shell_item(dir);
		if (f) IFileDialog_SetFolder(d, f), IShellItem_Release(f);
	}
	if (name) w = asuka_wide(name), IFileDialog_SetFileName(d, w), free(w);
	free(dir);
	free(name);
	spec_t *specs = NULL;
	char *def = NULL;
	size_t ns = 0;
	if (!folders) {
		ns = filters(&r->filters, &specs, &def);
		if (ns) {
			IFileDialog_SetFileTypes(d, (UINT)ns, (const COMDLG_FILTERSPEC *)specs);  // copied
			IFileDialog_SetFileTypeIndex(d, 1);
		}
		if (save && def) w = asuka_wide(def), IFileDialog_SetDefaultExtension(d, w), free(w);
	}
	for (size_t i = 0; i < ns; i++) free(specs[i].name), free(specs[i].spec);
	free(specs);
	free(def);
	hr = IFileDialog_Show(d, (HWND)asuka_desktop_window(0));
	char **paths = NULL;
	size_t n = 0;
	if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		hr = S_OK;  // cancelled: nothing chosen
	} else if (SUCCEEDED(hr) && save) {
		IShellItem *item = NULL;
		if (SUCCEEDED(IFileDialog_GetResult(d, &item)) && item) {
			char *p = item_path(item);
			IShellItem_Release(item);
			if (p) paths = malloc(sizeof *paths), paths[n++] = p;
			else hr = E_FAIL, asuka_error_failed(&e, "dialog", 0, "the file chosen isn't in the file system");
		}
	} else if (SUCCEEDED(hr)) {
		IShellItemArray *items = NULL;
		if (SUCCEEDED(IFileOpenDialog_GetResults((IFileOpenDialog *)d, &items)) && items) {
			DWORD k = 0;
			IShellItemArray_GetCount(items, &k);
			paths = calloc(k ? k : 1, sizeof *paths);
			for (DWORD i = 0; i < k; i++) {
				IShellItem *item = NULL;
				if (SUCCEEDED(IShellItemArray_GetItemAt(items, i, &item)) && item) {
					char *p = item_path(item);
					if (p) paths[n++] = p;
					IShellItem_Release(item);
				}
			}
			IShellItemArray_Release(items);
		}
	}
	IFileDialog_Release(d);
	if (FAILED(hr)) {
		if (!e.status) asuka_win_fail(&e, hr, "the file dialog");
		asuka_call_fail(call, &e);
		for (size_t i = 0; i < n; i++) free(paths[i]);
		free(paths);
		return;
	}
	if (save) {
		dialog_Chooser_save_done(call, n ? paths[0] : NULL);
		free(paths);
		return;
	}
	dialog_list_string l = {paths ? paths : calloc(1, sizeof(char *)), n};
	if (folders) dialog_Chooser_folder_done(call, l);
	else dialog_Chooser_open_done(call, l);
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
