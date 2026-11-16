// asuka/audio on Linux: GStreamer (which GTK's media plays with too), a
// sound a playbin whose pitch scaletempo keeps at any rate, its bus's
// messages heard on the main thread (GLib's loop, the host's). macOS runs
// the package's Swift.
#include "asuka_linux.h"
#include "audio.asuka.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	audio_Sound base;
	GstElement *pipe;
	GstBus *bus;
	gulong handlers[3];
	bool loop, once, playing, ended, prerolled, rate_due, released;
	double rate, pending_seek;  // pending_seek < 0: none
} sound;

static void s_close(audio_Sound *self, asuka_error *err);

// uri is a source as a URI: a URL, a file, an asset.
static char *uri(const char *source) {
	if (strstr(source, "://") && strncmp(source, "file://", 7) != 0) return asuka_strdup(source);
	char *path = source[0] == '/' ? asuka_strdup(source)
	             : strncmp(source, "file://", 7) == 0 ? NULL
	                                                  : asuka_sprintf("%s/%s", asuka_desktop_assets_dir(), source);
	if (!path) return asuka_strdup(source);
	char abs[PATH_MAX];
	GFile *f = G(g_file_new_for_path)(realpath(path, abs) ? abs : path);
	gchar *u = G(g_file_get_uri)(f);
	G(g_object_unref)(f);
	char *out = asuka_strdup(u);
	G(g_free)(u);
	free(path);
	return out;
}

static double position(sound *p) {
	gint64 pos = 0;
	if (p->pipe && G(gst_element_query_position)(p->pipe, GST_FORMAT_TIME, &pos) && pos > 0) return pos / 1e9;
	return 0;
}

// seek goes to a time, at the sound's rate.
static void seek(sound *p, double sec) {
	if (!p->prerolled) {
		p->pending_seek = sec;
		return;
	}
	G(gst_element_seek)(p->pipe, p->rate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET,
	                    (gint64)((sec > 0 ? sec : 0) * 1e9), GST_SEEK_TYPE_NONE, -1);
}

static void on_async_done(GstBus *bus, gpointer msg, gpointer d) {
	sound *p = d;
	if (p->prerolled) return;
	p->prerolled = true;
	if (p->pending_seek >= 0) {
		double s = p->pending_seek;
		p->pending_seek = -1;
		seek(p, s);
	} else if (p->rate_due && p->rate != 1) {
		seek(p, position(p));
	}
	p->rate_due = false;
}

static void free_sound(sound *p) {
	s_close(&p->base, NULL);
	free(p);
}

// reached_end: again (loop), let go (once), or ended.
static void on_eos(GstBus *bus, gpointer msg, gpointer d) {
	sound *p = d;
	if (p->loop) {
		seek(p, 0);
	} else if (p->once) {
		s_close(&p->base, NULL);
		if (p->released) free(p);  // kept only to finish
	} else {
		p->ended = true;
		p->playing = false;
		G(gst_element_set_state)(p->pipe, GST_STATE_PAUSED);
	}
}

static void on_error(GstBus *bus, gpointer msg, gpointer d) {
	sound *p = d;
	p->playing = false;
	p->ended = true;
	if (p->once && p->released) free_sound(p);
}

static void s_open(audio_Sound *self, const char *source, asuka_error *err) {
	sound *p = (sound *)self;
	s_close(self, NULL);
	GstElement *pipe = G(gst_element_factory_make)("playbin", NULL);
	if (!pipe) {
		asuka_error_failed(err, "audio", 0, "audio: GStreamer's playbin isn't installed (gst-plugins-base)");
		return;
	}
	G(g_object_ref_sink)(pipe);
	char *u = uri(source);
	G(g_object_set)(pipe, "uri", u, NULL);
	free(u);
	// Sound only: a video's picture goes nowhere.
	GstElement *sink = G(gst_element_factory_make)("fakesink", NULL);
	if (sink) G(g_object_set)(pipe, "video-sink", sink, NULL);
	// A voice keeps its pitch at any rate.
	gpointer f = G(gst_element_factory_find)("scaletempo");
	if (f) {
		G(gst_object_unref)(f);
		GstElement *st = G(gst_element_factory_make)("scaletempo", NULL);
		if (st) G(g_object_set)(pipe, "audio-filter", st, NULL);
	}
	p->pipe = pipe;
	p->prerolled = p->ended = p->playing = false;
	p->pending_seek = -1;
	p->bus = G(gst_element_get_bus)(pipe);
	G(gst_bus_add_signal_watch)(p->bus);
	p->handlers[0] = g_signal_connect(p->bus, "message::async-done", on_async_done, p);
	p->handlers[1] = g_signal_connect(p->bus, "message::eos", on_eos, p);
	p->handlers[2] = g_signal_connect(p->bus, "message::error", on_error, p);
	G(gst_element_set_state)(pipe, GST_STATE_PAUSED);  // preroll: the duration's known
}

