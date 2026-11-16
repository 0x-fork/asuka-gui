// asuka/print on Windows: a file's app prints it (the shell's print
// verb); markup prints from a WebView2 page that's never shown, through
// its print dialog (the system's), which doesn't say what the user did:
// unknown. A PDF is Chromium's printing to PDF (DevTools'
// Page.printToPDF).
#include "asuka_windows.h"
#include "print.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_file(print_task *t, const char *path) {
	wchar_t *w = asuka_wide(path);
	// SEE_MASK_NOASYNC: done before it returns. Without SEE_MASK_FLAG_NO_UI,
	// Windows asks which app to use for a type with none, as Explorer does.
	SHELLEXECUTEINFOW sei;
	memset(&sei, 0, sizeof sei);
	sei.cbSize = sizeof sei;
	sei.fMask = SEE_MASK_NOASYNC;
	sei.hwnd = (HWND)asuka_desktop_window(0);
	sei.lpVerb = L"print";
	sei.lpFile = w;
	sei.nShow = SW_SHOWNORMAL;
	BOOL ok = ShellExecuteExW(&sei);
	DWORD code = ok ? 0 : GetLastError();
	INT_PTR inst = (INT_PTR)sei.hInstApp;
	free(w);
	asuka_error e = {0};
	if (ok || code == ERROR_CANCELLED) {  // the user closed the dialog
		print_outcome(t, print_Outcome_unknown);
	} else if (inst == SE_ERR_NOASSOC || code == ERROR_NO_ASSOCIATION || code == SE_ERR_NOASSOC) {
		asuka_error_declared(&e, print_PrintError_unsupported);
		print_fail(t, &e);
	} else {
		asuka_win_fail(&e, HRESULT_FROM_WIN32(code), "print");
		print_fail(t, &e);
	}
	print_release(t);
}

// The page html printed last, kept while its dialog may still print it
// (WebView2 doesn't say when it's done).
static asuka_web_page *last_page;

static void html_loaded(void *ctx, asuka_web_page *p, asuka_error *err) {
	print_task *t = ctx;
	if (!p) {
		print_fail(t, err);
	} else {
		if (last_page) asuka_win_web_page_close(last_page);
		last_page = p;
		asuka_win_web_page_print(p, 1);  // the system's dialog
		print_outcome(t, print_Outcome_unknown);
	}
	print_release(t);
}

void print_html(print_task *t) { asuka_win_web_page(t->source, html_loaded, t); }

typedef struct {
	print_task *t;
	asuka_web_page *page;
	char *title;
} making;

static int b64(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// unbase64 decodes standard base64 (NULL: not base64).
static bool unbase64(const char *s, asuka_bytes *out) {
	size_t n = strlen(s);
	uint8_t *p = malloc(n / 4 * 3 + 3);
	size_t len = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '=') break;
		int v = b64((unsigned char)s[i]);
		if (v < 0) {
			free(p);
			return false;
		}
		acc = acc << 6 | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			p[len++] = (uint8_t)(acc >> bits);
		}
	}
	*out = (asuka_bytes){p, len};
	return true;
}

static void made(void *ctx, char *json, asuka_error *err) {
	making *m = ctx;
	asuka_win_web_page_close(m->page);
	asuka_error e = {0};
	char *raw = json ? asuka_json_get(json, "data") : NULL;
	char *data = raw ? asuka_json_string(raw) : NULL;
	asuka_bytes pdf = {0};
	if (err) {
		asuka_error_free(err);
		asuka_error_failed(&e, "print", 0, "print: the PDF couldn't be made");
	} else if (!data || !unbase64(data, &pdf)) {
		asuka_error_failed(&e, "print", 0, "print: the PDF couldn't be made");
	}
	free(raw), free(data), free(json);
	char *path = e.status ? NULL : print_new_pdf(m->title, &e);
	if (path) {
		wchar_t *w = asuka_wide(path);
		FILE *f = _wfopen(w, L"wb");
		free(w);
		bool ok = f && fwrite(pdf.data, 1, pdf.len, f) == pdf.len;
		if (f && fclose(f) != 0) ok = false;
		if (!ok) {
			asuka_error_failed(&e, "print", 0, "print: can't write %s", path);
			free(path);
			path = NULL;
		}
	}
	free(pdf.data);
	if (path) print_path(m->t, path);
	else print_fail(m->t, &e);
	print_release(m->t);
	free(m->title);
	free(m);
}

static void titled(void *ctx, char *json, asuka_error *err) {
	making *m = ctx;
	if (err) asuka_error_free(err);
	char *t = json ? asuka_json_string(json) : NULL;
	m->title = t ? t : asuka_strdup("");
	free(json);
	const print_Layout *l = &m->t->layout;
	// DevTools takes inches.
	char *params = asuka_sprintf("{\"paperWidth\":%.6f,\"paperHeight\":%.6f,\"marginTop\":%.6f,\"marginRight\":%.6f,\"marginBottom\":%.6f,"
	                             "\"marginLeft\":%.6f,\"printBackground\":true,\"preferCSSPageSize\":false,\"transferMode\":\"ReturnAsBase64\"}",
	                             l->width / 72, l->height / 72, l->top / 72, l->right / 72, l->bottom / 72, l->left / 72);
	asuka_win_web_page_devtools(m->page, "Page.printToPDF", params, made, m);
	free(params);
}

static void pdf_loaded(void *ctx, asuka_web_page *p, asuka_error *err) {
	print_task *t = ctx;
	if (!p) {
		print_fail(t, err);
		print_release(t);
		return;
	}
	making *m = calloc(1, sizeof *m);
	m->t = t;
	m->page = p;
	asuka_win_web_page_evaluate(p, "document.title", titled, m);
}

void print_pdf(print_task *t) { asuka_win_web_page(t->layout.html, pdf_loaded, t); }
