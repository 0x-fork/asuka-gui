// hotkey.c: asuka/hotkey on Linux, two ways:
//
//   - Wayland: apps can't see keys pressed in other apps; the desktop
//     binds shortcuts for an app that asks the XDG desktop portal's
//     GlobalShortcuts (GNOME 48, KDE Plasma 6, Hyprland's portal). A
//     session binds its shortcuts once (BindShortcuts), so each change
//     closes it and binds them all in a new one; the desktop remembers the
//     keys the user picks by the shortcut's id (its keys, as the portal
//     writes them), so binding the same ones again asks nothing. The
//     desktop may ask the user to confirm (declined: "not allowed") and
//     knows apps by their desktop entry (an app it can't place: "not
//     allowed"). Presses are its Activated signal; the window OnPress
//     presents or focuses takes the focus with the press's activation
//     token, as Wayland asks. Without the portal (GNOME before 48,
//     xdg-desktop-portal-gtk alone): "not available"; so under XWayland,
//     whose grabs see only X11 windows' keys.
//   - X11: the app grabs the keys on the root window (XGrabKey, with Caps
//     Lock's and Num Lock's states too), which fails while another client
//     has them ("taken"); the presses come through GDK's xevent signal. A
//     key held down repeats as presses without releases (GDK asks for
//     detectable autorepeat) or as releases and presses at the same time:
//     neither is a press again.
//
// The module: "register" (u32 id, u8 mods, u32 key length, the key, the
// description the portal shows), "unregister" (u32 id), "next" (answered
// with the ids pressed since, u32 each, once there are some) and "stop"
// (fails the waiting "next" with "stopped").
#include "host.h"

#include <stdio.h>
#include <stdlib.h>

bool lx_is_x11(GdkDisplay *d);
bool lx_is_wayland(GdkDisplay *d);

#define SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"

// A registration: its keys, the X11 grab's, or the portal's shortcut and
// the answer waiting for its binding.
typedef struct {
	uint32_t id;
	uint8_t mods;
	char *key, *desc;
	uint8_t keycode;
	uint32_t xmods;
	bool down;             // X11: pressed, not released yet
	uint32_t released_at;  // X11: the last release's time
	char *trigger;         // the portal's: CTRL+SHIFT+space
	reply *waiting;
} hk_reg;

enum { WAY_X11 = 1, WAY_PORTAL, WAY_NONE };

static struct {
	int way;
	AK_VEC(hk_reg *) regs;
	ak_buf pressed;
	char *token;  // the portal's: the last press's activation token
	reply *waiting;
	gulong xevent;  // X11: the xevent handler
	// The portal's.
	char *session;  // the session the shortcuts are bound in
	bool busy;      // a session is being made and bound
	bool stale;     // the shortcuts changed meanwhile
	guint activated;
} HK;

// lx_hotkey_portal_only binds through the portal under X11 too (tests).
bool lx_hotkey_portal_only;

static void reg_free(hk_reg *r) {
	free(r->key);
	free(r->desc);
	free(r->trigger);
	free(r);
}

static hk_reg *reg_of(uint32_t id) {
	for (uint32_t i = 0; i < HK.regs.len; i++)
		if (HK.regs.data[i]->id == id) return HK.regs.data[i];
	return NULL;
}

static void reg_drop(hk_reg *r) {
	for (uint32_t i = 0; i < HK.regs.len; i++)
		if (HK.regs.data[i] == r) {
			memmove(HK.regs.data + i, HK.regs.data + i + 1, (HK.regs.len - i - 1) * sizeof *HK.regs.data);
			HK.regs.len--;
			return;
		}
}

static void clear_startup(void *ctx) {
	(void)ctx;
	if (gdk_wayland_display_set_startup_notification_id && lx_is_wayland(gdk_display_get_default())) gdk_wayland_display_set_startup_notification_id(gdk_display_get_default(), NULL);
}

