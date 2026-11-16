// asuka/nowplaying on Windows (the SystemMediaTransportControls) and
// Linux (an MPRIS player on the session bus); macOS runs the package's
// Swift. What both share: the watchers, which hear the commands. Its
// methods come on the main thread, where the commands are heard and sent
// on.
#include "nowplaying.h"

#include <stdlib.h>

typedef struct watcher {
	nowplaying_fn_Command_float command;
	asuka_call *call;
	struct watcher *next;
} watcher;

static watcher *watchers;

bool nowplaying_has(const nowplaying_Info *info, nowplaying_Command c) { return info && (info->commands & ((int64_t)1 << c)) != 0; }

void nowplaying_send(const nowplaying_Info *info, nowplaying_Command c, double v) {
	if (!nowplaying_has(info, c)) return;
	if ((c == nowplaying_Command_skipForward || c == nowplaying_Command_skipBackward) && v == 0) v = info->skip;
	for (watcher *w = watchers; w; w = w->next) nowplaying_fn_Command_float_call(w->command, c, v);
}

nowplaying_Info *nowplaying_copy(const nowplaying_Info *i) {
	nowplaying_Info *c = malloc(sizeof *c);
	*c = *i;
	c->title = asuka_strdup(i->title);
	c->artist = asuka_strdup(i->artist);
	c->album = asuka_strdup(i->album);
	c->artwork = asuka_strdup(i->artwork);
	return c;
}

void nowplaying_free_info(nowplaying_Info *i) {
	if (!i) return;
	nowplaying_Info_free(i);
	free(i);
}

static void stop(void *p) {
	watcher *w = p;
	for (watcher **q = &watchers; *q; q = &(*q)->next)
		if (*q == w) {
			*q = w->next;
			break;
		}
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	free(w);
}

void nowplaying_watch(nowplaying_Center *self, nowplaying_fn_Command_float command, asuka_call *call) {
	watcher *w = malloc(sizeof *w);
	*w = (watcher){command, call, watchers};
	watchers = w;
	asuka_call_on_cancel(call, stop, w);
}
