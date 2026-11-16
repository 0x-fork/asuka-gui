// tray.c: asuka/tray on Linux. GTK 4 has no tray, and the GTK 3
// appindicator libraries can't load in a GTK 4 process, so the host
// speaks the protocol they speak itself, through GDBus:
//
//   - Each icon is a StatusNotifierItem (org.kde.StatusNotifierItem) on a
//     session bus connection of its own, at /StatusNotifierItem, with its
//     menu a com.canonical.dbusmenu object at /MenuBar on the same
//     connection; it's registered with org.kde.StatusNotifierWatcher
//     (again when the watcher's name gets a new owner, as the panel
//     restarts). Removing it closes its connection: its unique name goes,
//     which every watcher follows (an object going off a connection that
//     stays isn't seen by all of them).
//   - Its icon is IconName, the icon theme's name for a Symbol (with
//     IconThemePath the host's directory of Material symbols), or
//     IconPixmap, an Image's pixels (ARGB32, network order) at its size
//     and smaller ones; a Template image is white while libadwaita's
//     style is dark, black otherwise. ToolTip is its tooltip, and
//     XAyatanaLabel its Title (GNOME's AppIndicator extension shows it).
//   - ItemIsMenu is true without OnClick, so a click opens the menu; with
//     it, Activate is the click, and the panel opens the menu on a
//     secondary click (ContextMenu).
//   - The Dock menu is the launcher's quicklist: a dbusmenu object on the
//     app's session bus connection, named by com.canonical.Unity.
//     LauncherEntry's Update signal for the app's desktop file
//     (application://<id>.desktop), which Ubuntu's Dock, Dash to Dock,
//     Plank and KDE's task manager follow.
//
// The module's methods are std/tray's: supported, set, remove, dock,
// next (a long poll of clicks and chosen items: u32 icon, u8 kind, u32
// item), stop.
#include "host.h"

#include <stdio.h>
#include <stdlib.h>

const char *lx_icon_dir(void);

#define SNI_PATH "/StatusNotifierItem"
#define SNI_MENU_PATH "/MenuBar"
#define SNI_IFACE "org.kde.StatusNotifierItem"
#define MENU_IFACE "com.canonical.dbusmenu"
#define WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define QUICKLIST_PATH "/org/asuka/Quicklist"
#define LAUNCHER_PATH "/org/asuka/LauncherEntry"
#define LAUNCHER_IFACE "com.canonical.Unity.LauncherEntry"

static const char sni_xml[] =
	"<node><interface name=\"org.kde.StatusNotifierItem\">"
	"<property name=\"Category\" type=\"s\" access=\"read\"/>"
	"<property name=\"Id\" type=\"s\" access=\"read\"/>"
	"<property name=\"Title\" type=\"s\" access=\"read\"/>"
	"<property name=\"Status\" type=\"s\" access=\"read\"/>"
	"<property name=\"WindowId\" type=\"i\" access=\"read\"/>"
	"<property name=\"IconThemePath\" type=\"s\" access=\"read\"/>"
	"<property name=\"IconName\" type=\"s\" access=\"read\"/>"
	"<property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"<property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
	"<property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"<property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
	"<property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"<property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>"
	"<property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
	"<property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
	"<property name=\"Menu\" type=\"o\" access=\"read\"/>"
	"<property name=\"XAyatanaLabel\" type=\"s\" access=\"read\"/>"
	"<property name=\"XAyatanaLabelGuide\" type=\"s\" access=\"read\"/>"
	"<method name=\"ContextMenu\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
	"<method name=\"Activate\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
	"<method name=\"SecondaryActivate\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
	"<method name=\"Scroll\"><arg name=\"delta\" type=\"i\" direction=\"in\"/><arg name=\"orientation\" type=\"s\" direction=\"in\"/></method>"
	"<method name=\"ProvideXdgActivationToken\"><arg name=\"token\" type=\"s\" direction=\"in\"/></method>"
	"<signal name=\"NewTitle\"/><signal name=\"NewIcon\"/><signal name=\"NewAttentionIcon\"/><signal name=\"NewOverlayIcon\"/>"
	"<signal name=\"NewToolTip\"/><signal name=\"NewStatus\"><arg type=\"s\"/></signal><signal name=\"NewMenu\"/>"
	"<signal name=\"XAyatanaNewLabel\"><arg type=\"s\"/><arg type=\"s\"/></signal>"
	"</interface></node>";

static const char menu_xml[] =
	"<node><interface name=\"com.canonical.dbusmenu\">"
	"<property name=\"Version\" type=\"u\" access=\"read\"/>"
	"<property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
	"<property name=\"Status\" type=\"s\" access=\"read\"/>"
	"<property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
	"<method name=\"GetLayout\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/><arg type=\"(ia{sv}av)\" direction=\"out\"/></method>"
	"<method name=\"GetGroupProperties\"><arg type=\"ai\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/><arg type=\"a(ia{sv})\" direction=\"out\"/></method>"
	"<method name=\"GetProperty\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>"
	"<method name=\"Event\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>"
	"<method name=\"EventGroup\"><arg type=\"a(isvu)\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/></method>"
	"<method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>"
	"<method name=\"AboutToShowGroup\"><arg type=\"ai\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/><arg type=\"ai\" direction=\"out\"/></method>"
	"<signal name=\"ItemsPropertiesUpdated\"><arg type=\"a(ia{sv})\"/><arg type=\"a(ias)\"/></signal>"
	"<signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>"
	"<signal name=\"ItemActivationRequested\"><arg type=\"i\"/><arg type=\"u\"/></signal>"
	"</interface></node>";

