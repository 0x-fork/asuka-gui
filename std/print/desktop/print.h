// print.h: what print.c (the calls, both platforms) and each platform's
// printing (print_linux.c, print_windows.c) share.
#ifndef PRINT_H
#define PRINT_H

#include "print.asuka.h"

#include <stdbool.h>

// A call's printing. The platform's code holds a reference until it's
// done with it (print_release), the call another until it's answered
// (once: print_outcome, print_path or print_fail; a cancelled call's
// later answer is dropped).
typedef struct {
	asuka_call *call;  // NULL once answered
	int refs;
	bool file;  // file's: answers file's outcome
	print_Job job;  // a copy
	char *source;   // file's source, html's markup (a copy)
	print_Layout layout;  // pdf's (a copy)
	void *platform;
} print_task;

void print_outcome(print_task *t, print_Outcome o);
void print_path(print_task *t, char *path);  // path: malloc's, taken
void print_fail(print_task *t, asuka_error *e);
void print_release(print_task *t);

// print_job_name is a job's name: the app's, else fallback (a file's
// name, a page's title), else the app's name. Of malloc's.
char *print_job_name(const print_Job *j, const char *fallback);

// print_new_pdf is a new PDF's path in the app's caches (its folder
// made), named after title ("Document" for none): NULL with err set.
char *print_new_pdf(const char *title, asuka_error *err);

// The platform's, on the main thread: print a file (its path), markup,
// or make a PDF of markup.
void print_file(print_task *t, const char *path);
void print_html(print_task *t);
void print_pdf(print_task *t);

#endif
