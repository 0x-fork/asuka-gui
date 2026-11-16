// symbols.c: symbols are SF Symbol names. Those with a match in the
// freedesktop and Adwaita icon names are the icon theme's symbolic icons,
// so an app looks as GNOME's do; the rest are Material Symbols (as Android
// and the web draw them), written once as symbolic SVGs into an icon
// directory the theme searches. Either way the name is an icon name GTK
// draws and recolors (buttons, menus, tabs, symbols).
#include "host.h"
#include "symbols.h"

#include <stdio.h>
#include <stdlib.h>

// The SF Symbol names freedesktop's and Adwaita's icon names match, sorted.
static const struct {
	const char *sf, *icon;
} adwaita_icons[] = {
	{"archivebox", "package-x-generic-symbolic"},
	{"arrow.backward", "go-previous-symbolic"},
	{"arrow.clockwise", "view-refresh-symbolic"},
	{"arrow.counterclockwise", "object-rotate-left-symbolic"},
	{"arrow.down", "go-down-symbolic"},
	{"arrow.forward", "go-next-symbolic"},
	{"arrow.left", "go-previous-symbolic"},
	{"arrow.right", "go-next-symbolic"},
	{"arrow.triangle.2.circlepath", "view-refresh-symbolic"},
	{"arrow.up", "go-up-symbolic"},
	{"arrow.uturn.backward", "edit-undo-symbolic"},
	{"arrow.uturn.forward", "edit-redo-symbolic"},
	{"arrowshape.turn.up.left", "mail-reply-sender-symbolic"},
	{"arrowshape.turn.up.left.2", "mail-reply-all-symbolic"},
	{"backward.fill", "media-seek-backward-symbolic"},
	{"battery.100", "battery-full-symbolic"},
	{"bell", "preferences-system-notifications-symbolic"},
	{"bell.fill", "preferences-system-notifications-symbolic"},
	{"bold", "format-text-bold-symbolic"},
	{"bookmark", "user-bookmarks-symbolic"},
	{"bookmark.fill", "user-bookmarks-symbolic"},
	{"calendar", "x-office-calendar-symbolic"},
	{"camera", "camera-photo-symbolic"},
	{"camera.fill", "camera-photo-symbolic"},
	{"checkmark", "object-select-symbolic"},
	{"checkmark.circle", "emblem-ok-symbolic"},
	{"checkmark.circle.fill", "emblem-ok-symbolic"},
	{"chevron.backward", "go-previous-symbolic"},
	{"chevron.down", "go-down-symbolic"},
	{"chevron.forward", "go-next-symbolic"},
	{"chevron.left", "go-previous-symbolic"},
	{"chevron.right", "go-next-symbolic"},
	{"chevron.up", "go-up-symbolic"},
	{"clock", "preferences-system-time-symbolic"},
	{"clock.fill", "preferences-system-time-symbolic"},
	{"cloud", "weather-overcast-symbolic"},
	{"desktopcomputer", "computer-symbolic"},
	{"doc", "text-x-generic-symbolic"},
	{"doc.fill", "text-x-generic-symbolic"},
	{"doc.on.clipboard", "edit-paste-symbolic"},
	{"doc.on.doc", "edit-copy-symbolic"},
	{"doc.text", "text-x-generic-symbolic"},
	{"ellipsis", "view-more-symbolic"},
	{"ellipsis.circle", "view-more-symbolic"},
	{"envelope", "mail-unread-symbolic"},
	{"envelope.fill", "mail-unread-symbolic"},
	{"exclamationmark.circle", "dialog-error-symbolic"},
	{"exclamationmark.triangle", "dialog-warning-symbolic"},
	{"exclamationmark.triangle.fill", "dialog-warning-symbolic"},
	{"eye", "view-reveal-symbolic"},
	{"eye.slash", "view-conceal-symbolic"},
	{"film", "video-x-generic-symbolic"},
	{"folder", "folder-symbolic"},
	{"folder.fill", "folder-symbolic"},
	{"forward.fill", "media-seek-forward-symbolic"},
	{"gear", "emblem-system-symbolic"},
	{"gearshape", "emblem-system-symbolic"},
	{"gearshape.2", "applications-system-symbolic"},
	{"gearshape.fill", "emblem-system-symbolic"},
	{"globe", "web-browser-symbolic"},
	{"hammer", "applications-engineering-symbolic"},
	{"headphones", "audio-headphones-symbolic"},
	{"heart", "emblem-favorite-symbolic"},
	{"heart.fill", "emblem-favorite-symbolic"},
	{"house", "go-home-symbolic"},
	{"house.circle", "go-home-symbolic"},
	{"house.fill", "go-home-symbolic"},
	{"info.circle", "dialog-information-symbolic"},
	{"info.circle.fill", "dialog-information-symbolic"},
	{"iphone", "phone-symbolic"},
	{"italic", "format-text-italic-symbolic"},
	{"key", "dialog-password-symbolic"},
	{"keyboard", "input-keyboard-symbolic"},
	{"laptopcomputer", "computer-symbolic"},
	{"line.3.horizontal", "open-menu-symbolic"},
	{"link", "insert-link-symbolic"},
	{"list.bullet", "view-list-bullet-symbolic"},
	{"location", "find-location-symbolic"},
	{"location.fill", "find-location-symbolic"},
	{"lock", "changes-prevent-symbolic"},
	{"lock.fill", "changes-prevent-symbolic"},
	{"lock.open", "changes-allow-symbolic"},
	{"magnifyingglass", "system-search-symbolic"},
	{"mappin", "mark-location-symbolic"},
	{"mic", "audio-input-microphone-symbolic"},
	{"mic.fill", "audio-input-microphone-symbolic"},
	{"minus", "list-remove-symbolic"},
	{"minus.circle", "list-remove-symbolic"},
	{"moon", "weather-clear-night-symbolic"},
	{"moon.fill", "weather-clear-night-symbolic"},
	{"music.note", "audio-x-generic-symbolic"},
	{"paperclip", "mail-attachment-symbolic"},
	{"paperplane", "mail-send-symbolic"},
	{"paperplane.fill", "mail-send-symbolic"},
	{"pause", "media-playback-pause-symbolic"},
	{"pause.fill", "media-playback-pause-symbolic"},
	{"pencil", "document-edit-symbolic"},
	{"person", "avatar-default-symbolic"},
	{"person.2", "system-users-symbolic"},
	{"person.2.fill", "system-users-symbolic"},
	{"person.circle", "avatar-default-symbolic"},
	{"person.crop.circle", "avatar-default-symbolic"},
	{"person.crop.circle.fill", "avatar-default-symbolic"},
	{"person.fill", "avatar-default-symbolic"},
	{"phone", "call-start-symbolic"},
	{"phone.fill", "call-start-symbolic"},
	{"photo", "image-x-generic-symbolic"},
	{"photo.fill", "image-x-generic-symbolic"},
	{"photo.on.rectangle", "image-x-generic-symbolic"},
	{"play", "media-playback-start-symbolic"},
	{"play.fill", "media-playback-start-symbolic"},
	{"plus", "list-add-symbolic"},
	{"plus.circle", "list-add-symbolic"},
	{"plus.circle.fill", "list-add-symbolic"},
	{"printer", "printer-symbolic"},
	{"questionmark.circle", "dialog-question-symbolic"},
	{"rectangle.grid.2x2", "view-grid-symbolic"},
	{"safari", "web-browser-symbolic"},
	{"scissors", "edit-cut-symbolic"},
	{"shield", "security-high-symbolic"},
	{"sidebar.left", "sidebar-show-symbolic"},
	{"sparkles", "applications-graphics-symbolic"},
	{"speaker.slash", "audio-volume-muted-symbolic"},
	{"speaker.slash.fill", "audio-volume-muted-symbolic"},
	{"speaker.wave.2", "audio-volume-high-symbolic"},
	{"speaker.wave.2.fill", "audio-volume-high-symbolic"},
	{"square.and.arrow.down", "document-save-symbolic"},
	{"square.and.arrow.up", "emblem-shared-symbolic"},
	{"square.and.arrow.up.fill", "emblem-shared-symbolic"},
	{"square.and.pencil", "document-edit-symbolic"},
	{"square.grid.2x2", "view-grid-symbolic"},
	{"star", "non-starred-symbolic"},
	{"star.fill", "starred-symbolic"},
	{"stop", "media-playback-stop-symbolic"},
	{"stop.fill", "media-playback-stop-symbolic"},
	{"strikethrough", "format-text-strikethrough-symbolic"},
	{"sun.max", "display-brightness-symbolic"},
	{"trash", "user-trash-symbolic"},
	{"trash.fill", "user-trash-symbolic"},
	{"tray", "mail-inbox-symbolic"},
	{"tray.fill", "mail-inbox-symbolic"},
	{"underline", "format-text-underline-symbolic"},
	{"video", "camera-video-symbolic"},
	{"video.fill", "camera-video-symbolic"},
	{"wand.and.stars", "applications-graphics-symbolic"},
	{"wifi", "network-wireless-symbolic"},
	{"wrench", "applications-engineering-symbolic"},
	{"xmark", "window-close-symbolic"},
	{"xmark.circle", "edit-clear-symbolic"},
	{"xmark.circle.fill", "edit-clear-symbolic"},
};

