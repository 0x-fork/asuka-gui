// maps.View on Linux: libshumate's ShumateSimpleMap (GNOME Maps' map),
// showing OpenStreetMap's tiles; Satellite and Hybrid have no free
// imagery source: they show the streets too, as Muted does. showsUser
// has no location layer: nothing's shown. Its events: regionChange once
// the user moved it, markerPress, press (a click off the markers), error
// (no libshumate, which the view then says instead of a map). It has no
// size of its own: its style gives it one.
//
// libshumate's functions are asuka_linux.def's, as GTK's are.
#include "asuka_linux.h"
#include "maps.asuka.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _GParamSpec GParamSpec;

static struct {
	bool tried;
	char *err;  // why there's no map
} S;

// shumate loads libshumate (asuka_linux.def's SHUMATE functions), once:
// false (S.err why) when it isn't there.
static bool shumate(void) {
	if (S.tried) return !S.err;
	S.tried = true;
	if (!asuka_linux() || !G(gtk_widget_add_css_class)) {
		S.err = asuka_strdup("maps need GTK 4");
		return false;
	}
	if (!asuka_linux_load(ASUKA_LIB_SHUMATE)) {
		S.err = asuka_sprintf("maps need libshumate (libshumate-1.0-1): %s isn't installed", asuka_linux_soname(ASUKA_LIB_SHUMATE));
		return false;
	}
	if (!G(shumate_simple_map_new) || !G(shumate_simple_map_set_map_source) || !G(shumate_simple_map_get_map) || !G(shumate_simple_map_get_viewport) ||
	    !G(shumate_simple_map_add_overlay_layer) || !G(shumate_simple_map_set_show_zoom_buttons) || !G(shumate_map_center_on) ||
	    !G(shumate_viewport_set_zoom_level) || !G(shumate_viewport_widget_coords_to_location) || !G(shumate_location_get_latitude) ||
	    !G(shumate_location_get_longitude) || !G(shumate_location_set_location) || !G(shumate_map_source_registry_new_with_defaults) ||
	    !G(shumate_map_source_registry_get_by_id) || !G(shumate_marker_layer_new) || !G(shumate_marker_layer_add_marker) ||
	    !G(shumate_marker_layer_remove_all) || !G(shumate_marker_new) || !G(shumate_marker_set_child) || !G(gtk_label_new) ||
	    !G(gtk_gesture_click_new) || !G(gtk_widget_add_controller) || !G(gtk_widget_get_width) || !G(gtk_widget_get_height) ||
	    !G(gtk_widget_set_can_target) || !G(gtk_widget_set_tooltip_text) || !G(gtk_image_new_from_icon_name) || !G(gtk_image_set_pixel_size) ||
	    !G(gtk_css_provider_new) || !G(gtk_style_context_add_provider_for_display)) {
		S.err = asuka_strdup("maps: this libshumate (or GTK) is too old");
		return false;
	}
	return true;
}

typedef struct {
	maps_View base;
	maps_View_events events;
	int refs;  // the view's, and each handler's and post's
	GtkWidget *sm, *map, *layer;
	gpointer vp;
	bool has_pending;
	maps_Region pending;  // waits for the map's size
	bool ticking, setting, reporting, marker_hit, interactive, destroyed;
} view;

static view *ref(view *v) {
	v->refs++;
	return v;
}

static void unref(view *v) {
	if (--v->refs == 0) free(v);
}

static void unref_closure(gpointer data, gpointer closure) { unref(data); }
static void unref_notify(gpointer data) { unref(data); }

// connect connects a handler that holds the view until it's let go.
static void connect(gpointer obj, const char *signal, GCallback cb, view *v) {
	G(g_signal_connect_data)(obj, signal, cb, ref(v), (GClosureNotify)unref_closure, 0);
}

static void emit_error(void *p) {
	view *v = p;
	if (!v->destroyed) maps_View_error(v->events, S.err);
	unref(v);
}

// apply centers the map on the pending region, zoomed to show it, once
// the map has a size.
static void apply(view *v);

static gboolean tick(GtkWidget *w, gpointer clock, gpointer data) {
	view *v = data;
	if (v->destroyed || !v->has_pending) {
		v->ticking = false;
		return 0;
	}
	if (G(gtk_widget_get_width)(v->map) > 0) {
		v->ticking = false;
		apply(v);
		return 0;
	}
	return 1;
}

static double merc(double lat) {
	lat = fmax(fmin(lat, 85), -85) * M_PI / 180;
	return log(tan(M_PI / 4 + lat / 2));
}