// What "next" reports.
enum { TRAY_CLICK = 1, TRAY_ITEM = 2 };

// ---- dbusmenu ----

// A menu exported with com.canonical.dbusmenu, as libdbusmenu does: items
// by ID (0 the root, an item's number plus one), each with its properties
// and children. A destructive item is set apart by separators, as GTK's
// menus set it apart in a section.
typedef struct {
	int32_t id;
	int32_t entry;  // its item's place in entries; -1: the root, or a separator of the host's
	AK_VEC(int32_t) kids;
} dm_node;

typedef struct {
	GDBusConnection *conn;
	const char *path;
	guint reg;
	uint32_t revision;
	ak_buf bytes;  // the menu as sent, which entries point into
	ak_menu_entry_vec entries;
	AK_VEC(dm_node) nodes;
	uint32_t icon;  // what "next" says chose an item: the icon, 0 the Dock menu
} dbus_menu;

// The host's separators are numbered past any item's.
#define DM_SEPARATOR (1 << 24)

static dm_node *dm_find(dbus_menu *m, int32_t id) {
	for (uint32_t i = 0; i < m->nodes.len; i++)
		if (m->nodes.data[i].id == id) return &m->nodes.data[i];
	return NULL;
}

static bool dm_is_separator(dbus_menu *m, int32_t id) {
	dm_node *n = dm_find(m, id);
	return n && (n->entry < 0 ? n->id >= DM_SEPARATOR : (m->entries.data[n->entry].flags & AK_MENU_SEPARATOR) != 0);
}

static uint32_t dm_add_node(dbus_menu *m, int32_t id, int32_t entry) {
	dm_node n = {id, entry, {0}};
	ak_vec_push(&m->nodes, n);
	return m->nodes.len - 1;
}

// dm_add adds the items whose parent is parent (UINT32_MAX: the menu's
// own) to node at, returning nothing: its kids are filled in place.
static void dm_add(dbus_menu *m, uint32_t parent, uint32_t at, int32_t *seps) {
	AK_VEC(uint32_t) items = {0};
	for (uint32_t i = 0; i < m->entries.len; i++)
		if (m->entries.data[i].parent == parent) ak_vec_push(&items, i);
	for (uint32_t k = 0; k < items.len; k++) {
		const ak_menu_entry *it = &m->entries.data[items.data[k]];
		int32_t id = (int32_t)it->index + 1;
		bool destructive = (it->flags & AK_MENU_DESTRUCTIVE) && !(it->flags & AK_MENU_SEPARATOR);
		dm_node *p = &m->nodes.data[at];
		if (destructive && p->kids.len && !dm_is_separator(m, p->kids.data[p->kids.len - 1])) {
			int32_t s = ++*seps;
			dm_add_node(m, s, -1);
			ak_vec_push(&m->nodes.data[at].kids, s);
		}
		uint32_t me = dm_add_node(m, id, (int32_t)items.data[k]);
		ak_vec_push(&m->nodes.data[at].kids, id);
		if (it->flags & AK_MENU_SUBMENU) dm_add(m, it->index, me, seps);
		if (destructive && k + 1 < items.len && !(m->entries.data[items.data[k + 1]].flags & AK_MENU_SEPARATOR)) {
			int32_t s = ++*seps;
			dm_add_node(m, s, -1);
			ak_vec_push(&m->nodes.data[at].kids, s);
		}
	}
	ak_vec_free(&items);
}

static void dm_clear(dbus_menu *m) {
	for (uint32_t i = 0; i < m->nodes.len; i++) ak_vec_free(&m->nodes.data[i].kids);
	m->nodes.len = 0;
}

// dm_set takes the menu's items, and tells the panel the layout changed.
static void dm_set(dbus_menu *m, ak_span menu) {
	dm_clear(m);
	ak_buf_reset(&m->bytes);
	ak_buf_put(&m->bytes, menu.p, menu.len);
	m->entries.len = 0;
	ak_decode_menu(m->bytes.data, m->bytes.len, &m->entries);
	int32_t seps = DM_SEPARATOR;
	dm_add_node(m, 0, -1);
	dm_add(m, UINT32_MAX, 0, &seps);
	m->revision++;
	if (m->reg) lx_dbus_emit(m->conn, m->path, MENU_IFACE, "LayoutUpdated", g_variant_new("(ui)", m->revision, 0));
}

static bool wanted(const char *const *names, const char *key) {
	if (!names || !names[0]) return true;
	for (const char *const *p = names; *p; p++)
		if (!strcmp(*p, key)) return true;
	return false;
}

