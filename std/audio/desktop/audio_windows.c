// asuka/audio on Windows: each sound an MCI mpegvideo device
// (DirectShow's playback), opened on its source's file (an asset, a path,
// a URL downloaded to the app's caches first, off the main thread). A
// play asks to be told when it ends (MM_MCINOTIFY, to a message window of
// the package's): looping plays again, once lets the sound go.
#include "asuka_windows.h"
#include <mmsystem.h>
#include "audio.asuka.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sound sound;

struct sound {
	audio_Sound base;
	char alias[32];
	bool open, loop, once, ended, playing, released;
	char **pending;  // what was asked before its file was open (MCI commands, notify marked by a leading '!')
	size_t npending;
	double rate, volume;
	int serial;  // its open, which a later open or close outdates
	sound *next;  // the open ones
};

static HWND notify_window;
static sound *opened;
static int aliases;

// command is cmd with @ the sound's alias, as UTF-16.
static wchar_t *command(sound *p, const char *cmd, bool notify) {
	asuka_writer w = {0};
	for (const char *q = cmd; *q; q++) {
		if (*q == '@') asuka_writer_put(&w, p->alias, strlen(p->alias));
		else asuka_writer_u8(&w, (uint8_t)*q);
	}
	if (notify) asuka_writer_put(&w, " notify", 7);
	asuka_writer_u8(&w, 0);
	wchar_t *wc = asuka_wide((const char *)w.data);
	asuka_writer_free(&w);
	return wc;
}

// mci sends a command: its answer.
static char *mci(sound *p, const char *cmd, bool notify) {
	wchar_t *wc = command(p, cmd, notify);
	wchar_t out[256] = {0};
	mciSendStringW(wc, out, 256, notify ? notify_window : NULL);
	free(wc);
	return asuka_narrow(out, -1);
}

static void mci_do(sound *p, const char *cmd, bool notify) { free(mci(p, cmd, notify)); }

static double num(sound *p, const char *cmd) {
	char *s = mci(p, cmd, false);
	double v = atof(s);
	free(s);
	return v;
}

static bool mode_playing(sound *p) {
	char *s = mci(p, "status @ mode", false);
	bool v = strcmp(s, "playing") == 0;
	free(s);
	return v;
}

// later runs a command now, or once the file is open.
static void later(sound *p, const char *cmd, bool notify) {
	if (p->open) {
		mci_do(p, cmd, notify);
		return;
	}
	p->pending = realloc(p->pending, (p->npending + 1) * sizeof *p->pending);
	p->pending[p->npending++] = asuka_sprintf("%c%s", notify ? '!' : ' ', cmd);
}

static void unlink_sound(sound *p) {
	for (sound **q = &opened; *q; q = &(*q)->next)
		if (*q == p) {
			*q = p->next;
			return;
		}
}

static void clear_pending(sound *p) {
	for (size_t i = 0; i < p->npending; i++) free(p->pending[i]);
	free(p->pending);
	p->pending = NULL;
	p->npending = 0;
}

static void s_close(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	p->serial++;
	clear_pending(p);
	if (p->open) {
		mci_do(p, "close @", false);
		p->open = false;
		unlink_sound(p);
	}
	p->playing = p->ended = false;
}

// notified follows an MCI notification: a play that ended (looping:
// played again; once: let go).
static void notified(void) {
	for (sound *p = opened, *next; p; p = next) {
		next = p->next;
		if (!p->playing || mode_playing(p)) continue;
		double pos = num(p, "status @ position"), dur = num(p, "status @ length");
		if (dur > 0 && pos < dur - 50) continue;  // paused or stopped, not at its end
		if (p->loop) {
			mci_do(p, "seek @ to start", false);
			mci_do(p, "play @", true);
		} else if (p->once) {
			s_close(&p->base, NULL);
			if (p->released) free(p);
		} else {
			p->playing = false;
			p->ended = true;
		}
	}
}

static bool on_message(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *r) {
	if (m == MM_MCINOTIFY && wp == MCI_NOTIFY_SUCCESSFUL) {
		notified();
		*r = 0;
		return true;
	}
	return false;
}

typedef struct {
	sound *p;
	int serial;
	char *source, *path;
	asuka_error err;
} opening;

