// platform/nowplaying on Windows: the SystemMediaTransportControls of the
// app's window (ISystemMediaTransportControlsInterop::GetForWindow, as a
// desktop app gets them; a hidden window of the package's when the app
// has none, a background app's), which the volume flyout (Windows 10),
// quick settings' media controls (Windows 11), the lock screen and the
// keyboard's media keys show and drive. Its buttons are Play, Pause,
// Stop, Next, Previous, FastForward and Rewind (the skips), and the
// timeline's position requests (seeks). The artwork is read off the main
// thread and handed over as a stream.
#include "asuka_windows.h"
#include "nowplaying.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GUID iid_interop = {0xDDB0472D, 0xC911, 0x4A1F, {0x86, 0xD9, 0xDC, 0x3D, 0x71, 0xA9, 0x5F, 0x5A}};
static const GUID iid_smtc = {0x99FA3FF4, 0x1742, 0x42A6, {0x90, 0x2E, 0x08, 0x7D, 0x41, 0xF9, 0x65, 0xEC}};
static const GUID iid_smtc2 = {0xEA98D2F6, 0x7F3C, 0x4AF2, {0xA5, 0x86, 0x72, 0x88, 0x98, 0x08, 0xEF, 0xB1}};
static const GUID iid_music2 = {0x00368462, 0x97D3, 0x44B9, {0xB0, 0x0F, 0x00, 0x8A, 0xFC, 0xEF, 0xAF, 0x18}};
static const GUID iid_timeline = {0x5125316A, 0xC3A2, 0x475B, {0x85, 0x07, 0x93, 0x53, 0x4D, 0xC8, 0x8F, 0x15}};
static const GUID iid_ras = {0x905A0FE1, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID iid_stream_ref_statics = {0x857309DC, 0x3FBF, 0x4E7D, {0x98, 0x6F, 0xEF, 0x3B, 0x1A, 0x07, 0xA9, 0x64}};
// TypedEventHandler<SystemMediaTransportControls, …ButtonPressedEventArgs>,
// and <…, PlaybackPositionChangeRequestedEventArgs>.
static const GUID iid_button_handler = {0x0557E996, 0x7B23, 0x5BAE, {0xAA, 0x81, 0xEA, 0x0D, 0x67, 0x11, 0x43, 0xA4}};
static const GUID iid_position_handler = {0x44E34F15, 0xBDC0, 0x50A7, {0xAC, 0xE4, 0x39, 0xE9, 0x1F, 0xB7, 0x53, 0xF1}};

enum {
	interop_get_for_window = 6,
	smtc_put_status = 7,
	smtc_get_updater = 8,
	smtc_put_enabled = 11,
	smtc_put_play = 13,
	smtc_put_stop = 15,
	smtc_put_pause = 17,
	smtc_put_fast_forward = 21,
	smtc_put_rewind = 23,
	smtc_put_previous = 25,
	smtc_put_next = 27,
	smtc_add_button_pressed = 32,
	smtc2_put_rate = 11,
	smtc2_update_timeline = 12,
	smtc2_add_position_requested = 13,
	updater_put_type = 7,
	updater_put_thumbnail = 11,
	updater_get_music = 12,
	updater_clear_all = 16,
	updater_update = 17,
	music_put_title = 7,
	music_put_artist = 11,
	music2_put_album = 7,
	timeline_put_start = 7,
	timeline_put_end = 9,
	timeline_put_min_seek = 11,
	timeline_put_max_seek = 13,
	timeline_put_position = 15,
	stream_ref_create = 8,
	button_args_get = 6,
	position_args_get = 6,
};

enum { closed = 0, stopped = 2, playing = 3, paused = 4 };  // MediaPlaybackStatus
enum { b_play = 0, b_pause = 1, b_stop = 2, b_fast_forward = 4, b_rewind = 5, b_next = 6, b_previous = 7 };

typedef HRESULT(STDMETHODCALLTYPE *put_int)(void *, int);
typedef HRESULT(STDMETHODCALLTYPE *put_i64)(void *, int64_t);
typedef HRESULT(STDMETHODCALLTYPE *put_ptr)(void *, void *);
typedef HRESULT(STDMETHODCALLTYPE *get_ptr)(void *, void **);
typedef HRESULT(STDMETHODCALLTYPE *put_hs)(void *, HSTRING);

static struct {
	void *c, *c2;  // ISystemMediaTransportControls, and its second version
	asuka_error failed;  // why there are none
	nowplaying_Info *info;
	char *art_src;
	asuka_bytes art;  // the artwork's bytes, once read
} s;

static void on_button(void *ctx, void *sender, void *args) {
	int b = -1;
	if (FAILED(ASUKA_SLOT(args, button_args_get, HRESULT(STDMETHODCALLTYPE *)(void *, int *))(args, &b))) return;
	static const nowplaying_Command commands[8] = {nowplaying_Command_play, nowplaying_Command_pause, nowplaying_Command_stop, nowplaying_Command_unknown,
	                                               nowplaying_Command_skipForward, nowplaying_Command_skipBackward, nowplaying_Command_next,
	                                               nowplaying_Command_previous};
	if (b >= 0 && b < 8 && commands[b] != nowplaying_Command_unknown) nowplaying_send(s.info, commands[b], 0);
}

static void on_position(void *ctx, void *sender, void *args) {
	int64_t t = 0;  // a TimeSpan, in 100 ns
	if (SUCCEEDED(ASUKA_SLOT(args, position_args_get, HRESULT(STDMETHODCALLTYPE *)(void *, int64_t *))(args, &t)))
		nowplaying_send(s.info, nowplaying_Command_seek, t / 1e7);
}

static bool hidden(void *ctx, HWND h, UINT m, WPARAM wp, LPARAM lp, LRESULT *r) { return false; }

// open gets the controls (and hooks them up) on first use. Main thread.
static bool open_controls(asuka_error *err) {
	if (s.c) return true;
	if (s.failed.status) {
		asuka_error_failed(err, "nowplaying", 0, "%s", s.failed.message);
		return false;
	}
	void *interop = NULL;
	HWND hwnd = (HWND)asuka_desktop_window(0);
	if (!hwnd) hwnd = asuka_win_window(0, L"Now Playing", WS_POPUP, 0, 0, 0, 0, NULL, hidden, NULL);
	if (FAILED(asuka_winrt_factory("Windows.Media.SystemMediaTransportControls", &iid_interop, &interop)) ||
	    FAILED(ASUKA_SLOT(interop, interop_get_for_window, HRESULT(STDMETHODCALLTYPE *)(void *, HWND, const GUID *, void **))(interop, hwnd, &iid_smtc, &s.c)) ||
	    !s.c) {
		asuka_com_release(interop);
		asuka_error_unsupported(&s.failed, "not available: no SystemMediaTransportControls");
		asuka_error_unsupported(err, "not available: no SystemMediaTransportControls");
		s.c = NULL;
		return false;
	}
	asuka_com_release(interop);
	int64_t token;
	void *h = asuka_winrt_delegate(&iid_button_handler, on_button, NULL, NULL);
	ASUKA_SLOT(s.c, smtc_add_button_pressed, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))(s.c, h, &token);
	asuka_com_release(h);
	s.c2 = asuka_com_query(s.c, &iid_smtc2);
	if (s.c2) {
		h = asuka_winrt_delegate(&iid_position_handler, on_position, NULL, NULL);
		ASUKA_SLOT(s.c2, smtc2_add_position_requested, HRESULT(STDMETHODCALLTYPE *)(void *, void *, int64_t *))(s.c2, h, &token);
		asuka_com_release(h);
	}
	return true;
}