static void put(GVariantBuilder *b, const char *const *names, const char *key, GVariant *v) {
	if (wanted(names, key))
		g_variant_builder_add(b, "{sv}", key, v);
	else
		g_variant_unref(g_variant_ref_sink(v));  // floating, not taken
}

static const char *entry_title(const ak_menu_entry *it, char *buf, size_t n) {
	if (!it->title.len && it->role == AK_MENU_ROLE_QUIT) return lx_word_app("quit", H.app_name);
	if (!it->title.len && it->role == AK_MENU_ROLE_CLOSE) return lx_word("close");
	snprintf(buf, n, "%.*s", (int)it->title.len, it->title.p);
	return buf;
}

// dm_props are an item's properties (only those named, if any are).
static GVariant *dm_props(dbus_menu *m, int32_t id, const char *const *names) {
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	dm_node *n = dm_find(m, id);
	if (!n) {
	} else if (id == 0) {
		put(&b, names, "children-display", g_variant_new_string("submenu"));
	} else if (n->entry < 0 || (m->entries.data[n->entry].flags & AK_MENU_SEPARATOR)) {
		put(&b, names, "type", g_variant_new_string("separator"));
	} else {
		const ak_menu_entry *it = &m->entries.data[n->entry];
		char buf[512];
		const char *title = entry_title(it, buf, sizeof buf);
		ak_buf label = {0};
		for (const char *p = title; *p; p++) {
			if (*p == '_') ak_buf_u8(&label, '_');  // dbusmenu's mnemonics
			ak_buf_u8(&label, (uint8_t)*p);
		}
		put(&b, names, "label", g_variant_new_string(lx_buf_cstr(&label)));
		ak_buf_free(&label);
		put(&b, names, "enabled", g_variant_new_boolean(!(it->flags & AK_MENU_DISABLED)));
		put(&b, names, "visible", g_variant_new_boolean(1));
		if (it->flags & AK_MENU_CHECKED) {
			put(&b, names, "toggle-type", g_variant_new_string("checkmark"));
			put(&b, names, "toggle-state", g_variant_new_int32(1));
		}
		if (it->flags & AK_MENU_SUBMENU) put(&b, names, "children-display", g_variant_new_string("submenu"));
	}
	return g_variant_builder_end(&b);
}

// dm_layout is the (ia{sv}av) of an item and its children, depth levels
// down (-1: all).
static GVariant *dm_layout(dbus_menu *m, int32_t id, int32_t depth, const char *const *names) {
	GVariantBuilder kids;
	g_variant_builder_init(&kids, G_VARIANT_TYPE("av"));
	dm_node *n = dm_find(m, id);
	if (n && depth != 0) {
		uint32_t count = n->kids.len;
		int32_t *ks = malloc((count ? count : 1) * sizeof *ks);
		if (!ks) abort();
		memcpy(ks, n->kids.data, count * sizeof *ks);  // n moves as nothing's added, but keep it plain
		for (uint32_t i = 0; i < count; i++) g_variant_builder_add(&kids, "v", dm_layout(m, ks[i], depth - 1, names));
		free(ks);
	}
	return g_variant_new("(i@a{sv}av)", id, dm_props(m, id, names), &kids);
}

static void choose(void *ctx);

typedef struct {
	uint32_t icon;
	uint32_t index;
	uint8_t role;
} chosen;

// dm_event is the panel's event on an item: a click chooses it.
static void dm_event(dbus_menu *m, int32_t id, const char *name) {
	dm_node *n = dm_find(m, id);
	if (strcmp(name, "clicked") || !n || n->entry < 0) return;
	const ak_menu_entry *it = &m->entries.data[n->entry];
	if (it->flags & (AK_MENU_SEPARATOR | AK_MENU_DISABLED | AK_MENU_SUBMENU)) return;
	chosen *c = malloc(sizeof *c);
	if (!c) abort();
	*c = (chosen){m->icon, it->index, it->role};
	lx_after_core(choose, c);
}

