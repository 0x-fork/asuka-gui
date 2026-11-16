// asuka/js on Windows: a WebView2 page that's never shown
// (asuka_windows.h's, in the process's WebView2 environment), the script
// run through DevTools' Runtime.evaluate, which awaits a promise.
#include "asuka_windows.h"
#include "js.h"

#include <stdlib.h>

void js_stop(js_run *r) {
	if (r->page) asuka_win_web_page_close(r->page);
	r->page = NULL;
}

static void evaluated(void *ctx, char *json, asuka_error *err) {
	js_run *r = ctx;
	if (r->done) {  // answered already (a timeout, a cancel)
		free(json);
		if (err) asuka_error_free(err);
	} else {
		js_answer(r, json, err);
	}
	js_unref(r);
}

static void loaded(void *ctx, asuka_web_page *page, asuka_error *err) {
	js_run *r = ctx;
	if (r->done) {  // timed out or cancelled while it loaded
		asuka_win_web_page_close(page);
	} else if (!page) {
		js_answer(r, NULL, err);
	} else {
		r->page = page;
		r->refs++;  // evaluated's
		asuka_win_web_page_evaluate(page, r->script, evaluated, r);
	}
	js_unref(r);  // the load's
}

void js_start(js_run *r) {
	r->refs++;  // the page's load, which answers even after the run did
	asuka_win_web_page("<!doctype html><html></html>", loaded, r);
}
