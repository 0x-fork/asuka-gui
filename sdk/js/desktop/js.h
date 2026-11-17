// js.h: what js's Windows and Linux C share (js.c): a script run in a
// page of its own, answered once, or timed out, or cancelled.
#ifndef JS_H
#define JS_H

#include "js.asuka.h"

typedef struct js_run js_run;

// The platform's: start loads a blank page and runs r's script in it,
// then calls js_answer; stop lets the page go. Main thread.
void js_start(js_run *r);
void js_stop(js_run *r);

// js_answer answers the run with the value's JSON, or err (taken), once;
// the platform's page goes with it. Main thread.
void js_answer(js_run *r, char *json, asuka_error *err);
// js_unref lets go of a reference to the run: the platform takes one
// (r->refs++) for each callback that may come after it answered.
void js_unref(js_run *r);

struct js_run {
	char *script;
	asuka_call *call;
	void *page;  // the platform's
	bool done;
	int refs;  // the run's, its timeout's
};

#endif