static void dm_method(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
	(void)conn;
	(void)sender;
	(void)path;
	(void)iface;
	dbus_menu *m = data;
	if (!strcmp(method, "GetLayout")) {
		gint32 id, depth;
		const gchar **names = NULL;
		g_variant_get(params, "(ii^a&s)", &id, &depth, &names);
		if (!dm_find(m, id)) {
			g_free(names);
			g_dbus_method_invocation_return_dbus_error(inv, "com.canonical.dbusmenu.Error.UnknownId", "no such item");
			return;
		}
		g_dbus_method_invocation_return_value(inv, g_variant_new("(u@(ia{sv}av))", m->revision, dm_layout(m, id, depth, names)));
		g_free(names);
	} else if (!strcmp(method, "GetGroupProperties")) {
		GVariant *ids_v = NULL;
		const gchar **names = NULL;
		g_variant_get(params, "(@ai^a&s)", &ids_v, &names);
		gsize n = 0;
		const gint32 *ids = g_variant_get_fixed_array(ids_v, &n, sizeof(gint32));
		GVariantBuilder b;
		g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
		if (n) {
			for (gsize i = 0; i < n; i++)
				if (dm_find(m, ids[i])) g_variant_builder_add(&b, "(i@a{sv})", ids[i], dm_props(m, ids[i], names));
		} else {
			for (uint32_t i = 0; i < m->nodes.len; i++) g_variant_builder_add(&b, "(i@a{sv})", m->nodes.data[i].id, dm_props(m, m->nodes.data[i].id, names));
		}
		g_variant_unref(ids_v);
		g_free(names);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(a(ia{sv}))", &b));
	} else if (!strcmp(method, "GetProperty")) {
		gint32 id;
		const gchar *name;
		g_variant_get(params, "(i&s)", &id, &name);
		const char *names[] = {name, NULL};
		GVariant *props = g_variant_ref_sink(dm_props(m, id, names));
		if (!g_variant_n_children(props)) {
			g_variant_unref(props);
			g_dbus_method_invocation_return_dbus_error(inv, "com.canonical.dbusmenu.Error.UnknownProperty", "no such property");
			return;
		}
		// The dictionary's one value, a variant: the reply's.
		GVariant *e = g_variant_get_child_value(props, 0);
		GVariant *val = g_variant_get_child_value(e, 1);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(@v)", val));
		g_variant_unref(val);
		g_variant_unref(e);
		g_variant_unref(props);
	} else if (!strcmp(method, "Event")) {
		gint32 id;
		const gchar *name;
		GVariant *v = NULL;
		guint32 t;
		g_variant_get(params, "(i&svu)", &id, &name, &v, &t);
		dm_event(m, id, name);
		if (v) g_variant_unref(v);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (!strcmp(method, "EventGroup")) {
		GVariantIter *it = NULL;
		g_variant_get(params, "(a(isvu))", &it);
		gint32 id;
		const gchar *name;
		GVariant *v;
		guint32 t;
		while (g_variant_iter_loop(it, "(i&svu)", &id, &name, &v, &t)) dm_event(m, id, name);
		g_variant_iter_free(it);
		g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE("i"), NULL, 0)));
	} else if (!strcmp(method, "AboutToShow")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", 0));
	} else if (!strcmp(method, "AboutToShowGroup")) {
		g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE("i"), NULL, 0), g_variant_new_array(G_VARIANT_TYPE("i"), NULL, 0)));
	} else {
		g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", "no such method");
	}
}

static GVariant *dm_property(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err, gpointer data) {
	(void)conn;
	(void)sender;
	(void)path;
	(void)iface;
	(void)err;
	(void)data;
	if (!strcmp(name, "Version")) return g_variant_new_uint32(3);
	if (!strcmp(name, "TextDirection")) return g_variant_new_string(H.rtl ? "rtl" : "ltr");
	if (!strcmp(name, "Status")) return g_variant_new_string("normal");
	if (!strcmp(name, "IconThemePath")) {
		const char *d = lx_icon_dir();
		return g_variant_new_strv(*d ? &d : NULL, *d ? 1 : 0);
	}
	return NULL;
}

static const GDBusInterfaceVTable dm_vtable = {dm_method, dm_property, NULL, {0}};

static bool dm_export(dbus_menu *m, char **err) {
	m->reg = lx_dbus_export(m->conn, m->path, menu_xml, MENU_IFACE, &dm_vtable, m, err);
	return m->reg != 0;
}

static void dm_free(dbus_menu *m) {
	if (m->reg) g_dbus_connection_unregister_object(m->conn, m->reg);
	dm_clear(m);
	ak_vec_free(&m->nodes);
	ak_vec_free(&m->entries);
	ak_buf_free(&m->bytes);
	free(m);
}

// ---- The icons ----

typedef struct {
	uint32_t id;
	GDBusConnection *conn;  // its own session bus connection
	guint reg;
	dbus_menu *menu;
	uint8_t flags;  // 1 template, 2 clickable
	char *image, *symbol, *title, *tooltip, *name;
	const char *icon_name;
	// The Image's pixels (straight RGBA), once decoded.
	uint8_t *pixels;
	int w, h;
	char *loading;  // the Image being decoded
	// Where the panel last said it was clicked (screen pixels), which a
	// flyout hangs from, and when its flyout last closed as it lost the
	// keyboard (desk.c).
	int click_x, click_y;
	bool clicked;
	gint64 flyout_closed;
} sni;

static struct {
	AK_VEC(sni *) icons;
	dbus_menu *dock;
	reply *waiting;
	AK_VEC(ak_buf) queue;
	guint watching;  // the NameOwnerChanged subscription
} T;

static sni *icon_of(uint32_t id) {
	for (uint32_t i = 0; i < T.icons.len; i++)
		if (T.icons.data[i]->id == id) return T.icons.data[i];
	return NULL;
}

