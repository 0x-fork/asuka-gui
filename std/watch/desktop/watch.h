// watch.h: what watch's Windows and Linux C share (watch.c). A watch
// keeps a snapshot of the watched tree and, a moment after the system
// says something changed, looks again and compares: the events are the
// differences.
#ifndef WATCH_H
#define WATCH_H

#include "watch.asuka.h"

// An entry of the snapshot: a path relative to the root, with slashes
// ("." the root itself).
typedef struct {
	char *rel;
	bool dir;
	uint64_t size, mtime, id;  // id: the file system's (inode, file index), for renames
	uint32_t mode;
} wentry;

typedef struct {
	wentry *e;
	size_t n, cap;
	size_t *slots;  // a hash index of e by rel (SIZE_MAX: empty)
	size_t nslots;
} wsnap;

void wsnap_add(wsnap *s, wentry e);
const wentry *wsnap_find(const wsnap *s, const char *rel);
void wsnap_free(wsnap *s);

// watch_ignored reports whether the patterns (a .gitignore's lines)
// ignore rel.
bool watch_ignored(const watch_list_string *patterns, const char *rel, bool dir);

// A watch: the platform's notifier runs on a thread of its own, calling
// watch_signal when the system says something changed (overflow: it
// dropped changes), and watch_wait between, which looks again once the
// changes settle.
typedef struct watcher watcher;

// The platform's.
typedef struct wnative wnative;
// watch_scan is the snapshot of root (a directory, or a file as "."),
// with recursive the directories under it too: false when root isn't there.
bool watch_scan(watcher *w, wsnap *out);
// watch_native_start makes the notifier and its thread, which runs
// watch_run(w) (false with err set); watch_native_wake wakes it to stop.
bool watch_native_start(watcher *w, asuka_error *err);
void watch_native_wake(watcher *w);
void watch_native_free(watcher *w);

struct watcher {
	watch_Watcher base;
	char *root;
	bool root_dir;  // a directory, else a file (watched through its folder)
	bool recursive, attributes;
	watch_list_string ignore;
	wsnap snap;
	wnative *native;
	// Shared with the thread, under mu.
	void *mu;
	int refs;  // the object's, the thread's, a following call's
	bool stop, ended;
	bool dirty, overflow;
	uint64_t first, last;  // when the changes waiting began, and the last came
	watch_fn_list_Event changed;
	asuka_call *call;  // following, until it answers
	watch_list_Event *pending;  // batches that came before follow
	size_t npending;
};

// For the notifier's thread: note a change, and whether to stop.
void watch_signal(watcher *w, bool overflow);
bool watch_stopping(watcher *w);
// watch_timeout is how long the thread may wait for the system (ms; -1
// forever), and watch_settle what it calls after: it looks again when the
// changes have settled (false: the root went, the thread ends).
int watch_timeout(watcher *w);
bool watch_settle(watcher *w);
// watch_exit is the thread's last call.
void watch_exit(watcher *w);

// Platform helpers of watch.c.
uint64_t watch_now_ms(void);
void *watch_mutex_new(void);
void watch_lock(void *m);
void watch_unlock(void *m);

#endif
