// hotkey.c: platform/hotkey on Windows: RegisterHotKey with a message window
// of the modules', which gets WM_HOTKEY for a press whichever app has the
// keyboard. It fails while another app has the keys ("taken"); MOD_NOREPEAT
// keeps a key held down from pressing it again. The press gives the app the
// right to take the foreground, so OnPress can bring its window up. Primary
// and Control are both Ctrl; a character is the key that types it on the
// keyboard's layout (with Shift if it takes it), as the menu bar's
// accelerators are.
#include "modules.h"

typedef struct {
	int win;  // the ID Windows has it by (an app's are 0 to 0xBFFF)
	uint32_t id;  // the package's
} hotkey;

static struct {
	HWND hwnd;
	hotkey *keys;
	uint32_t n;
	int next_win;
	ak_buf pressed;  // u32 ids "next" hasn't answered with yet
	wh_reply *waiting;
} hk;

// flush answers the waiting "next" with the presses.
static void flush(void) {
	if (!hk.waiting || !hk.pressed.len) return;
	wh_reply *r = hk.waiting;
	hk.waiting = NULL;
	wh_reply_ok(r, hk.pressed.data, hk.pressed.len);
	ak_buf_reset(&hk.pressed);
}

static LRESULT hotkey_message(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled) {
	(void)hwnd, (void)lp;
	if (m != WM_HOTKEY) {
		*handled = false;
		return 0;
	}
	*handled = true;
	for (uint32_t i = 0; i < hk.n; i++)
		if (hk.keys[i].win == (int)wp) {
			ak_buf_u32(&hk.pressed, hk.keys[i].id);
			flush();
			break;
		}
	return 0;
}

static HWND window(void) {
	if (!hk.hwnd) hk.hwnd = wh_module_window(true, hotkey_message);
	return hk.hwnd;
}

static bool used(int win) {
	for (uint32_t i = 0; i < hk.n; i++)
		if (hk.keys[i].win == win) return true;
	return false;
}

// register: u32 id, u8 mods, u32 key length, the key, the description
// (which Windows doesn't show).
static void m_register(wh_reply *r, const uint8_t *a, size_t n) {
	if (n < 9 || wh_u32(a + 5) > n - 9) {
		wh_reply_fail(r, "hotkey.register: bad arguments");
		return;
	}
	uint32_t id = wh_u32(a);
	uint8_t mods = a[4];
	uint32_t kl = wh_u32(a + 5);
	char key[64];
	snprintf(key, sizeof key, "%.*s", (int)(kl < 63 ? kl : 63), (const char *)a + 9);
	ACCEL acc;
	if (!wh_accelerator(key, mods, &acc) || !acc.key) {
		wh_reply_fail(r, "no key");
		return;
	}
	UINT wmods = MOD_NOREPEAT;
	if (acc.fVirt & FCONTROL) wmods |= MOD_CONTROL;
	if (acc.fVirt & FSHIFT) wmods |= MOD_SHIFT;
	if (acc.fVirt & FALT) wmods |= MOD_ALT;
	HWND w = window();
	int win = -1;
	for (int i = 0; i < 0xC000; i++) {
		hk.next_win = (hk.next_win + 1) % 0xC000;
		if (!used(hk.next_win)) {
			win = hk.next_win;
			break;
		}
	}
	if (win < 0) {
		wh_reply_fail(r, "hotkey: too many registered");
		return;
	}
	if (!RegisterHotKey(w, win, wmods, acc.key)) {
		DWORD e = GetLastError();
		if (e == ERROR_HOTKEY_ALREADY_REGISTERED) wh_reply_fail(r, "taken");
		else {
			char *msg = wh_win_error(e);
			wh_reply_fail(r, "hotkey: RegisterHotKey: %s", msg);
			free(msg);
		}
		return;
	}
	hk.keys = wh_realloc(hk.keys, (hk.n + 1) * sizeof *hk.keys);
	hk.keys[hk.n++] = (hotkey){win, id};
	wh_reply_nil(r);
}

// unregister: u32 id.
static void m_unregister(wh_reply *r, const uint8_t *a, size_t n) {
	if (n >= 4) {
		uint32_t id = wh_u32(a);
		for (uint32_t i = 0; i < hk.n;) {
			if (hk.keys[i].id == id) {
				UnregisterHotKey(hk.hwnd, hk.keys[i].win);
				hk.keys[i] = hk.keys[--hk.n];
			} else i++;
		}
	}
	wh_reply_nil(r);
}

// next is answered with the ids pressed since, u32 each, once there are
// some.
static void m_next(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	if (hk.waiting) wh_reply_fail(hk.waiting, "stopped");  // one poll at a time
	hk.waiting = r;
	flush();
}

static void m_stop(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	wh_reply *w = hk.waiting;
	hk.waiting = NULL;
	ak_buf_reset(&hk.pressed);
	if (w) wh_reply_fail(w, "stopped");
	wh_reply_nil(r);
}

static const wh_method_def hotkey_methods[] = {
	{"register", m_register}, {"unregister", m_unregister}, {"next", m_next}, {"stop", m_stop}, {NULL, NULL},
};

const wh_module_def wh_hotkey_module = {"hotkey", hotkey_methods};