// tray_event hands std/tray what happened: to the "next" waiting, or
// queued.
static void tray_event(uint32_t id, uint8_t kind, uint32_t index) {
	ak_buf b = {0};
	ak_buf_u32(&b, id);
	ak_buf_u8(&b, kind);
	ak_buf_u32(&b, index);
	if (T.waiting) {
		reply *r = T.waiting;
		T.waiting = NULL;
		reply_ok(r, b.data, b.len);
		ak_buf_free(&b);
		return;
	}
	ak_vec_push(&T.queue, b);
}

static void quit_now(void *ctx) {
	(void)ctx;
	lx_request_quit();
}

static void choose(void *ctx) {
	chosen *c = ctx;
	if (c->role == AK_MENU_ROLE_QUIT)
		lx_after_core(quit_now, NULL);
	else
		tray_event(c->icon, TRAY_ITEM, c->index);
	free(c);
}

static void sni_emit(sni *t, const char *signal, GVariant *params) {
	if (t->conn) lx_dbus_emit(t->conn, SNI_PATH, SNI_IFACE, signal, params);
}

// lx_tray_click is where a tray icon was last clicked (screen pixels):
// false if no panel said.
bool lx_tray_click(uint32_t icon, int *x, int *y) {
	sni *t = icon_of(icon);
	if (!t || !t->clicked) return false;
	*x = t->click_x;
	*y = t->click_y;
	return true;
}

// lx_tray_flyout_closed: icon's flyout closed as it lost the keyboard.
void lx_tray_flyout_closed(uint32_t icon) {
	sni *t = icon_of(icon);
	if (t) t->flyout_closed = g_get_monotonic_time();
}

// click_closed_flyout reports whether a click on t is the one that just
// closed its flyout (taking its keyboard: within 400 ms), and forgets it.
static bool click_closed_flyout(sni *t) {
	if (!t->flyout_closed) return false;
	bool closed = g_get_monotonic_time() - t->flyout_closed < 400000;
	t->flyout_closed = 0;
	return closed;
}

// sni_method answers the panel: Activate is a click, for an icon that
// takes them; the rest the panel does itself (the menu, through
// dbusmenu).
static void sni_method(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
	(void)conn;
	(void)sender;
	(void)path;
	(void)iface;
	sni *t = data;
	if (g_variant_is_of_type(params, G_VARIANT_TYPE("(ii)"))) {
		// Activate, SecondaryActivate and ContextMenu carry where the click
		// was, on the panels that know (KDE Plasma, GNOME's AppIndicator
		// extension).
		gint32 x, y;
		g_variant_get(params, "(ii)", &x, &y);
		if (x || y) {
			t->click_x = x;
			t->click_y = y;
			t->clicked = true;
		}
	}
	// The click that took the keyboard from its flyout closed it: it
	// doesn't open it again.
	if (!strcmp(method, "Activate") && (t->flags & 2) && !click_closed_flyout(t)) tray_event(t->id, TRAY_CLICK, 0);
	g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *empty_pixmaps(void) { return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0); }

// scale_rgba scales straight RGBA pixels to height size, the aspect kept,
// each pixel the mean of those it covers (weighted by alpha).
static uint8_t *scale_rgba(const uint8_t *px, int w, int h, int size, int *ow) {
	int nw = (int)((int64_t)w * size / h);
	if (nw < 1) nw = 1;
	*ow = nw;
	uint8_t *out = calloc((size_t)nw * (size_t)size * 4, 1);
	if (!out) abort();
	for (int y = 0; y < size; y++) {
		int y0 = y * h / size, y1 = (y + 1) * h / size;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < nw; x++) {
			int x0 = x * w / nw, x1 = (x + 1) * w / nw;
			if (x1 <= x0) x1 = x0 + 1;
			uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int sy = y0; sy < y1 && sy < h; sy++)
				for (int sx = x0; sx < x1 && sx < w; sx++) {
					const uint8_t *p = px + ((size_t)sy * (size_t)w + (size_t)sx) * 4;
					r += p[0] * (uint32_t)p[3];
					g += p[1] * (uint32_t)p[3];
					b += p[2] * (uint32_t)p[3];
					a += p[3];
					n++;
				}
			uint8_t *o = out + ((size_t)y * (size_t)nw + (size_t)x) * 4;
			if (a) {
				o[0] = (uint8_t)(r / a);
				o[1] = (uint8_t)(g / a);
				o[2] = (uint8_t)(b / a);
			}
			if (n) o[3] = (uint8_t)(a / n);
		}
	}
	return out;
}

