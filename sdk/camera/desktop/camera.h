// camera.h: what camera's Windows and Linux C share (camera.c): a
// started call waiting on the camera, the kinds of code a view reads and
// the codes it's seen, the files photos and videos go in, and the take
// dialog's words.
#ifndef CAMERA_H
#define CAMERA_H

#include "camera.asuka.h"

// A call waiting on the camera (take, capture, record): kept in one slot
// (a view's, a dialog's, a photo being saved), which it clears as it's
// answered. If its task goes first, it's answered as cancelled, the slot
// cleared, and cancel(ctx) run (a recording stopped, a dialog closed).
// Main thread.
typedef struct camera_pending camera_pending;

// camera_wait makes the call wait in *slot (where it's kept until
// answered).
void camera_wait(asuka_call *call, void (*done)(asuka_call *call, char *value), camera_pending **slot, void (*cancel)(void *ctx), void *ctx);
// camera_move keeps the waiting call of *from in *to instead.
void camera_move(camera_pending **from, camera_pending **to);
// camera_answer answers the call in *slot (if any) with value; camera_fail
// with the platform's error (a message), camera_fail_declared with one of
// the package's.
void camera_answer(camera_pending **slot, const char *value);
void camera_fail(camera_pending **slot, const char *message);
void camera_fail_declared(camera_pending **slot, uint32_t index);

// The kinds of code a view reads (none: every kind).
typedef struct {
	char **kinds;
	size_t n;
} camera_kinds;

void camera_kinds_set(camera_kinds *k, camera_list_string list);
bool camera_kinds_wants(const camera_kinds *k, const char *kind);
void camera_kinds_free(camera_kinds *k);

// The codes in view: one is new as it comes into view, and again once
// it's been out of it a second.
typedef struct {
	struct {
		char *key;
		int64_t at;
	} *seen;
	size_t n, cap;
} camera_seen;

bool camera_seen_new(camera_seen *s, const char *kind, const char *value, int64_t now_ms);
void camera_seen_free(camera_seen *s);

// camera_now_ms is a monotonic clock's milliseconds.
int64_t camera_now_ms(void);

// camera_file is a new file's path (malloc's) in the app's caches'
// asuka-camera, of extension ext (".jpg"), made if missing: NULL if it
// can't be.
char *camera_file(const char *ext);

// The take dialog's words, in a language: the first that the user's
// languages (locales, most preferred first: "de-DE", "zh-Hant-TW", "pt_BR")
// match, else English.
typedef struct {
	const char *lang, *take, *cancel;
} camera_word;

extern const camera_word camera_words[];
const camera_word *camera_words_for(const char *const *locales, size_t n);

#endif