static GtkIconTheme *icon_theme;
static char *icon_dir;
static GHashTable *resolved;  // SF name → icon name ("" none)

static int cmp_adw(const void *k, const void *e) { return strcmp(k, *(const char *const *)e); }
static int cmp_path(const void *k, const void *e) { return strcmp(k, ((const lx_symbol_path *)e)->name); }
static int cmp_label(const void *k, const void *e) { return strcmp(k, ((const lx_symlabel *)e)->name); }

static const char *adwaita(const char *sf) {
	const void *e = bsearch(sf, adwaita_icons, AK_LEN(adwaita_icons), sizeof *adwaita_icons, cmp_adw);
	return e ? ((const char *const *)e)[1] : NULL;
}

const char *lx_symbol_label(const char *symbol) {
	if (str_empty(symbol)) return "";
	const lx_symlabel *l = bsearch(symbol, lx_symbol_labels, lx_symbol_label_count, sizeof *lx_symbol_labels, cmp_label);
	return l ? l->label : "";
}

static GtkIconTheme *theme(void) {
	if (!icon_theme) icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	return icon_theme;
}

// write_icon writes a Material Symbol as a symbolic SVG icon the theme
// finds, and returns its name (malloc'd), or NULL.
static char *write_icon(const char *sf, const char *path) {
	if (!icon_dir) {
		icon_dir = dsk_join(g_get_user_cache_dir(), "asuka/icons");
		if (!dsk_mkdirs(icon_dir)) return NULL;
		gtk_icon_theme_add_search_path(theme(), icon_dir);
	}
	char name[256];
	size_t k = (size_t)snprintf(name, sizeof name, "asuka-");
	for (const char *p = sf; *p && k < sizeof name - 16; p++) name[k++] = *p == '.' ? '-' : *p;
	snprintf(name + k, sizeof name - k, "-symbolic");
	char file[512];
	snprintf(file, sizeof file, "%s.svg", name);
	char *full = dsk_join(icon_dir, file);
	ak_buf svg = {0};
	ak_buf_printf(&svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 -960 960 960\"><path fill=\"#000\" d=\"%s\"/></svg>", path);
	size_t n;
	char *old = dsk_read_file(full, &n);
	bool same = old && n == svg.len && !memcmp(old, svg.data, n);
	free(old);
	bool ok = same || dsk_write_file(full, svg.data, svg.len);
	ak_buf_free(&svg);
	free(full);
	return ok ? strdup(name) : NULL;
}

