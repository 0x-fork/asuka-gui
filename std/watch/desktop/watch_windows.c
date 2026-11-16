// std/watch on Windows: ReadDirectoryChangesW on the watched directory
// (its tree, with recursive; the file's directory, for a file), read on
// a thread of the watch's, overlapped, woken by an event to stop. The
// snapshot's ids are the files' (FileIdBothDirectoryInfo), for renames.
#include "asuka_windows.h"
#include "watch.h"

#include <stdlib.h>
#include <string.h>

struct wnative {
	HANDLE dir, stop;
	OVERLAPPED ov;
	uint8_t *buf;
};

enum { BUF = 64 * 1024 };

static uint64_t ft(LARGE_INTEGER t) { return (uint64_t)t.QuadPart * 100; }

static void walk(watcher *w, const char *path, const char *rel, wsnap *out) {
	wchar_t *wp = asuka_wide(path);
	HANDLE h = CreateFileW(wp, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(wp);
	if (h == INVALID_HANDLE_VALUE) return;
	uint8_t *buf = malloc(BUF);
	FILE_INFO_BY_HANDLE_CLASS cls = FileIdBothDirectoryRestartInfo;
	while (GetFileInformationByHandleEx(h, cls, buf, BUF)) {
		cls = FileIdBothDirectoryInfo;
		for (FILE_ID_BOTH_DIR_INFO *e = (FILE_ID_BOTH_DIR_INFO *)buf;; e = (FILE_ID_BOTH_DIR_INFO *)((uint8_t *)e + e->NextEntryOffset)) {
			char *name = asuka_narrow(e->FileName, (int)(e->FileNameLength / sizeof(wchar_t)));
			if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
				bool dir = (e->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				char *r = strcmp(rel, ".") == 0 ? asuka_strdup(name) : asuka_sprintf("%s/%s", rel, name);
				if (!watch_ignored(&w->ignore, r, dir)) {
					wsnap_add(out, (wentry){asuka_strdup(r), dir, (uint64_t)e->EndOfFile.QuadPart, ft(e->LastWriteTime), (uint64_t)e->FileId.QuadPart,
					                        (uint32_t)(e->FileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))});
					// Not into junctions and symbolic links.
					if (dir && w->recursive && !(e->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
						char *p = asuka_sprintf("%s\\%s", path, name);
						walk(w, p, r, out);
						free(p);
					}
				}
				free(r);
			}
			free(name);
			if (!e->NextEntryOffset) break;
		}
	}
	free(buf);
	CloseHandle(h);
}

static bool stat_root(const char *path, wentry *e, bool *dir) {
	wchar_t *wp = asuka_wide(path);
	HANDLE h = CreateFileW(wp, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	free(wp);
	if (h == INVALID_HANDLE_VALUE) return false;
	BY_HANDLE_FILE_INFORMATION fi;
	bool ok = GetFileInformationByHandle(h, &fi);
	CloseHandle(h);
	if (!ok) return false;
	*dir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	LARGE_INTEGER t;
	t.LowPart = fi.ftLastWriteTime.dwLowDateTime;
	t.HighPart = (LONG)fi.ftLastWriteTime.dwHighDateTime;
	*e = (wentry){asuka_strdup("."), *dir, (uint64_t)fi.nFileSizeHigh << 32 | fi.nFileSizeLow, ft(t), (uint64_t)fi.nFileIndexHigh << 32 | fi.nFileIndexLow,
	              (uint32_t)(fi.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))};
	return true;
}

bool watch_scan(watcher *w, wsnap *out) {
	wentry e;
	bool dir;
	if (!stat_root(w->root, &e, &dir)) return false;
	if (dir != w->root_dir) {
		free(e.rel);
		return false;
	}
	wsnap_add(out, e);
	if (dir) walk(w, w->root, ".", out);
	return true;
}

static bool arm(wnative *n, bool subtree) {
	ResetEvent(n->ov.hEvent);
	return ReadDirectoryChangesW(n->dir, n->buf, BUF, subtree,
	                             FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
	                                 FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY,
	                             NULL, &n->ov, NULL);
}

static void run(void *p) {
	watcher *w = p;
	wnative *n = w->native;
	bool subtree = w->root_dir && w->recursive;
	bool armed = arm(n, subtree);
	while (!watch_stopping(w)) {
		HANDLE hs[2] = {n->stop, n->ov.hEvent};
		int t = watch_timeout(w);
		DWORD r = WaitForMultipleObjects(armed ? 2 : 1, hs, FALSE, t < 0 ? (armed ? INFINITE : 200) : (DWORD)t);
		if (r == WAIT_OBJECT_0) break;
		if (armed && r == WAIT_OBJECT_0 + 1) {
			DWORD got = 0;
			bool ok = GetOverlappedResult(n->dir, &n->ov, &got, FALSE);
			// No bytes: the buffer overflowed, the changes dropped.
			watch_signal(w, ok && got == 0);
			armed = arm(n, subtree);
			if (!armed) watch_signal(w, false);  // the directory went: looking again says so
		}
		if (!watch_settle(w)) break;
	}
	CancelIoEx(n->dir, &n->ov);
	watch_exit(w);
}

bool watch_native_start(watcher *w, asuka_error *err) {
	char *dir = NULL;
	if (w->root_dir) {
		dir = asuka_strdup(w->root);
	} else {
		// A file, through its directory: replaced at its path, it's written.
		const char *s = strrchr(w->root, '\\');
		dir = s ? asuka_strndup(w->root, (size_t)(s - w->root) + (s - w->root == 2 ? 1 : 0)) : asuka_strdup(".");
	}
	wchar_t *wd = asuka_wide(dir);
	free(dir);
	wnative *n = calloc(1, sizeof *n);
	n->dir = CreateFileW(wd, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
	                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	free(wd);
	if (n->dir == INVALID_HANDLE_VALUE) {
		asuka_win_fail(err, HRESULT_FROM_WIN32(GetLastError()), "watch");
		free(n);
		return false;
	}
	n->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
	n->ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	n->buf = malloc(BUF);
	w->native = n;
	if (!watch_scan(w, &w->snap)) {
		asuka_error_failed(err, "watch", 0, "watch: %s went", w->root);
		return false;
	}
	if (!asuka_desktop_spawn(run, w)) {
		asuka_error_failed(err, "watch", 0, "no thread");
		return false;
	}
	return true;
}

void watch_native_wake(watcher *w) {
	if (w->native) SetEvent(w->native->stop);
}

void watch_native_free(watcher *w) {
	wnative *n = w->native;
	if (!n) return;
	CloseHandle(n->dir);
	CloseHandle(n->stop);
	CloseHandle(n->ov.hEvent);
	free(n->buf);
	free(n);
	w->native = NULL;
}