// pixmaps is the Image at its size (at most 128 pixels tall) and smaller
// ones panels draw at, as ARGB32 in network byte order; a template's
// color the desktop's style's.
static GVariant *pixmaps(sni *t) {
	if (!t->pixels) return empty_pixmaps();
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a(iiay)"));
	int sizes[] = {t->h < 128 ? t->h : 128, 48, 32, 24, 22, 16}, done[6], nd = 0;
	uint8_t ink = H.dark ? 255 : 0;
	for (int i = 0; i < 6; i++) {
		int size = sizes[i];
		bool seen = false;
		for (int k = 0; k < nd; k++) seen |= done[k] == size;
		if (size > t->h || size <= 0 || seen) continue;
		done[nd++] = size;
		int w;
		uint8_t *px = size == t->h ? NULL : scale_rgba(t->pixels, t->w, t->h, size, &w);
		const uint8_t *src = px ? px : t->pixels;
		if (!px) w = t->w;
		size_t n = (size_t)w * (size_t)size * 4;
		uint8_t *argb = malloc(n);
		if (!argb) abort();
		for (size_t k = 0; k < n; k += 4) {
			uint8_t r = src[k], g = src[k + 1], bl = src[k + 2], a = src[k + 3];
			if (t->flags & 1) r = g = bl = ink;
			argb[k] = a;
			argb[k + 1] = r;
			argb[k + 2] = g;
			argb[k + 3] = bl;
		}
		g_variant_builder_add(&b, "(ii@ay)", w, size, g_variant_new_fixed_array(G_VARIANT_TYPE("y"), argb, n, 1));
		free(argb);
		free(px);
	}
	return g_variant_builder_end(&b);
}

static GVariant *sni_property(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface, const gchar *name, GError **err, gpointer data) {
	(void)conn;
	(void)sender;
	(void)path;
	(void)iface;
	(void)err;
	sni *t = data;
	if (!strcmp(name, "Category")) return g_variant_new_string("ApplicationStatus");
	if (!strcmp(name, "Id")) {
		char id[512];
		if (!str_empty(t->name))
			snprintf(id, sizeof id, "%s-%s", H.app_id, t->name);
		else if (t->id > 1)
			snprintf(id, sizeof id, "%s-%u", H.app_id, (unsigned)t->id);
		else
			snprintf(id, sizeof id, "%s", H.app_id);
		return g_variant_new_string(id);
	}
	if (!strcmp(name, "Title")) return g_variant_new_string(S(H.app_name));
	if (!strcmp(name, "Status")) return g_variant_new_string("Active");
	if (!strcmp(name, "WindowId")) return g_variant_new_int32(0);
	if (!strcmp(name, "IconThemePath")) return g_variant_new_string(lx_icon_dir());
	if (!strcmp(name, "IconName")) return g_variant_new_string(S(t->icon_name));
	if (!strcmp(name, "IconPixmap")) return pixmaps(t);
	if (!strcmp(name, "OverlayIconName") || !strcmp(name, "AttentionIconName") || !strcmp(name, "AttentionMovieName")) return g_variant_new_string("");
	if (!strcmp(name, "OverlayIconPixmap") || !strcmp(name, "AttentionIconPixmap")) return empty_pixmaps();
	if (!strcmp(name, "ToolTip")) return g_variant_new("(s@a(iiay)ss)", "", empty_pixmaps(), S(t->tooltip), "");
	if (!strcmp(name, "ItemIsMenu")) return g_variant_new_boolean(!(t->flags & 2));
	if (!strcmp(name, "Menu")) return g_variant_new_object_path(SNI_MENU_PATH);
	if (!strcmp(name, "XAyatanaLabel") || !strcmp(name, "XAyatanaLabelGuide")) return g_variant_new_string(S(t->title));
	return NULL;
}

static const GDBusInterfaceVTable sni_vtable = {sni_method, sni_property, NULL, {0}};

typedef struct {
	uint32_t id;
	char *src;
} image_wait;

static void image_loaded(void *ctx, GdkTexture *tex, const char *err) {
	image_wait *iw = ctx;
	sni *t = icon_of(iw->id);
	if (t && t->image && !strcmp(t->image, iw->src)) {
		free(t->loading);
		t->loading = NULL;
		if (!tex) {
			lx_log("tray icon %s: %s", iw->src, err ? err : "can't decode it");
		} else {
			int w = gdk_texture_get_width(tex), h = gdk_texture_get_height(tex);
			uint8_t *px = malloc((size_t)w * (size_t)h * 4);
			if (!px) abort();
			GdkTextureDownloader *d = gdk_texture_downloader_new(tex);
			gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
			gdk_texture_downloader_download_into(d, px, (gsize)w * 4);
			gdk_texture_downloader_free(d);
			free(t->pixels);
			t->pixels = px;
			t->w = w;
			t->h = h;
			sni_emit(t, "NewIcon", NULL);
		}
	}
	free(iw->src);
	free(iw);
}

static void sni_close(sni *t) {
	if (t->reg) g_dbus_connection_unregister_object(t->conn, t->reg);
	t->reg = 0;
	if (t->menu) dm_free(t->menu);
	t->menu = NULL;
	lx_close_bus(t->conn);
	t->conn = NULL;
}

static void sni_free(sni *t) {
	sni_close(t);
	free(t->image);
	free(t->symbol);
	free(t->title);
	free(t->tooltip);
	free(t->name);
	free(t->pixels);
	free(t->loading);
	free(t);
}

