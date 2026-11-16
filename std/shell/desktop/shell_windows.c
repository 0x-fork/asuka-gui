// asuka/shell on Windows: Explorer (SHOpenFolderAndSelectItems),
// ShellExecute's open, the Recycle Bin (IFileOperation), MessageBeep.
// Reveal and Trash wait for the shell on a thread of their own (its
// apartment's); ShellExecute, which may ask which app opens a type, runs
// on the main thread, owned by the window the user works in.
#include "asuka_windows.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include "shell.asuka.h"

#include <stdlib.h>
#include <string.h>

static bool exists(const char *path, asuka_call *call) {
	wchar_t *w = asuka_wide(path);
	DWORD a = GetFileAttributesW(w);
	free(w);
	if (a != INVALID_FILE_ATTRIBUTES) return true;
	asuka_error e = {0};
	asuka_error_failed(&e, "shell", 0, "no file at %s", path);
	asuka_call_fail(call, &e);
	return false;
}

static void fail_hr(asuka_call *call, HRESULT hr, const char *what) {
	asuka_error e = {0};
	asuka_win_fail(&e, hr, what);
	asuka_call_fail(call, &e);
}

typedef struct {
	char *path;
	asuka_call *call;
	int op;  // 0 reveal, 1 trash
} job;

// The Recycle Bin, through IFileOperation, as Explorer's delete: Windows
// deletes an item for good when it can't recycle it (one too big for the
// Recycle Bin, or on a drive without one, as network shares and many
// removable drives are), and with no UI nothing asks first; a progress
// sink refuses those deletes, so trash fails instead.
typedef struct {
	IFileOperationProgressSink base;
	LONG refs;
	bool refused;
} sink;

static HRESULT STDMETHODCALLTYPE s_query(IFileOperationProgressSink *t, REFIID iid, void **out) {
	if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IFileOperationProgressSink)) {
		*out = t;
		InterlockedIncrement(&((sink *)t)->refs);
		return S_OK;
	}
	*out = NULL;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE s_addref(IFileOperationProgressSink *t) { return (ULONG)InterlockedIncrement(&((sink *)t)->refs); }