// flush answers the waiting "next" with the presses. With the portal's
// activation token, the windows OnPress presents or focuses take the
// focus with it: it's the display's while the answer is dispatched
// (posted, as the core's flush with it), and cleared after.
static void flush(void) {
	if (!HK.waiting || !HK.pressed.len) return;
	reply *w = HK.waiting;
	HK.waiting = NULL;
	char *token = HK.token;
	HK.token = NULL;
	if (token && gdk_wayland_display_set_startup_notification_id && lx_is_wayland(gdk_display_get_default()))
		gdk_wayland_display_set_startup_notification_id(gdk_display_get_default(), token);
	reply_ok(w, HK.pressed.data, HK.pressed.len);
	HK.pressed.len = 0;
	if (token) {
		lx_post(clear_startup, NULL);
		free(token);
	}
}

static void press(hk_reg *r) {
	ak_buf_u32(&HK.pressed, r->id);
	flush();
}

// ---- X11 ----

enum {
	X_SHIFT = 1 << 0,
	X_LOCK = 1 << 1,  // Caps Lock
	X_CONTROL = 1 << 2,
	X_MOD1 = 1 << 3,  // Alt
	X_MOD2 = 1 << 4,  // Num Lock
	X_KEY_PRESS = 2,
	X_KEY_RELEASE = 3,
	X_BAD_ACCESS = 10,
};

// The lock states a grab is made in too, so it works with Caps Lock or
// Num Lock on.
static const unsigned x_locks[] = {0, X_LOCK, X_MOD2, X_LOCK | X_MOD2};

// x_keysym is a shortcut key's keysym: GDK's name for it (X's), or the
// character's (Latin-1 as itself, the rest Unicode's).
static unsigned long x_keysym(const char *key) {
	char name[64];
	lx_key_gdk_name(key, name, sizeof name);
	if (name[0] && strlen(name) > 1) {
		unsigned long ks = XStringToKeysym(name);
		if (ks) return ks;
	}
	char *l = g_utf8_strdown(key, -1);
	gunichar c = g_utf8_strlen(l, -1) == 1 ? g_utf8_get_char(l) : 0;
	g_free(l);
	if (!c) return 0;
	if ((c >= 0x20 && c <= 0x7e) || (c >= 0xa0 && c <= 0xff)) return c;
	return 0x01000000ul | c;
}

static void *xdpy(void) { return gdk_x11_display_get_xdisplay(gdk_display_get_default()); }

// on_xevent is GdkX11Display's xevent: a grabbed key's press is a press,
// its repeats aren't; true takes the event from GDK.
static gboolean on_xevent(GdkDisplay *d, void *xev, gpointer data) {
	(void)d;
	(void)data;
	int type = *(int *)xev;
	if (type != X_KEY_PRESS && type != X_KEY_RELEASE) return 0;
	// XKeyEvent (LP64): time at 56, state at 80, keycode at 84.
	const uint8_t *e = xev;
	uint64_t t64;
	uint32_t state, kc32;
	memcpy(&t64, e + 56, 8);
	memcpy(&state, e + 80, 4);
	memcpy(&kc32, e + 84, 4);
	uint32_t at = (uint32_t)t64;
	state &= ~(unsigned)(X_LOCK | X_MOD2);
	uint8_t kc = (uint8_t)kc32;
	for (uint32_t i = 0; i < HK.regs.len; i++) {
		hk_reg *r = HK.regs.data[i];
		if (r->keycode != kc) continue;
		// The release comes to the grab whichever modifiers are still down
		// (they may be let go first).
		if (type == X_KEY_RELEASE) {
			if (!r->down) continue;
			r->down = false;
			r->released_at = at;
			return 1;
		}
		if (r->xmods != (state & (X_SHIFT | X_CONTROL | X_MOD1 | 1 << 6))) continue;
		if (r->down || (at == r->released_at && at)) {
			r->down = true;
			return 1;  // a repeat
		}
		r->down = true;
		press(r);
		return 1;
	}
	return 0;
}