// sni_open connects the icon to the session bus on a connection of its
// own and exports its item and menu there.
static bool sni_open(sni *t, char **err) {
	t->conn = lx_private_bus(err);
	if (!t->conn) return false;
	t->menu = calloc(1, sizeof *t->menu);
	if (!t->menu) abort();
	t->menu->conn = t->conn;
	t->menu->path = SNI_MENU_PATH;
	t->menu->icon = t->id;
	if (!dm_export(t->menu, err)) {
		sni_close(t);
		return false;
	}
	t->reg = lx_dbus_export(t->conn, SNI_PATH, sni_xml, SNI_IFACE, &sni_vtable, t, err);
	if (!t->reg) {
		sni_close(t);
		return false;
	}
	return true;
}

// sni_register tells the watcher (the panel's) of the icon, if one runs.
static void sni_register(sni *t) {
	lx_dbus_call(t->conn, WATCHER_NAME, WATCHER_PATH, WATCHER_NAME, "RegisterStatusNotifierItem", g_variant_new("(s)", g_dbus_connection_get_unique_name(t->conn)), 5000, NULL, NULL);
}

static void on_owner_changed(GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface, const gchar *signal, GVariant *params, gpointer data) {
	(void)c;
	(void)sender;
	(void)path;
	(void)iface;
	(void)signal;
	(void)data;
	const gchar *name, *old, *now;
	g_variant_get(params, "(&s&s&s)", &name, &old, &now);
	if (strcmp(name, WATCHER_NAME) || !*now) return;
	for (uint32_t i = 0; i < T.icons.len; i++) sni_register(T.icons.data[i]);
}

// watch_watcher registers the icons again with each new watcher (the
// panel restarting, the AppIndicator extension turned on).
static void watch_watcher(void) {
	GDBusConnection *bus = lx_session_bus();
	if (T.watching || !bus) return;
	T.watching = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_owner_changed, NULL, NULL);
}

static bool str_same(const char *a, const char *b) { return !strcmp(S(a), S(b)); }

static char *read_str(ak_reader *r) {
	uint32_t n;
	const uint8_t *p = ak_read_bytes(r, &n);
	return str_dup(ak_strn((const char *)p, p ? n : 0));
}

// tray_set makes an icon, or changes one: u32 id, u8 flags, strings
// image, symbol, title, tooltip, name, then its menu.
static void tray_set(ak_span args, reply *r) {
	ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
	uint32_t id = ak_read_u32(&rd);
	uint8_t flags = ak_read_u8(&rd);
	char *image = read_str(&rd), *symbol = read_str(&rd), *title = read_str(&rd), *tooltip = read_str(&rd), *name = read_str(&rd);
	if (rd.err) {
		reply_err(r, "tray.set: bad arguments");
		goto out;
	}
	sni *t = icon_of(id);
	bool created = !t;
	if (created) {
		t = calloc(1, sizeof *t);
		if (!t) abort();
		t->id = id;
		char *err = NULL;
		if (!sni_open(t, &err)) {
			reply_err(r, "%s", S(err));
			g_free(err);
			free(t);
			goto out;
		}
		ak_vec_push(&T.icons, t);
		watch_watcher();
	}
	bool icon_changed = !str_same(t->image, image) || !str_same(t->symbol, symbol) || (t->flags & 1) != (flags & 1);
	bool tip_changed = !str_same(t->tooltip, tooltip), title_changed = !str_same(t->title, title);
	bool image_changed = !str_same(t->image, image);
	t->flags = flags;
	free(t->image);
	free(t->symbol);
	free(t->title);
	free(t->tooltip);
	free(t->name);
	t->image = image;
	t->symbol = symbol;
	t->title = title;
	t->tooltip = tooltip;
	t->name = name;
	image = symbol = title = tooltip = name = NULL;
	t->icon_name = "";
	if (str_empty(t->image)) {
		t->icon_name = lx_icon_name(t->symbol);
		free(t->pixels);
		t->pixels = NULL;
	} else if (image_changed || (!t->pixels && !str_same(t->loading, t->image))) {
		free(t->pixels);
		t->pixels = NULL;
		free(t->loading);
		t->loading = str_dup(ak_cstr(t->image));
		image_wait *iw = malloc(sizeof *iw);
		if (!iw) abort();
		iw->id = id;
		iw->src = str_dup(ak_cstr(t->image));
		lx_load_texture(t->image, image_loaded, iw);
	}
	dm_set(t->menu, ak_strn((const char *)rd.p + rd.off, rd.len - rd.off));
	if (created) {
		sni_register(t);
	} else {
		if (icon_changed) sni_emit(t, "NewIcon", NULL);
		if (tip_changed) sni_emit(t, "NewToolTip", NULL);
		if (title_changed) sni_emit(t, "XAyatanaNewLabel", g_variant_new("(ss)", S(t->title), S(t->title)));
	}
	reply_ok(r, NULL, 0);
out:
	free(image);
	free(symbol);
	free(title);
	free(tooltip);
	free(name);
}

