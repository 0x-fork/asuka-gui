// platform/js on Windows and Linux (macOS runs the package's Swift): the
// platform's web view, never shown, made for each script on the main
// thread, where its answer comes; or the timeout's, or the call's
// cancellation, whichever is first.
#ifdef _WIN32
#include "asuka_windows.h"
#else
#include <time.h>
#endif
#include "js.h"

#include <stdlib.h>

void js_unref(js_run *r) {
	if (--r->refs > 0) return;
	free(r->script);
	free(r);
}

void js_answer(js_run *r, char *json, asuka_error *err) {
	if (r->done) {
		free(json);
		if (err) asuka_error_free(err);
		return;
	}
	r->done = true;
	js_stop(r);
	if (err) asuka_call_fail(r->call, err);
	else js_Engine_run_done(r->call, json);
	js_unref(r);
}

static void cancelled(void *p) {
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	js_answer(p, NULL, &e);
}

typedef struct {
	js_run *r;
	int64_t ns;
} timing;

static void timed_out(void *p) {
	js_run *r = p;
	asuka_error e = {0};
	asuka_error_declared(&e, js_JsError_timeout);
	js_answer(r, NULL, &e);
	js_unref(r);  // the timeout's
}

// wait sleeps the timeout on a thread of its own, then answers on the
// main thread (if nothing did first).
static void wait(void *p) {
	timing *t = p;
#ifdef _WIN32
	Sleep((DWORD)(t->ns / 1000000));
#else
	struct timespec ts = {t->ns / 1000000000, t->ns % 1000000000};
	nanosleep(&ts, NULL);
#endif
	asuka_desktop_post(timed_out, t->r);
	free(t);
}

static void run(js_Engine *self, const char *script, int64_t timeout, asuka_call *call) {
	js_run *r = calloc(1, sizeof *r);
	r->script = asuka_strdup(script);
	r->call = call;
	r->refs = 1;
	if (timeout > 0) {
		timing *t = malloc(sizeof *t);
		*t = (timing){r, timeout};
		r->refs++;
		if (!asuka_desktop_spawn(wait, t)) {
			free(t);
			r->refs--;
		}
	}
	asuka_call_on_cancel(call, cancelled, r);
	js_start(r);
}

static const js_Engine_vtable vt = {.run = run};

js_Engine *js_make_Engine(asuka_error *err) {
	js_Engine *e = malloc(sizeof *e);
	e->vt = &vt;
	return e;
}