static void opened_main(void *v) {
	opening *o = v;
	sound *p = o->p;
	if (p->serial == o->serial) {  // not closed, or opened again, meanwhile
		bool ok = o->path != NULL;
		if (ok) {
			char *cmd = asuka_sprintf("open \"%s\" type mpegvideo alias @", o->path);
			wchar_t *wc = command(p, cmd, false);
			ok = mciSendStringW(wc, NULL, 0, NULL) == 0;
			free(wc);
			free(cmd);
		}
		if (!ok) {
			fprintf(stderr, "asuka: audio: %s: %s\n", o->source, o->err.message ? o->err.message : "MCI couldn't open it");
			clear_pending(p);
		} else {
			p->open = true;
			p->next = opened;
			opened = p;
			mci_do(p, "set @ time format milliseconds", false);
			for (size_t i = 0; i < p->npending; i++) mci_do(p, p->pending[i] + 1, p->pending[i][0] == '!');
			clear_pending(p);
		}
	}
	asuka_error_free(&o->err);
	free(o->source);
	free(o->path);
	free(o);
}

static void find(void *v) {
	opening *o = v;
	o->path = asuka_desktop_local_file(o->source, "", &o->err);
	asuka_desktop_post(opened_main, o);
}

static void s_open(audio_Sound *self, const char *source, asuka_error *err) {
	sound *p = (sound *)self;
	s_close(self, NULL);
	opening *o = calloc(1, sizeof *o);
	o->p = p;
	o->serial = p->serial;
	o->source = asuka_strdup(source);
	if (!asuka_desktop_spawn(find, o)) find(o);
}

static void s_play(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	if (p->ended) {
		p->ended = false;
		later(p, "seek @ to start", false);
	}
	p->playing = true;
	later(p, "play @", true);
}

static void s_play_once(audio_Sound *self, asuka_error *err) {
	((sound *)self)->once = true;
	s_play(self, err);
}

static void s_pause(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	later(p, "pause @", false);
	p->playing = false;
}

static void s_seek(audio_Sound *self, double seconds, asuka_error *err) {
	sound *p = (sound *)self;
	p->ended = false;
	char *c = asuka_sprintf("seek @ to %d", seconds > 0 ? (int)(seconds * 1000) : 0);
	later(p, c, false);
	free(c);
	if (p->playing) later(p, "play @", true);
}

static void s_volume(audio_Sound *self, double v, asuka_error *err) {
	sound *p = (sound *)self;
	p->volume = v < 0 ? 0 : v > 1 ? 1 : v;
	char *c = asuka_sprintf("setaudio @ volume to %d", (int)(p->volume * 1000));
	later(p, c, false);
	free(c);
}

static void s_rate(audio_Sound *self, double r, asuka_error *err) {
	sound *p = (sound *)self;
	p->rate = r;
	char *c = asuka_sprintf("set @ speed %d", (int)((r > 0.1 ? r : 0.1) * 1000));
	later(p, c, false);
	free(c);
}

static void s_loop(audio_Sound *self, bool on, asuka_error *err) { ((sound *)self)->loop = on; }

static audio_Status s_status(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	audio_Status s = {0};
	if (!p->open) return s;
	s.position = num(p, "status @ position") / 1000;
	s.duration = num(p, "status @ length") / 1000;
	s.playing = mode_playing(p);
	s.ended = p->ended;
	return s;
}

// release is the core letting it go: it stops, unless it's playing once.
static void s_release(audio_Sound *self) {
	sound *p = (sound *)self;
	if (p->once && p->playing) {
		p->released = true;
		return;
	}
	s_close(self, NULL);
	free(p);
}

static const audio_Sound_vtable vt = {
    .release = s_release, .open = s_open, .play = s_play, .playOnce = s_play_once, .pause = s_pause, .seek = s_seek,
    .setVolume = s_volume, .setRate = s_rate, .setLoop = s_loop, .status = s_status, .close = s_close,
};

audio_Sound *audio_make_Sound(asuka_error *err) {
	if (!notify_window) notify_window = asuka_win_window(0, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, on_message, NULL);
	sound *p = calloc(1, sizeof *p);
	p->base.vt = &vt;
	p->rate = p->volume = 1;
	snprintf(p->alias, sizeof p->alias, "asukasound%d", ++aliases);
	return &p->base;
}
