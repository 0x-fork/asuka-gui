// asuka/speech on Windows: SAPI 5's voice (ISpVoice), made on the main
// thread (its apartment), speaking asynchronously; its events (an
// utterance's stream ending) signal an event a thread of the package's
// waits on, then are read on the main thread. The pitch is SAPI's XML's,
// the rate and volume the voice's. macOS runs the package's Swift.
#include "asuka_windows.h"
#include "speech.asuka.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GUID clsid_voice = {0x96749377, 0x3391, 0x11D2, {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
static const GUID iid_voice = {0x6C44DF74, 0x72B9, 0x4992, {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};
static const GUID clsid_token = {0xEF411752, 0x3736, 0x4CB4, {0x9C, 0x8C, 0x8E, 0xF4, 0xCC, 0xB5, 0x8E, 0xFE}};
static const GUID iid_token = {0x14056589, 0xE16C, 0x11D2, {0xBB, 0x90, 0x00, 0xC0, 0x4F, 0x8E, 0xE6, 0xC0}};

// ISpVoice's slots (ISpNotifySource's, ISpEventSource's, then its own),
// and ISpObjectToken's SetId.
enum {
	sp_set_notify_win32_event = 7,
	sp_get_notify_event_handle = 9,
	sp_set_interest = 10,
	sp_get_events = 11,
	sp_pause = 16,
	sp_resume = 17,
	sp_set_voice = 18,
	sp_speak = 20,
	sp_set_rate = 28,
	sp_set_volume = 30,
	token_set_id = 15,
};

enum { SPF_ASYNC = 1, SPF_PURGEBEFORESPEAK = 2, SPF_IS_XML = 8 };
enum { SPEI_END_INPUT_STREAM = 2 };
// SPFEI(SPEI_END_INPUT_STREAM) | SPFEI_FLAGCHECK.
static const ULONGLONG interest = 1ull << 2 | 1ull << 30 | 1ull << 33;

typedef struct {
	WORD id, param_type;
	ULONG stream;
	ULONGLONG offset;
	WPARAM w;
	LPARAM l;
} sp_event;

// An utterance waiting for its stream's end.
typedef struct spoken {
	asuka_call *call;
	ULONG stream;
	struct spoken *next;
} spoken;

static struct {
	void *voice;
	HANDLE event;
	spoken *waiting;
} s;

static void answer(spoken *u, bool stopped) {
	if (stopped) {
		asuka_error e = {0};
		asuka_error_declared(&e, speech_SpeechError_stopped);
		asuka_call_fail(u->call, &e);
	} else {
		speech_Synthesizer_speak_done(u->call);
	}
	free(u);
}

// events answers the utterances whose streams ended. Main thread.
static void events(void *ctx) {
	for (int i = 0; i < 64; i++) {
		sp_event ev = {0};
		ULONG got = 0;
		if (FAILED(ASUKA_SLOT(s.voice, sp_get_events, HRESULT(STDMETHODCALLTYPE *)(void *, ULONG, sp_event *, ULONG *))(s.voice, 1, &ev, &got)) || !got)
			break;
		if (ev.id == SPEI_END_INPUT_STREAM)
			for (spoken **p = &s.waiting; *p; p = &(*p)->next)
				if ((*p)->stream == ev.stream) {
					spoken *u = *p;
					*p = u->next;
					answer(u, false);
					break;
				}
		if (ev.param_type == 1 || ev.param_type == 2) asuka_com_release((void *)ev.l);  // a token's, an object's reference
		else if (ev.param_type == 3 || ev.param_type == 4) CoTaskMemFree((void *)ev.l);  // a pointer, a string
	}
}

static void waiter(void *ctx) {
	for (;;) {
		WaitForSingleObject(s.event, INFINITE);
		asuka_desktop_post(events, NULL);
	}
}

// open makes the voice the first time. Main thread.
static bool open_voice(asuka_error *err) {
	if (s.voice) return true;
	void *v = NULL;
	HRESULT hr = CoCreateInstance(&clsid_voice, NULL, CLSCTX_ALL, &iid_voice, &v);
	if (FAILED(hr) || !v) {
		asuka_win_fail(err, hr, "speech: SAPI's voice isn't there");
		return false;
	}
	ASUKA_SLOT(v, sp_set_notify_win32_event, HRESULT(STDMETHODCALLTYPE *)(void *))(v);
	ASUKA_SLOT(v, sp_set_interest, HRESULT(STDMETHODCALLTYPE *)(void *, ULONGLONG, ULONGLONG))(v, interest, interest);
	s.event = ASUKA_SLOT(v, sp_get_notify_event_handle, HANDLE(STDMETHODCALLTYPE *)(void *))(v);
	s.voice = v;
	if (!asuka_desktop_spawn(waiter, NULL)) {
		asuka_error_failed(err, "speech", 0, "speech: no thread");
		return false;
	}
	return true;
}

// ---- Voices: Speech's Voices\Tokens, the 64-bit registry view ----

#define TOKENS L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens"

static char *reg_string(const wchar_t *path, const wchar_t *name) {
	wchar_t buf[512];
	DWORD n = sizeof buf;
	if (RegGetValueW(HKEY_LOCAL_MACHINE, path, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, buf, &n) != ERROR_SUCCESS) return asuka_strdup("");
	return asuka_narrow(buf, -1);
}

// lcid_name is the BCP 47 tag of a hexadecimal LCID ("409": en-US).
static char *lcid_name(const char *hex) {
	LCID lcid = (LCID)strtoul(hex, NULL, 16);
	wchar_t buf[LOCALE_NAME_MAX_LENGTH];
	if (!lcid || !LCIDToLocaleName(lcid, buf, LOCALE_NAME_MAX_LENGTH, 0)) return asuka_strdup("");
	return asuka_narrow(buf, -1);
}

static speech_list_Voice voices(void) {
	speech_list_Voice out = {0};
	HKEY k;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TOKENS, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return out;
	size_t cap = 0;
	for (DWORD i = 0; i < 256; i++) {
		wchar_t key[256];
		DWORD n = 256;
		if (RegEnumKeyExW(k, i, key, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
		size_t len = wcslen(TOKENS) + wcslen(key) + 16;
		wchar_t *path = malloc(len * sizeof *path), *attrs = malloc(len * sizeof *attrs);
		_snwprintf(path, len, L"%ls\\%ls", TOKENS, key);
		_snwprintf(attrs, len, L"%ls\\Attributes", path);
		char *lcids = reg_string(attrs, L"Language");
		lcids[strcspn(lcids, ";")] = 0;
		if (out.len == cap) out.data = realloc(out.data, (cap = cap ? cap * 2 : 8) * sizeof *out.data);
		wchar_t *id = malloc((len + 24) * sizeof *id);
		_snwprintf(id, len + 24, L"HKEY_LOCAL_MACHINE\\%ls", path);
		out.data[out.len++] = (speech_Voice){
		    .id = asuka_narrow(id, -1), .name = reg_string(path, NULL), .language = *lcids ? lcid_name(lcids) : asuka_strdup("")};
		free(lcids);
		free(id);
		free(path);
		free(attrs);
	}
	RegCloseKey(k);
	return out;
}

// voice_for is the ID of an installed voice of a language (a BCP 47
// tag): of the language and region, else of the language; NULL if none.
static char *voice_for(const char *lang) {
	speech_list_Voice vs = voices();
	char *match = NULL;
	size_t base = strcspn(lang, "-_");
	for (size_t i = 0; i < vs.len; i++) {
		const char *l = vs.data[i].language;
		if (!_stricmp(l, lang)) {
			free(match);
			match = asuka_strdup(vs.data[i].id);
			break;
		}
		if (!match && strcspn(l, "-") == base && !_strnicmp(l, lang, base)) match = asuka_strdup(vs.data[i].id);
	}
	speech_list_Voice_free(&vs);
	return match;
}

// set_voice uses the voice of a token ID, or the default (NULL).
static bool set_voice(const char *id) {
	typedef HRESULT(STDMETHODCALLTYPE * set)(void *, void *);
	if (!id) return SUCCEEDED(ASUKA_SLOT(s.voice, sp_set_voice, set)(s.voice, NULL));
	void *t = NULL;
	if (FAILED(CoCreateInstance(&clsid_token, NULL, CLSCTX_ALL, &iid_token, &t)) || !t) return false;
	wchar_t *w = asuka_wide(id);
	bool ok = SUCCEEDED(ASUKA_SLOT(t, token_set_id, HRESULT(STDMETHODCALLTYPE *)(void *, LPCWSTR, LPCWSTR, BOOL))(t, NULL, w, FALSE)) &&
	          SUCCEEDED(ASUKA_SLOT(s.voice, sp_set_voice, set)(s.voice, t));
	free(w);
	asuka_com_release(t);
	return ok;
}

// ---- The interface ----

// stop stops speaking: each utterance waiting ends stopped. Main thread.
static void stop(void) {
	if (!s.voice) return;
	ASUKA_SLOT(s.voice, sp_speak, HRESULT(STDMETHODCALLTYPE *)(void *, LPCWSTR, DWORD, ULONG *))(s.voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);
	spoken *u = s.waiting;
	s.waiting = NULL;
	while (u) {
		spoken *n = u->next;
		answer(u, true);
		u = n;
	}
}

// The task that waits went: its utterance answers cancelled, and
// speaking stops.
static void on_cancel(void *ctx) {
	spoken *u = ctx;
	for (spoken **p = &s.waiting; *p; p = &(*p)->next)
		if (*p == u) {
			*p = u->next;
			asuka_error e = {0};
			asuka_error_cancelled(&e);
			asuka_call_fail(u->call, &e);
			free(u);
			break;
		}
	stop();
}

static void y_speak(speech_Synthesizer *self, const char *text, const speech_Options *o, asuka_call *call) {
	asuka_error e = {0};
	if (!open_voice(&e)) {
		asuka_call_fail(call, &e);
		return;
	}
	char *id = o->voice && *o->voice ? asuka_strdup(o->voice) : o->language && *o->language ? voice_for(o->language) : NULL;
	if (!id || !set_voice(id)) set_voice(NULL);
	free(id);
	double rate = o->rate > 0 ? o->rate : 1;
	// SAPI's rate is -10 to 10, 10 three times as fast.
	long r = lround(10 * log(rate) / log(3));
	ASUKA_SLOT(s.voice, sp_set_rate, HRESULT(STDMETHODCALLTYPE *)(void *, long))(s.voice, r < -10 ? -10 : r > 10 ? 10 : r);
	double vol = o->volume < 0 ? 0 : o->volume > 1 ? 1 : o->volume;
	ASUKA_SLOT(s.voice, sp_set_volume, HRESULT(STDMETHODCALLTYPE *)(void *, USHORT))(s.voice, (USHORT)lround(vol * 100));
	long pitch = 0;
	if (o->pitch > 0) {
		pitch = lround(10 * log2(o->pitch));
		pitch = pitch < -10 ? -10 : pitch > 10 ? 10 : pitch;
	}
	asuka_writer x = {0};
	char head[48];
	snprintf(head, sizeof head, "<pitch absmiddle=\"%ld\">", pitch);
	asuka_writer_put(&x, head, strlen(head));
	for (const char *t = text; *t; t++) {
		const char *esc = *t == '<' ? "&lt;" : *t == '>' ? "&gt;" : *t == '&' ? "&amp;" : *t == '"' ? "&#34;" : *t == '\'' ? "&#39;" : NULL;
		if (esc) asuka_writer_put(&x, esc, strlen(esc));
		else asuka_writer_u8(&x, (uint8_t)*t);
	}
	asuka_writer_put(&x, "</pitch>", 9);  // and the NUL
	wchar_t *w = asuka_wide((const char *)x.data);
	asuka_writer_free(&x);
	ULONG stream = 0;
	HRESULT hr = ASUKA_SLOT(s.voice, sp_speak, HRESULT(STDMETHODCALLTYPE *)(void *, LPCWSTR, DWORD, ULONG *))(s.voice, w, SPF_ASYNC | SPF_IS_XML, &stream);
	free(w);
	if (FAILED(hr)) {
		asuka_win_fail(&e, hr, "SAPI couldn't speak");
		asuka_call_fail(call, &e);
		return;
	}
	spoken *u = malloc(sizeof *u);
	*u = (spoken){call, stream, s.waiting};
	s.waiting = u;
	asuka_call_on_cancel(call, on_cancel, u);
}

static void y_stop(speech_Synthesizer *self, asuka_error *err) { stop(); }

static void y_pause(speech_Synthesizer *self, asuka_error *err) {
	if (s.voice) ASUKA_SLOT(s.voice, sp_pause, HRESULT(STDMETHODCALLTYPE *)(void *))(s.voice);
}

static void y_resume(speech_Synthesizer *self, asuka_error *err) {
	if (s.voice) ASUKA_SLOT(s.voice, sp_resume, HRESULT(STDMETHODCALLTYPE *)(void *))(s.voice);
}

static bool y_speaking(speech_Synthesizer *self, asuka_error *err) { return s.voice && s.waiting; }

static void y_voices(speech_Synthesizer *self, asuka_call *call) { speech_Synthesizer_voices_done(call, voices()); }

static void y_release(speech_Synthesizer *self) {}  // the one synthesizer lives with the app

static const speech_Synthesizer_vtable vt = {.release = y_release,
                                             .speak = y_speak,
                                             .stop = y_stop,
                                             .pause = y_pause,
                                             .resume = y_resume,
                                             .speaking = y_speaking,
                                             .voices = y_voices};

speech_Synthesizer *speech_make_Synthesizer(asuka_error *err) {
	static speech_Synthesizer synth = {&vt};
	return &synth;
}