static ULONG STDMETHODCALLTYPE s_release(IFileOperationProgressSink *t) { return (ULONG)InterlockedDecrement(&((sink *)t)->refs); }
static HRESULT STDMETHODCALLTYPE s_ok0(IFileOperationProgressSink *t) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_finish(IFileOperationProgressSink *t, HRESULT hr) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_pre_rename(IFileOperationProgressSink *t, DWORD f, IShellItem *i, LPCWSTR n) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_post_rename(IFileOperationProgressSink *t, DWORD f, IShellItem *i, LPCWSTR n, HRESULT hr, IShellItem *o) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_pre_move(IFileOperationProgressSink *t, DWORD f, IShellItem *i, IShellItem *d, LPCWSTR n) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_post_move(IFileOperationProgressSink *t, DWORD f, IShellItem *i, IShellItem *d, LPCWSTR n, HRESULT hr, IShellItem *o) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_pre_delete(IFileOperationProgressSink *t, DWORD flags, IShellItem *i) {
	if (!(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE)) {
		((sink *)t)->refused = true;
		return E_ABORT;
	}
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE s_post_delete(IFileOperationProgressSink *t, DWORD f, IShellItem *i, HRESULT hr, IShellItem *n) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_pre_new(IFileOperationProgressSink *t, DWORD f, IShellItem *d, LPCWSTR n) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_post_new(IFileOperationProgressSink *t, DWORD f, IShellItem *d, LPCWSTR n, LPCWSTR tpl, DWORD a, HRESULT hr, IShellItem *o) { return S_OK; }
static HRESULT STDMETHODCALLTYPE s_progress(IFileOperationProgressSink *t, UINT total, UINT sofar) { return S_OK; }

static IFileOperationProgressSinkVtbl sink_vtbl = {
    s_query, s_addref, s_release, s_ok0, s_finish, s_pre_rename, s_post_rename, s_pre_move, s_post_move,
    s_pre_move, s_post_move,  // PreCopyItem, PostCopyItem: the same shapes
    s_pre_delete, s_post_delete, s_pre_new, s_post_new, s_progress, s_ok0, s_ok0, s_ok0,
};

static void run(void *p) {
	job *j = p;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	wchar_t *w = asuka_wide(j->path);
	if (j->op == 0) {
		PIDLIST_ABSOLUTE pidl = NULL;
		HRESULT hr = SHParseDisplayName(w, NULL, &pidl, 0, NULL);
		if (FAILED(hr) || !pidl) fail_hr(j->call, hr, j->path);
		else if (FAILED(hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0))) fail_hr(j->call, hr, j->path);  // no children: pidl selected in its folder
		else shell_Desktop_reveal_done(j->call);
		if (pidl) CoTaskMemFree(pidl);
	} else {
		IShellItem *item = NULL;
		IFileOperation *op = NULL;
		HRESULT hr = SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item);
		if (SUCCEEDED(hr)) hr = CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_ALL, &IID_IFileOperation, (void **)&op);
		sink s = {{&sink_vtbl}, 1, false};
		if (SUCCEEDED(hr))
			hr = IFileOperation_SetOperationFlags(op, FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD);
		if (SUCCEEDED(hr)) hr = IFileOperation_DeleteItem(op, item, &s.base);
		BOOL aborted = FALSE;
		if (SUCCEEDED(hr)) {
			hr = IFileOperation_PerformOperations(op);
			IFileOperation_GetAnyOperationsAborted(op, &aborted);
		}
		if (s.refused) {
			asuka_error e = {0};
			asuka_error_failed(&e, "shell", 0, "%s can't go to the Recycle Bin: it would be deleted for good", j->path);
			asuka_call_fail(j->call, &e);
		} else if (FAILED(hr)) {
			fail_hr(j->call, hr, j->path);
		} else if (aborted) {
			asuka_error e = {0};
			asuka_error_failed(&e, "shell", 0, "couldn't move %s to the Recycle Bin", j->path);
			asuka_call_fail(j->call, &e);
		} else {
			shell_Desktop_trash_done(j->call);
		}
		if (op) IFileOperation_Release(op);
		if (item) IShellItem_Release(item);
	}
	free(w);
	CoUninitialize();
	free(j->path);
	free(j);
}

static void start(const char *path, asuka_call *call, int op) {
	if (!exists(path, call)) return;
	job *j = malloc(sizeof *j);
	*j = (job){asuka_strdup(path), call, op};
	if (!asuka_desktop_spawn(run, j)) {
		free(j->path);
		free(j);
		fail_hr(call, E_OUTOFMEMORY, "no thread");
	}
}

static void d_reveal(shell_Desktop *self, const char *path, asuka_call *call) { start(path, call, 0); }
static void d_trash(shell_Desktop *self, const char *path, asuka_call *call) { start(path, call, 1); }

static void d_open(shell_Desktop *self, const char *path, asuka_call *call) {
	if (!exists(path, call)) return;
	wchar_t *file = asuka_wide(path);
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
	if (ok || code == ERROR_CANCELLED) {
		shell_Desktop_open_done(call);
	} else if ((INT_PTR)sei.hInstApp == SE_ERR_NOASSOC || code == ERROR_NO_ASSOCIATION || code == SE_ERR_NOASSOC) {
		asuka_error e = {0};
		asuka_error_declared(&e, shell_ShellError_noApp);
		asuka_call_fail(call, &e);
	} else {
		fail_hr(call, HRESULT_FROM_WIN32(code), path);
	}
}

static void d_beep(shell_Desktop *self, asuka_error *err) { MessageBeep(MB_OK); }

static const shell_Desktop_vtable vt = {.reveal = d_reveal, .open = d_open, .trash = d_trash, .beep = d_beep};

shell_Desktop *shell_make_Desktop(asuka_error *err) {
	shell_Desktop *d = malloc(sizeof *d);
	d->vt = &vt;
	return d;
}