static int grab(unsigned kc, unsigned mods, bool on) {
	GdkDisplay *display = gdk_display_get_default();
	void *dpy = xdpy();
	unsigned long root = gdk_x11_display_get_xrootwindow(display);
	gdk_x11_display_error_trap_push(display);
	for (size_t i = 0; i < AK_LEN(x_locks); i++) {
		if (on)
			XGrabKey(dpy, (int)kc, mods | x_locks[i], root, 0, 1, 1);  // GrabModeAsync
		else
			XUngrabKey(dpy, (int)kc, mods | x_locks[i], root);
	}
	XFlush(dpy);
	return gdk_x11_display_error_trap_pop(display);  // syncs
}

// grab_hotkey grabs r's keys on the root window: "taken" if another client
// has them, "no key" if the keyboard has none.
static const char *grab_hotkey(hk_reg *r) {
	unsigned long ks = x_keysym(r->key);
	if (!ks) return "no key";
	void *dpy = xdpy();
	unsigned char kc = XKeysymToKeycode(dpy, ks);
	if (!kc) return "no key";
	unsigned mods = 0;
	if (r->mods & (1 | 8)) mods |= X_CONTROL;  // Primary, Control
	if (r->mods & 2) mods |= X_SHIFT;
	if (r->mods & 4) mods |= X_MOD1;
	// A character the key types only with Shift ("+" on a US keyboard).
	if (XKeycodeToKeysym(dpy, kc, 0) != ks && XKeycodeToKeysym(dpy, kc, 1) == ks) mods |= X_SHIFT;
	r->keycode = kc;
	r->xmods = mods;
	for (uint32_t i = 0; i < HK.regs.len; i++)
		if (HK.regs.data[i]->keycode == kc && HK.regs.data[i]->xmods == mods) return "taken";
	int code = grab(kc, mods, true);
	if (code) {
		grab(kc, mods, false);
		return code == X_BAD_ACCESS ? "taken" : "hotkey: XGrabKey failed";
	}
	if (!HK.xevent) HK.xevent = g_signal_connect(gdk_display_get_default(), "xevent", G_CALLBACK(on_xevent), NULL);
	ak_vec_push(&HK.regs, r);
	return NULL;
}

// ---- The portal ----

// xdg_trigger is a shortcut as the XDG shortcuts specification writes
// it: CTRL+SHIFT+space (the key X's keysym name); NULL: no key.
static char *xdg_trigger(const char *key, uint8_t mods) {
	char name[64];
	lx_key_gdk_name(key, name, sizeof name);
	if (!name[0]) return NULL;
	char out[128];
	snprintf(out, sizeof out, "%s%s%s%s", mods & (1 | 8) ? "CTRL+" : "", mods & 4 ? "ALT+" : "", mods & 2 ? "SHIFT+" : "", name);
	return str_dup(ak_cstr(out));
}

static int by_id(const void *a, const void *b) {
	uint32_t x = (*(hk_reg *const *)a)->id, y = (*(hk_reg *const *)b)->id;
	return x < y ? -1 : x > y;
}

// shortcuts are the shortcuts to bind, as the portal takes them
// (a(sa{sv})): each one's id its keys (the desktop remembers the keys the
// user picks by it), with its description and preferred trigger.
static GVariant *shortcuts(void) {
	hk_reg **sorted = malloc((HK.regs.len ? HK.regs.len : 1) * sizeof *sorted);
	if (!sorted) abort();
	memcpy(sorted, HK.regs.data, HK.regs.len * sizeof *sorted);
	qsort(sorted, HK.regs.len, sizeof *sorted, by_id);
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a(sa{sv})"));
	for (uint32_t i = 0; i < HK.regs.len; i++) {
		hk_reg *r = sorted[i];
		GVariantBuilder o;
		g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
		g_variant_builder_add(&o, "{sv}", "description", g_variant_new_string(str_empty(r->desc) ? r->trigger : r->desc));
		g_variant_builder_add(&o, "{sv}", "preferred_trigger", g_variant_new_string(r->trigger));
		g_variant_builder_add(&b, "(sa{sv})", r->trigger, &o);
	}
	free(sorted);
	return g_variant_builder_end(&b);
}