static char *resolve_icon(const char *sf) {
	size_t n = strlen(sf);
	if (n > 9 && !strcmp(sf + n - 9, "-symbolic")) return strdup(sf);
	const char *adw = adwaita(sf);
	if (adw && gtk_icon_theme_has_icon(theme(), adw)) return strdup(adw);
	const lx_symbol_path *m = bsearch(sf, lx_material_symbols, lx_material_symbol_count, sizeof *lx_material_symbols, cmp_path);
	if (m) {
		char *name = write_icon(sf, lx_material_paths[m->path]);
		if (name) return name;
	}
	if (adw) return strdup(adw);  // GTK shows its missing-image icon
	lx_log("no symbol named %s on Linux", sf);
	return strdup("");
}

// lx_icon_name is the icon name GTK draws an SF Symbol name with ("" for
// none). A name that's already an icon name ("...-symbolic") passes
// through.
const char *lx_icon_name(const char *sf) {
	if (str_empty(sf)) return "";
	if (!resolved) resolved = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
	const char *name = g_hash_table_lookup(resolved, sf);
	if (name) return name;
	char *r = resolve_icon(sf);
	g_hash_table_insert(resolved, strdup(sf), r);
	return r;
}

void lx_style_symbol(node *n) {
	str_setc(&n->icon, lx_icon_name(n->symbol));
	lx_queue_draw(n);
}

// lx_draw_symbol draws a Symbol node's icon at its size, in its color.
void lx_draw_symbol(node *n, GtkSnapshot *snap, const graphene_rect_t *b) {
	if (str_empty(n->symbol) || str_empty(n->icon)) return;
	float size;
	int weight;
	lx_font(n, &size, &weight);
	int scale = n->widget ? gtk_widget_get_scale_factor(n->widget) : 1;
	GtkIconPaintable *p = gtk_icon_theme_lookup_icon(theme(), n->icon, NULL, (int)(size + 0.5f), scale, n->rtl ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR, 2 /* force symbolic */);
	if (!p) return;
	GdkRGBA c;
	if (!n->color || !lx_resolve(n->color, &c)) c = H.theme.fg;
	GdkRGBA colors[4] = {c, H.theme.red, H.theme.yellow, H.theme.green};
	graphene_point_t pt = {(b->size.width - size) / 2, (b->size.height - size) / 2};
	gtk_snapshot_save(snap);
	gtk_snapshot_translate(snap, &pt);
	gtk_symbolic_paintable_snapshot_symbolic((GtkSymbolicPaintable *)p, (GdkSnapshot *)snap, size, size, colors, 4);
	gtk_snapshot_restore(snap);
	g_object_unref(p);
}

// lx_icon_dir is where the host writes the symbols' icons ("" until the
// first): a tray's IconThemePath.
const char *lx_icon_dir(void) { return S(icon_dir); }