static void s_play(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	if (!p->pipe) return;
	if (p->ended) {
		p->ended = false;
		seek(p, 0);
	}
	p->playing = true;
	G(gst_element_set_state)(p->pipe, GST_STATE_PLAYING);
}

static void s_play_once(audio_Sound *self, asuka_error *err) {
	((sound *)self)->once = true;
	s_play(self, err);
}

static void s_pause(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	if (!p->pipe) return;
	p->playing = false;
	G(gst_element_set_state)(p->pipe, GST_STATE_PAUSED);
}

static void s_seek(audio_Sound *self, double seconds, asuka_error *err) {
	sound *p = (sound *)self;
	if (!p->pipe) return;
	p->ended = false;
	seek(p, seconds);
}

static void s_volume(audio_Sound *self, double v, asuka_error *err) {
	sound *p = (sound *)self;
	if (p->pipe) G(g_object_set)(p->pipe, "volume", v < 0 ? 0.0 : v > 1 ? 1.0 : v, NULL);
}

static void s_rate(audio_Sound *self, double r, asuka_error *err) {
	sound *p = (sound *)self;
	p->rate = r < 0.25 ? 0.25 : r > 4 ? 4 : r;
	if (!p->pipe) return;
	if (p->prerolled) seek(p, position(p));
	else p->rate_due = true;
}

static void s_loop(audio_Sound *self, bool on, asuka_error *err) { ((sound *)self)->loop = on; }

static audio_Status s_status(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	audio_Status s = {0};
	if (!p->pipe) return s;
	s.position = position(p);
	s.playing = p->playing;
	s.ended = p->ended;
	gint64 d = 0;
	if (G(gst_element_query_duration)(p->pipe, GST_FORMAT_TIME, &d) && d > 0) s.duration = d / 1e9;
	if (s.ended && s.duration > 0) s.position = s.duration;
	return s;
}

static void s_close(audio_Sound *self, asuka_error *err) {
	sound *p = (sound *)self;
	if (!p->pipe) return;
	for (int i = 0; i < 3; i++) G(g_signal_handler_disconnect)(p->bus, p->handlers[i]);
	G(gst_bus_remove_signal_watch)(p->bus);
	G(gst_object_unref)(p->bus);
	G(gst_element_set_state)(p->pipe, GST_STATE_NULL);
	G(gst_object_unref)(p->pipe);
	p->pipe = NULL;
	p->bus = NULL;
}

// release is the core letting it go: it stops, unless it's playing once
// (then it goes as it ends).
static void s_release(audio_Sound *self) {
	sound *p = (sound *)self;
	if (p->once && p->pipe && p->playing) {
		p->released = true;
		return;
	}
	free_sound(p);
}

static const audio_Sound_vtable vt = {
    .release = s_release, .open = s_open, .play = s_play, .playOnce = s_play_once, .pause = s_pause, .seek = s_seek,
    .setVolume = s_volume, .setRate = s_rate, .setLoop = s_loop, .status = s_status, .close = s_close,
};

audio_Sound *audio_make_Sound(asuka_error *err) {
	if (!asuka_linux_gst()) {
		asuka_error_failed(err, "audio", 0, "audio: GStreamer isn't installed (libgstreamer-1.0)");
		return NULL;
	}
	sound *p = calloc(1, sizeof *p);
	p->base.vt = &vt;
	p->rate = 1;
	p->pending_seek = -1;
	return &p->base;
}