// answer answers the registrations waiting for a binding; failed, they're
// dropped. It returns how many it answered.
static int answer(const char *err) {
	int n = 0;
	for (uint32_t i = 0; i < HK.regs.len;) {
		hk_reg *r = HK.regs.data[i];
		if (!r->waiting) {
			i++;
			continue;
		}
		reply *w = r->waiting;
		r->waiting = NULL;
		if (err) {
			reg_drop(r);
			reg_free(r);
			reply_err(w, "%s", err);
		} else {
			reply_ok(w, NULL, 0);
			i++;
		}
		n++;
	}
	return n;
}

static void sync_hotkeys(void);

// resync binds again if the shortcuts changed while a session was on its
// way.
static void resync(void) {
	if (HK.stale) {
		HK.stale = false;
		sync_hotkeys();
	}
}

static void close_session(const char *session) {
	GDBusConnection *bus = lx_session_bus();
	if (bus) lx_dbus_call(bus, LX_PORTAL, session, "org.freedesktop.portal.Session", "Close", NULL, -1, NULL, NULL);
}

static GVariant *bind_params(const char *token, void *ctx) {
	GVariantBuilder o;
	g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string(token));
	return g_variant_new("(o@a(sa{sv})sa{sv})", (const char *)ctx, shortcuts(), "", &o);
}

static void bound(void *ctx, uint32_t code, GVariant *results, const char *err) {
	(void)results;
	char *session = ctx;
	HK.busy = false;
	const char *why = err ? err : code == 1 ? "not allowed" : code ? "hotkey: the desktop didn't bind the shortcuts" : NULL;  // 1: the user declined them
	if (why && HK.session && !strcmp(HK.session, session)) {
		close_session(session);
		free(HK.session);
		HK.session = NULL;
	}
	// Failed, the new ones are dropped and the ones bound before are bound
	// again, in a session of their own (the desktop asks nothing for keys
	// it has bound for the app).
	if (answer(why) > 0 && why && HK.regs.len) HK.stale = true;
	free(session);
	resync();
}

static GVariant *session_params(const char *token, void *ctx) {
	(void)ctx;
	// The session is named after the app: KDE names the shortcuts of an
	// app it has no desktop entry for after it, and keeps the keys the
	// user picks for them by it.
	char name[256] = "asuka_";
	size_t k = strlen(name);
	for (const char *p = S(H.app_id); *p && k < sizeof name - 1; p++) name[k++] = ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) ? *p : '_';
	name[k] = 0;
	GVariantBuilder o;
	g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string(token));
	g_variant_builder_add(&o, "{sv}", "session_handle_token", g_variant_new_string(name));
	// KDE Plasma 5 binds the shortcuts given here, as the portal's drafts
	// had it (portals before 1.17 pass them on).
	g_variant_builder_add(&o, "{sv}", "shortcuts", shortcuts());
	return g_variant_new("(a{sv})", &o);
}

static void session_made(void *ctx, uint32_t code, GVariant *results, const char *err) {
	(void)ctx;
	const char *session = NULL;
	if (results) g_variant_lookup(results, "session_handle", "&s", &session);
	if (err || code || !session || !*session) {
		HK.busy = false;
		const char *why = err && strstr(err, "NotAllowed") ? "not allowed"  // the app has no desktop entry the desktop knows
		                  : err                            ? err
		                  : code == 1                      ? "not allowed"
		                                                   : "hotkey: the desktop portal made no session";
		answer(why);
		resync();
		return;
	}
	if (!HK.regs.len || HK.stale) {
		HK.busy = false;
		close_session(session);
		resync();
		return;
	}
	free(HK.session);
	HK.session = str_dup(ak_cstr(session));
	lx_portal_request(SHORTCUTS, "BindShortcuts", bind_params, bound, str_dup(ak_cstr(session)));
}