static void apply(view *v) {
	if (!v->has_pending) return;
	double w = G(gtk_widget_get_width)(v->map), h = G(gtk_widget_get_height)(v->map);
	if (w <= 0 || h <= 0) {
		if (!v->ticking) {
			v->ticking = true;
			G(gtk_widget_add_tick_callback)(v->sm, (GtkTickCallback)tick, ref(v), unref_notify);
		}
		return;
	}
	maps_Region r = v->pending;
	v->has_pending = false;
	double zoom = 20;
	if (r.lngDelta > 0) zoom = fmin(zoom, log2(w * 360 / (256 * r.lngDelta)));
	if (r.latDelta > 0) {
		double span = fabs(merc(r.center.lat + r.latDelta / 2) - merc(r.center.lat - r.latDelta / 2));
		if (span > 0) zoom = fmin(zoom, log2(h * 2 * M_PI / (256 * span)));
	}
	v->setting = true;
	G(shumate_viewport_set_zoom_level)(v->vp, fmax(zoom, 0));
	G(shumate_map_center_on)(v->map, r.center.lat, r.center.lng);
	v->setting = false;
}

// report tells the app the region the user moved the map to, once the
// loop's idle.
static void report(void *p) {
	view *v = p;
	v->reporting = false;
	if (!v->destroyed) {
		double w = G(gtk_widget_get_width)(v->map), h = G(gtk_widget_get_height)(v->map);
		double lat0, lng0, lat1, lng1;
		G(shumate_viewport_widget_coords_to_location)(v->vp, v->map, 0, 0, &lat0, &lng0);
		G(shumate_viewport_widget_coords_to_location)(v->vp, v->map, w, h, &lat1, &lng1);
		double dl = lng1 - lng0;
		if (dl < 0) dl += 360;
		maps_Region r = {{G(shumate_location_get_latitude)(v->vp), G(shumate_location_get_longitude)(v->vp)}, fabs(lat0 - lat1), dl};
		maps_View_regionChange(v->events, &r);
	}
	unref(v);
}

static void moved(GObject *o, GParamSpec *spec, gpointer data) {
	view *v = data;
	if (v->setting || v->reporting || v->destroyed) return;
	v->reporting = true;
	asuka_desktop_post(report, ref(v));
}

static void clicked(gpointer gesture, int n, double x, double y, gpointer data) {
	view *v = data;
	if (v->marker_hit) {
		v->marker_hit = false;
		return;
	}
	if (n != 1 || !v->interactive || v->destroyed) return;
	maps_Coordinate at;
	G(shumate_viewport_widget_coords_to_location)(v->vp, v->map, x, y, &at.lat, &at.lng);
	maps_View_press(v->events, &at);
}

typedef struct {
	view *v;
	char *id;
} pin;

static void pin_free(gpointer data, gpointer closure) {
	pin *p = data;
	unref(p->v);
	free(p->id);
	free(p);
}

static void pin_clicked(gpointer gesture, int n, double x, double y, gpointer data) {
	pin *p = data;
	p->v->marker_hit = true;
	if (!p->v->destroyed) maps_View_markerPress(p->v->events, p->id);
}

// The pins' colors: a CSS class each, in one provider for the display.
static struct {
	gpointer provider;
	asuka_writer css;
	char **classes;
	size_t n;
} pins;

static void color_pin(GtkWidget *image, uint32_t rgba) {
	char class[32];
	snprintf(class, sizeof class, "asuka-pin-%08x", rgba);
	bool have = false;
	for (size_t i = 0; i < pins.n && !have; i++) have = !strcmp(pins.classes[i], class);
	GdkDisplay *d = G(gdk_display_get_default)();
	if (!have && d) {
		pins.classes = realloc(pins.classes, (pins.n + 1) * sizeof *pins.classes);
		pins.classes[pins.n++] = asuka_strdup(class);
		char rule[96];
		int n = snprintf(rule, sizeof rule, ".%s { color: #%08x; }\n", class, rgba);
		asuka_writer_put(&pins.css, rule, (size_t)n);
		if (!pins.provider) {
			pins.provider = G(gtk_css_provider_new)();
			G(gtk_style_context_add_provider_for_display)(d, pins.provider, 700);
		}
		char *css = asuka_strndup((const char *)pins.css.data, pins.css.len);
		if (G(gtk_css_provider_load_from_string)) G(gtk_css_provider_load_from_string)(pins.provider, css);
		else if (G(gtk_css_provider_load_from_data)) G(gtk_css_provider_load_from_data)(pins.provider, css, -1);
		free(css);
	}
	G(gtk_widget_add_css_class)(image, class);
}

