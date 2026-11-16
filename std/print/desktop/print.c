// asuka/print on Windows and Linux (macOS runs the package's Swift): the
// calls, and what both platforms share. A file is found off the main
// thread (an asset's, a download's); the platform's dialogs and pages are
// on the main thread, where their answers come.
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include "print.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "asuka_windows.h"
#include <shlobj.h>
#define SEP "\\"
#else
#include <errno.h>
#include <sys/random.h>
#include <sys/stat.h>
#define SEP "/"
#endif

static void answered(print_task *t) {
	t->call = NULL;
	print_release(t);
}

void print_outcome(print_task *t, print_Outcome o) {
	if (!t->call) return;
	if (t->file) print_Printer_file_done(t->call, o);
	else print_Printer_html_done(t->call, o);
	answered(t);
}

void print_path(print_task *t, char *path) {
	if (!t->call) {
		free(path);
		return;
	}
	print_Printer_pdf_done(t->call, path);
	answered(t);
}

void print_fail(print_task *t, asuka_error *e) {
	if (!t->call) {
		asuka_error_free(e);
		return;
	}
	asuka_call_fail(t->call, e);
	answered(t);
}

void print_release(print_task *t) {
	if (--t->refs > 0) return;
	print_Job_free(&t->job);
	print_Layout_free(&t->layout);
	free(t->source);
	free(t);
}

char *print_job_name(const print_Job *j, const char *fallback) {
	if (j->name && *j->name) return asuka_strdup(j->name);
	if (fallback && *fallback) return asuka_strdup(fallback);
	return asuka_strdup(asuka_desktop_app_name());
}

static bool mkdirs(const char *dir) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(dir);
	int r = SHCreateDirectoryExW(NULL, w, NULL);
	free(w);
	return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
#else
	char *p = asuka_strdup(dir);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0755);
			*s = '/';
		}
	bool ok = mkdir(p, 0755) == 0 || errno == EEXIST;
	free(p);
	return ok;
#endif
}

char *print_new_pdf(const char *title, asuka_error *err) {
	const char *t = title ? title : "";
	while (*t == ' ' || *t == '\t' || *t == '\n') t++;
	char *name = asuka_strdup(t);
	for (size_t n = strlen(name); n && (name[n - 1] == ' ' || name[n - 1] == '\t' || name[n - 1] == '\n'); n--) name[n - 1] = 0;
	for (char *p = name; *p; p++)
		if (*p == '/' || *p == '\\' || *p == ':') *p = '-';
	if (!*name || !strncmp(title, "about:", 6) || !strncmp(title, "data:", 5)) {  // a page without a <title>
		free(name);
		name = asuka_strdup("Document");
	}
	uint8_t id[8];
#ifdef _WIN32
	for (int i = 0; i < 8; i += 4) {
		unsigned v = 0;
		rand_s(&v);
		memcpy(id + i, &v, 4);
	}
#else
	if (getrandom(id, sizeof id, 0) != (ssize_t)sizeof id)
		for (int i = 0; i < 8; i++) id[i] = (uint8_t)rand();
#endif
	char hex[17];
	for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02x", id[i]);
	char *dir = asuka_sprintf("%s" SEP "asuka-print" SEP "%s", asuka_desktop_cache_dir(), hex);
	char *path = NULL;
	if (mkdirs(dir)) path = asuka_sprintf("%s" SEP "%s.pdf", dir, name);
	else asuka_error_failed(err, "print", 0, "can't make %s", dir);
	free(dir);
	free(name);
	return path;
}

static print_task *task(asuka_call *call, const print_Job *job) {
	print_task *t = calloc(1, sizeof *t);
	t->call = call;
	t->refs = 2;
	if (job) {
		t->job.name = asuka_strdup(job->name);
		t->job.landscape = job->landscape;
		t->job.letter = job->letter;
	}
	return t;
}

static void cancelled(void *p) {
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	print_fail(p, &e);
}

typedef struct {
	print_task *t;
	char *path;
	asuka_error err;
} found;

static void found_main(void *p) {
	found *f = p;
	if (!f->path) {
		print_fail(f->t, &f->err);
		print_release(f->t);
	} else if (f->t->call) {
		print_file(f->t, f->path);  // its reference, the platform's
	} else {
		print_release(f->t);
	}
	free(f->path);
	free(f);
}

// find finds the file (downloading it): a worker's.
static void find(void *p) {
	found *f = p;
	f->path = asuka_desktop_local_file(f->t->source, "", &f->err);
	asuka_desktop_post(found_main, f);
}

static void p_file(print_Printer *self, const char *source, const print_Job *job, asuka_call *call) {
	print_task *t = task(call, job);
	t->source = asuka_strdup(source);
	t->file = true;
	asuka_call_on_cancel(call, cancelled, t);
	found *f = calloc(1, sizeof *f);
	f->t = t;
	if (!asuka_desktop_spawn(find, f)) {
		asuka_error_failed(&f->err, "print", 0, "no thread");
		found_main(f);
	}
}

static void p_html(print_Printer *self, const char *markup, const print_Job *job, asuka_call *call) {
	print_task *t = task(call, job);
	t->source = asuka_strdup(markup);
	asuka_call_on_cancel(call, cancelled, t);
	print_html(t);
}

static void p_pdf(print_Printer *self, const print_Layout *layout, asuka_call *call) {
	print_task *t = task(call, NULL);
	t->layout = *layout;
	t->layout.html = asuka_strdup(layout->html);
	asuka_call_on_cancel(call, cancelled, t);
	print_pdf(t);
}

static void p_release(print_Printer *self) {}

static const print_Printer_vtable vt = {.release = p_release, .file = p_file, .html = p_html, .pdf = p_pdf};

print_Printer *print_make_Printer(asuka_error *err) {
	static print_Printer printer = {&vt};
	return &printer;
}