// sync_hotkeys binds the shortcuts in a new session, closing the old: a
// session binds once. While one is on its way, it does so after.
static void sync_hotkeys(void) {
	if (HK.busy) {
		HK.stale = true;
		return;
	}
	if (!lx_session_bus()) {
		answer("no session bus");
		return;
	}
	if (HK.session) {
		close_session(HK.session);
		free(HK.session);
		HK.session = NULL;
	}
	if (!HK.regs.len) return;
	HK.busy = true;
	HK.stale = false;
	lx_portal_request(SHORTCUTS, "CreateSession", session_params, session_made, NULL);
}

// on_activated is the portal's Activated: (session, shortcut id,
// timestamp, options).
static void on_activated(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
	(void)c;
	(void)sender;
	(void)path;
	(void)iface;
	(void)signal;
	(void)data;
	if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(ostua{sv})")) && !g_variant_is_of_type(params, G_VARIANT_TYPE("(ossta{sv})"))) return;
	GVariant *s = g_variant_get_child_value(params, 0), *i = g_variant_get_child_value(params, 1), *opts = g_variant_get_child_value(params, 3);
	const char *session = g_variant_get_string(s, NULL), *id = g_variant_get_string(i, NULL);
	if (HK.session && !strcmp(session, HK.session))
		for (uint32_t k = 0; k < HK.regs.len; k++) {
			hk_reg *r = HK.regs.data[k];
			if (r->waiting || strcmp(r->trigger, id)) continue;
			const char *token = NULL;
			if (g_variant_lookup(opts, "activation_token", "&s", &token)) {
				free(HK.token);
				HK.token = str_dup(ak_cstr(token));
			}
			press(r);
			break;
		}
	g_variant_unref(s);
	g_variant_unref(i);
	g_variant_unref(opts);
}

// bind_hotkey adds r to the shortcuts the portal binds; it's answered once
// they're bound.
static void bind_hotkey(hk_reg *r, reply *w) {
	r->trigger = xdg_trigger(r->key, r->mods);
	if (!r->trigger) {
		reply_err(w, "no key");
		reg_free(r);
		return;
	}
	for (uint32_t i = 0; i < HK.regs.len; i++)
		if (!strcmp(HK.regs.data[i]->trigger, r->trigger)) {
			reply_err(w, "taken");
			reg_free(r);
			return;
		}
	GDBusConnection *bus = lx_session_bus();
	if (!bus) {
		reply_err(w, "no session bus");
		reg_free(r);
		return;
	}
	if (!HK.activated) HK.activated = g_dbus_connection_signal_subscribe(bus, LX_PORTAL, SHORTCUTS, "Activated", LX_PORTAL_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_activated, NULL, NULL);
	r->waiting = w;
	ak_vec_push(&HK.regs, r);
	sync_hotkeys();
}

// ---- The module ----

typedef struct {
	hk_reg *r;
	reply *w;
} pending_reg;

static void registered(pending_reg *p) {
	switch (HK.way) {
	case WAY_X11: {
		const char *err = grab_hotkey(p->r);
		if (err) {
			reg_free(p->r);
			reply_err(p->w, "%s", err);
		} else {
			reply_ok(p->w, NULL, 0);
		}
		break;
	}
	case WAY_PORTAL: bind_hotkey(p->r, p->w); break;
	default:
		reg_free(p->r);
		reply_err(p->w, "not available");
	}
	free(p);
}

typedef struct {
	AK_VEC(pending_reg *) list;
	bool asking;
} way_wait;
static way_wait choosing;