static void set_markers(maps_View *self, maps_list_Marker ms) {
	view *v = (view *)self;
	if (!v->sm) return;
	G(shumate_marker_layer_remove_all)(v->layer);
	for (size_t i = 0; i < ms.len; i++) {
		const maps_Marker *mk = &ms.data[i];
		GtkWidget *marker = G(shumate_marker_new)();
		GtkWidget *image = G(gtk_image_new_from_icon_name)("mark-location-symbolic");
		G(gtk_image_set_pixel_size)((GtkImage *)image, 32);
		if (mk->color) color_pin(image, asuka_desktop_color((uint64_t)*mk->color));
		else G(gtk_widget_add_css_class)(image, "accent");
		const char *t = mk->title ? mk->title : "", *s = mk->subtitle ? mk->subtitle : "";
		if (*t || *s) {
			char *tip = *t && *s ? asuka_sprintf("%s\n%s", t, s) : asuka_strdup(*t ? t : s);
			G(gtk_widget_set_tooltip_text)(image, tip);
			free(tip);
		}
		pin *p = malloc(sizeof *p);
		*p = (pin){ref(v), asuka_strdup(mk->id)};
		gpointer g = G(gtk_gesture_click_new)();
		G(g_signal_connect_data)(g, "released", (GCallback)pin_clicked, p, (GClosureNotify)pin_free, 0);
		G(gtk_widget_add_controller)(image, g);
		G(shumate_marker_set_child)(marker, image);
		G(shumate_location_set_location)(marker, mk->at.lat, mk->at.lng);
		G(shumate_marker_layer_add_marker)(v->layer, marker);
	}
}

static void set_region(maps_View *self, const maps_Region *r) {
	view *v = (view *)self;
	if (!v->sm) return;
	if (r->latDelta <= 0 && r->lngDelta <= 0 && r->center.lat == 0 && r->center.lng == 0) return;
	v->pending = *r;
	v->has_pending = true;
	apply(v);
}

static void set_interactive(maps_View *self, bool on) {
	view *v = (view *)self;
	v->interactive = on;
	if (v->sm) G(gtk_widget_set_can_target)(v->sm, on);
}

static void set_map_type(maps_View *self, maps_MapType t) {}  // OpenStreetMap's streets only
static void set_shows_user(maps_View *self, bool on) {}

// measure: none of its own; its style gives it a size.
static bool measure(maps_View *self, float w, float h, float *ow, float *oh) {
	*ow = isnan(w) ? 0 : w;
	*oh = isnan(h) ? 0 : h;
	return true;
}

static void destroy(maps_View *self) {
	view *v = (view *)self;
	v->destroyed = true;
	unref(v);
}

static const maps_View_vtable vt = {
	.destroy = destroy,
	.set_region = set_region,
	.set_markers = set_markers,
	.set_mapType = set_map_type,
	.set_showsUser = set_shows_user,
	.set_interactive = set_interactive,
	.measure = measure,
};

maps_View *maps_make_View(maps_View_events events) {
	view *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	v->events = events;
	v->refs = 1;
	v->interactive = true;
	if (!shumate()) {
		if (G(gtk_label_new)) {
			GtkWidget *l = G(gtk_label_new)("Maps need libshumate");
			G(gtk_widget_add_css_class)(l, "dim-label");
			if (G(gtk_label_set_wrap)) G(gtk_label_set_wrap)((GtkLabel *)l, true);
			v->base.widget = l;
		}
		asuka_desktop_post(emit_error, ref(v));
		return &v->base;
	}
	v->sm = G(shumate_simple_map_new)();
	v->base.widget = v->sm;
	gpointer reg = G(shumate_map_source_registry_new_with_defaults)();
	gpointer src = G(shumate_map_source_registry_get_by_id)(reg, "osm-mapnik");
	if (src) G(shumate_simple_map_set_map_source)(v->sm, src);
	G(g_object_unref)(reg);
	G(shumate_simple_map_set_show_zoom_buttons)(v->sm, false);
	v->map = G(shumate_simple_map_get_map)(v->sm);
	v->vp = G(shumate_simple_map_get_viewport)(v->sm);
	v->layer = G(shumate_marker_layer_new)(v->vp);
	G(shumate_simple_map_add_overlay_layer)(v->sm, v->layer);
	connect(v->vp, "notify::latitude", (GCallback)moved, v);
	connect(v->vp, "notify::longitude", (GCallback)moved, v);
	connect(v->vp, "notify::zoom-level", (GCallback)moved, v);
	gpointer click = G(gtk_gesture_click_new)();
	connect(click, "released", (GCallback)clicked, v);
	G(gtk_widget_add_controller)(v->map, click);
	return &v->base;
}