static void put_string(void *obj, int slot, const char *v) {
	HSTRING h = asuka_hstring(v);
	ASUKA_SLOT(obj, slot, put_hs)(obj, h);
	asuka_hstring_free(h);
}

// stream_reference is a RandomAccessStreamReference of the image's bytes
// (a reference), or NULL.
static void *stream_reference(asuka_bytes img) {
	if (!img.len) return NULL;
	HMODULE shlwapi = LoadLibraryExW(L"shlwapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	HMODULE shcore = LoadLibraryExW(L"shcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	IStream *(WINAPI * mem)(const BYTE *, UINT) = NULL;
	HRESULT(WINAPI * over)(IStream *, int, const GUID *, void **) = NULL;
	if (shlwapi) *(FARPROC *)&mem = GetProcAddress(shlwapi, "SHCreateMemStream");
	if (shcore) *(FARPROC *)&over = GetProcAddress(shcore, "CreateRandomAccessStreamOverStream");
	if (!mem || !over) return NULL;
	IStream *st = mem(img.data, (UINT)img.len);
	if (!st) return NULL;
	void *ras = NULL, *statics = NULL, *ref = NULL;
	if (SUCCEEDED(over(st, 0, &iid_ras, &ras)) && ras &&
	    SUCCEEDED(asuka_winrt_factory("Windows.Storage.Streams.RandomAccessStreamReference", &iid_stream_ref_statics, &statics)))
		ASUKA_SLOT(statics, stream_ref_create, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(statics, ras, &ref);
	asuka_com_release(statics);
	asuka_com_release(ras);
	IStream_Release(st);
	return ref;
}

// show shows info on the controls.
static void show(void) {
	nowplaying_Info *i = s.info;
	void *c = s.c;
	ASUKA_SLOT(c, smtc_put_enabled, put_int)(c, 1);
	ASUKA_SLOT(c, smtc_put_status, put_int)(c, i->state == nowplaying_State_playing ? playing : i->state == nowplaying_State_stopped ? stopped : paused);
	ASUKA_SLOT(c, smtc_put_play, put_int)(c, nowplaying_has(i, nowplaying_Command_play));
	ASUKA_SLOT(c, smtc_put_pause, put_int)(c, nowplaying_has(i, nowplaying_Command_pause));
	ASUKA_SLOT(c, smtc_put_stop, put_int)(c, nowplaying_has(i, nowplaying_Command_stop));
	ASUKA_SLOT(c, smtc_put_next, put_int)(c, nowplaying_has(i, nowplaying_Command_next));
	ASUKA_SLOT(c, smtc_put_previous, put_int)(c, nowplaying_has(i, nowplaying_Command_previous));
	ASUKA_SLOT(c, smtc_put_fast_forward, put_int)(c, nowplaying_has(i, nowplaying_Command_skipForward));
	ASUKA_SLOT(c, smtc_put_rewind, put_int)(c, nowplaying_has(i, nowplaying_Command_skipBackward));
	void *updater = NULL;
	if (SUCCEEDED(ASUKA_SLOT(c, smtc_get_updater, get_ptr)(c, &updater)) && updater) {
		ASUKA_SLOT(updater, updater_put_type, put_int)(updater, 1);  // MediaPlaybackType.Music
		void *music = NULL;
		if (SUCCEEDED(ASUKA_SLOT(updater, updater_get_music, get_ptr)(updater, &music)) && music) {
			put_string(music, music_put_title, i->title);
			put_string(music, music_put_artist, i->artist);
			void *music2 = asuka_com_query(music, &iid_music2);
			if (music2) put_string(music2, music2_put_album, i->album), asuka_com_release(music2);
			asuka_com_release(music);
		}
		void *thumb = stream_reference(s.art);
		ASUKA_SLOT(updater, updater_put_thumbnail, put_ptr)(updater, thumb);
		asuka_com_release(thumb);
		ASUKA_SLOT(updater, updater_update, HRESULT(STDMETHODCALLTYPE *)(void *))(updater);
		asuka_com_release(updater);
	}
	if (!s.c2) return;
	void *tl = NULL;
	if (SUCCEEDED(asuka_winrt_activate("Windows.Media.SystemMediaTransportControlsTimelineProperties", &tl))) {
		void *t = asuka_com_query(tl, &iid_timeline);
		if (t) {
			bool seek = nowplaying_has(i, nowplaying_Command_seek) && i->duration > 0;
			ASUKA_SLOT(t, timeline_put_start, put_i64)(t, 0);
			ASUKA_SLOT(t, timeline_put_end, put_i64)(t, (int64_t)(i->duration * 1e7));
			ASUKA_SLOT(t, timeline_put_min_seek, put_i64)(t, 0);
			ASUKA_SLOT(t, timeline_put_max_seek, put_i64)(t, seek ? (int64_t)(i->duration * 1e7) : 0);
			ASUKA_SLOT(t, timeline_put_position, put_i64)(t, (int64_t)(i->position * 1e7));
			ASUKA_SLOT(s.c2, smtc2_update_timeline, put_ptr)(s.c2, t);
			asuka_com_release(t);
		}
		asuka_com_release(tl);
	}
	ASUKA_SLOT(s.c2, smtc2_put_rate, HRESULT(STDMETHODCALLTYPE *)(void *, double))(s.c2, i->rate > 0 ? i->rate : 1);
}

typedef struct {
	char *src;
	asuka_bytes data;
} reading;

static void read_main(void *p) {
	reading *r = p;
	if (s.art_src && !strcmp(s.art_src, r->src) && s.info && s.c && r->data.data) {
		free(s.art.data);
		s.art = r->data;
		r->data.data = NULL;
		show();
	}
	free(r->data.data);
	free(r->src);
	free(r);
}

// read_art reads an image's bytes: an http(s) URL's, a file's, an
// asset's. Not on the main thread.
static void read_art(void *p) {
	reading *r = p;
	asuka_error e = {0};
	char *path = asuka_desktop_local_file(r->src, "", &e);
	asuka_error_free(&e);
	if (path) {
		wchar_t *w = asuka_wide(path);
		FILE *f = _wfopen(w, L"rb");
		free(w);
		if (f) {
			asuka_writer b = {0};
			char buf[16384];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) asuka_writer_put(&b, buf, n);
			fclose(f);
			r->data = (asuka_bytes){b.data, b.len};
		}
		free(path);
	}
	asuka_desktop_post(read_main, r);
}

static void c_set(nowplaying_Center *self, const nowplaying_Info *info, asuka_error *err) {
	if (!open_controls(err)) return;
	nowplaying_free_info(s.info);
	s.info = nowplaying_copy(info);
	if (!s.art_src || strcmp(s.art_src, info->artwork)) {
		free(s.art_src);
		free(s.art.data);
		s.art = (asuka_bytes){0};
		s.art_src = asuka_strdup(info->artwork);
		if (*info->artwork) {
			reading *r = calloc(1, sizeof *r);
			r->src = asuka_strdup(info->artwork);
			if (!asuka_desktop_spawn(read_art, r)) free(r->src), free(r);
		}
	}
	show();
}

static void c_clear(nowplaying_Center *self, asuka_error *err) {
	nowplaying_free_info(s.info);
	s.info = NULL;
	free(s.art_src);
	s.art_src = NULL;
	free(s.art.data);
	s.art = (asuka_bytes){0};
	if (!s.c) return;
	void *updater = NULL;
	if (SUCCEEDED(ASUKA_SLOT(s.c, smtc_get_updater, get_ptr)(s.c, &updater)) && updater) {
		ASUKA_SLOT(updater, updater_clear_all, HRESULT(STDMETHODCALLTYPE *)(void *))(updater);
		ASUKA_SLOT(updater, updater_update, HRESULT(STDMETHODCALLTYPE *)(void *))(updater);
		asuka_com_release(updater);
	}
	ASUKA_SLOT(s.c, smtc_put_status, put_int)(s.c, closed);
	ASUKA_SLOT(s.c, smtc_put_enabled, put_int)(s.c, 0);
}

static void c_release(nowplaying_Center *self) {}  // the one center lives with the app

static const nowplaying_Center_vtable vt = {.release = c_release, .set = c_set, .clear = c_clear, .watch = nowplaying_watch};

nowplaying_Center *nowplaying_make_Center(asuka_error *err) {
	static nowplaying_Center center = {&vt};
	return &center;
}