static void version_answer(void *ctx, GVariant *rep, const char *err) {
	(void)ctx;
	uint32_t v = 0;
	if (!err && rep) {
		GVariant *x = NULL;
		g_variant_get(rep, "(v)", &x);
		if (x && g_variant_is_of_type(x, G_VARIANT_TYPE("u"))) v = g_variant_get_uint32(x);
		if (x) g_variant_unref(x);
	}
	if (!HK.way) HK.way = v ? WAY_PORTAL : WAY_NONE;
	choosing.asking = false;
	for (uint32_t i = 0; i < choosing.list.len; i++) registered(choosing.list.data[i]);
	choosing.list.len = 0;
}

// choose_way chooses, once, how shortcuts are registered: on a Wayland
// session the portal (none: not available), on X11 the app's grabs.
static void choose_way(pending_reg *p) {
	if (HK.way) {
		registered(p);
		return;
	}
	GdkDisplay *d = gdk_display_get_default();
	const char *wd = getenv("WAYLAND_DISPLAY"), *st = getenv("XDG_SESSION_TYPE");
	bool wayland = lx_is_wayland(d) || (wd && *wd) || (st && !strcmp(st, "wayland"));
	if (lx_is_x11(d) && !wayland && !lx_hotkey_portal_only && XGrabKey) {
		HK.way = WAY_X11;
		registered(p);
		return;
	}
	ak_vec_push(&choosing.list, p);
	if (choosing.asking) return;
	choosing.asking = true;
	GDBusConnection *bus = lx_session_bus();
	if (!bus) {
		version_answer(NULL, NULL, "no session bus");
		return;
	}
	lx_dbus_call(bus, LX_PORTAL, LX_PORTAL_PATH, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", SHORTCUTS, "version"), 5000, version_answer, NULL);
}

static void hk_register(ak_span args, reply *w) {
	ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
	uint32_t id = ak_read_u32(&rd);
	uint8_t mods = ak_read_u8(&rd);
	uint32_t n;
	const uint8_t *key = ak_read_bytes(&rd, &n);
	if (rd.err || !n) {
		reply_err(w, "hotkey.register: bad arguments");
		return;
	}
	hk_reg *r = calloc(1, sizeof *r);
	if (!r) abort();
	r->id = id;
	r->mods = mods;
	r->key = str_dup(ak_strn((const char *)key, n));
	r->desc = str_dup(ak_strn((const char *)rd.p + rd.off, rd.len - rd.off));
	pending_reg *p = malloc(sizeof *p);
	if (!p) abort();
	*p = (pending_reg){r, w};
	choose_way(p);
}

static void hk_unregister(ak_span args, reply *w) {
	if (args.len >= 4) {
		ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
		hk_reg *r = reg_of(ak_read_u32(&rd));
		if (r) {
			reg_drop(r);
			if (HK.way == WAY_X11) {
				grab(r->keycode, r->xmods, false);
			} else if (HK.way == WAY_PORTAL) {
				if (r->waiting) reply_err(r->waiting, "unregistered");
				r->waiting = NULL;
				sync_hotkeys();
			}
			reg_free(r);
		}
	}
	reply_ok(w, NULL, 0);
}

static void hk_next(ak_span args, reply *w) {
	(void)args;
	if (HK.waiting) reply_err(HK.waiting, "stopped");  // one poll at a time
	HK.waiting = w;
	flush();
}

static void hk_stop(ak_span args, reply *w) {
	(void)args;
	reply *old = HK.waiting;
	HK.waiting = NULL;
	HK.pressed.len = 0;
	free(HK.token);
	HK.token = NULL;
	if (old) reply_err(old, "stopped");
	reply_ok(w, NULL, 0);
}

static const method_t hk_methods[] = {
	{"hotkey", "register", hk_register},
	{"hotkey", "unregister", hk_unregister},
	{"hotkey", "next", hk_next},
	{"hotkey", "stop", hk_stop},
};

static void hk_modules(void) { lx_register_methods(hk_methods, AK_LEN(hk_methods)); }

const area lx_area_hotkey = {
	.name = "hotkey",
	.modules = hk_modules,
};