// tray_remove takes an icon away: its connection closes, and the watcher
// sees its name go.
static void tray_remove(ak_span args, reply *r) {
	if (args.len >= 4) {
		ak_reader rd = {(const uint8_t *)args.p, args.len, 0, false};
		uint32_t id = ak_read_u32(&rd);
		for (uint32_t i = 0; i < T.icons.len; i++)
			if (T.icons.data[i]->id == id) {
				sni *t = T.icons.data[i];
				memmove(T.icons.data + i, T.icons.data + i + 1, (T.icons.len - i - 1) * sizeof *T.icons.data);
				T.icons.len--;
				sni_free(t);
				break;
			}
	}
	reply_ok(r, NULL, 0);
}

static void supported_answer(void *ctx, GVariant *rep, const char *err) {
	bool ok = false;
	if (!err && rep) {
		GVariant *v = NULL;
		g_variant_get(rep, "(v)", &v);
		if (v && g_variant_is_of_type(v, G_VARIANT_TYPE("b"))) ok = g_variant_get_boolean(v);
		if (v) g_variant_unref(v);
	}
	uint8_t b = ok;
	reply_ok(ctx, &b, 1);
}

// supported: u8 whether a panel shows tray icons (a StatusNotifierHost is
// registered with the watcher).
static void tray_supported(ak_span args, reply *r) {
	(void)args;
	GDBusConnection *bus = lx_session_bus();
	if (!bus) {
		reply_ok(r, "\0", 1);
		return;
	}
	lx_dbus_call(bus, WATCHER_NAME, WATCHER_PATH, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", WATCHER_NAME, "IsStatusNotifierHostRegistered"), 2000, supported_answer, r);
}

// launcher_update tells the dock of the quicklist ("/": none).
static void launcher_update(GDBusConnection *bus, const char *path) {
	uint32_t h = 2166136261u;  // FNV-1a of the app's ID
	for (const char *p = S(H.app_id); *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
	char obj[96], uri[512];
	snprintf(obj, sizeof obj, "%s/q%x", LAUNCHER_PATH, (unsigned)h);
	snprintf(uri, sizeof uri, "application://%s.desktop", S(H.app_id));
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&b, "{sv}", "quicklist", g_variant_new_string(path));
	lx_dbus_emit(bus, obj, LAUNCHER_IFACE, "Update", g_variant_new("(sa{sv})", uri, &b));
}

// dock: the launcher's quicklist, a dbusmenu of the items, announced with
// LauncherEntry's Update (an empty menu: none).
static void tray_dock(ak_span args, reply *r) {
	GDBusConnection *bus = lx_session_bus();
	if (!bus) {
		reply_err(r, "no session bus");
		return;
	}
	ak_menu_entry_vec items = {0};
	ak_decode_menu((const uint8_t *)args.p, args.len, &items);
	bool empty = items.len == 0;
	ak_vec_free(&items);
	if (empty) {
		if (T.dock) {
			dm_free(T.dock);
			T.dock = NULL;
			launcher_update(bus, "/");
		}
		reply_ok(r, NULL, 0);
		return;
	}
	if (!T.dock) {
		dbus_menu *d = calloc(1, sizeof *d);
		if (!d) abort();
		d->conn = bus;
		d->path = QUICKLIST_PATH;
		char *err = NULL;
		if (!dm_export(d, &err)) {
			reply_err(r, "%s", S(err));
			g_free(err);
			free(d);
			return;
		}
		T.dock = d;
	}
	dm_set(T.dock, args);
	launcher_update(bus, QUICKLIST_PATH);
	reply_ok(r, NULL, 0);
}

// next: the next click or chosen item (a long poll).
static void tray_next(ak_span args, reply *r) {
	(void)args;
	if (T.queue.len) {
		ak_buf b = T.queue.data[0];
		memmove(T.queue.data, T.queue.data + 1, (T.queue.len - 1) * sizeof *T.queue.data);
		T.queue.len--;
		reply_ok(r, b.data, b.len);
		ak_buf_free(&b);
		return;
	}
	if (T.waiting) reply_err(T.waiting, "stopped");
	T.waiting = r;
}

static void tray_stop(ak_span args, reply *r) {
	(void)args;
	if (T.waiting) {
		reply *w = T.waiting;
		T.waiting = NULL;
		reply_err(w, "stopped");
	}
	for (uint32_t i = 0; i < T.queue.len; i++) ak_buf_free(&T.queue.data[i]);
	T.queue.len = 0;
	reply_ok(r, NULL, 0);
}

// lx_tray_style_changed draws template images again in the desktop's
// style.
void lx_tray_style_changed(void) {
	for (uint32_t i = 0; i < T.icons.len; i++)
		if ((T.icons.data[i]->flags & 1) && T.icons.data[i]->pixels) sni_emit(T.icons.data[i], "NewIcon", NULL);
}

static const method_t tray_methods[] = {
	{"tray", "supported", tray_supported},
	{"tray", "set", tray_set},
	{"tray", "remove", tray_remove},
	{"tray", "dock", tray_dock},
	{"tray", "next", tray_next},
	{"tray", "stop", tray_stop},
};

static void tray_modules(void) { lx_register_methods(tray_methods, AK_LEN(tray_methods)); }

const area lx_area_tray = {
	.name = "tray",
	.modules = tray_modules,
};
