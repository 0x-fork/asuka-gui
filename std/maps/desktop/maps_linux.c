// maps.View on Linux: libshumate's ShumateSimpleMap (GNOME Maps' map),
// showing OpenStreetMap's tiles; Satellite and Hybrid have no free
// imagery source: they show the streets too, as Muted does. showsUser
// has no location layer: nothing's shown. Its events: regionChange once
// the user moved it, markerPress, press (a click off the markers), error
// (no libshumate, which the view then says instead of a map). It has no
// size of its own: its style gives it one.
//
// libshumate (and the few GTK functions only the map uses) are resolved
// here, with dlopen and dlsym, as asuka_linux.h resolves GTK's.
#include "asuka_linux.h"
#include "maps.asuka.h"

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _GParamSpec GParamSpec;

static struct {
	bool tried;
	char *err;  // why there's no map
	// libshumate
	GtkWidget *(*simple_map_new)(void);
	void (*simple_map_set_map_source)(GtkWidget *m, gpointer source);
	GtkWidget *(*simple_map_get_map)(GtkWidget *m);
	gpointer (*simple_map_get_viewport)(GtkWidget *m);
	void (*simple_map_add_overlay_layer)(GtkWidget *m, GtkWidget *layer);
	void (*simple_map_set_show_zoom_buttons)(GtkWidget *m, gboolean v);
	void (*map_center_on)(GtkWidget *m, double lat, double lng);
	void (*viewport_set_zoom_level)(gpointer vp, double z);
	void (*viewport_widget_coords_to_location)(gpointer vp, GtkWidget *w, double x, double y, double *lat, double *lng);
	double (*location_get_latitude)(gpointer loc);
	double (*location_get_longitude)(gpointer loc);
	void (*location_set_location)(gpointer loc, double lat, double lng);
	gpointer (*registry_new_with_defaults)(void);
	gpointer (*registry_get_by_id)(gpointer reg, const char *id);
	GtkWidget *(*marker_layer_new)(gpointer vp);
	void (*marker_layer_add_marker)(GtkWidget *layer, GtkWidget *marker);
	void (*marker_layer_remove_all)(GtkWidget *layer);
	GtkWidget *(*marker_new)(void);
	void (*marker_set_child)(GtkWidget *marker, GtkWidget *child);
	// GTK
	GtkWidget *(*label_new)(const char *s);
	void (*label_set_wrap)(GtkWidget *l, gboolean v);
	gpointer (*gesture_click_new)(void);
	void (*widget_add_controller)(GtkWidget *w, gpointer controller);
	int (*widget_get_width)(GtkWidget *w);
	int (*widget_get_height)(GtkWidget *w);
	void (*widget_set_can_target)(GtkWidget *w, gboolean v);
	void (*widget_set_tooltip_text)(GtkWidget *w, const char *s);
	GtkWidget *(*image_new_from_icon_name)(const char *name);
	void (*image_set_pixel_size)(GtkWidget *i, int size);
	gpointer (*css_provider_new)(void);
	void (*css_provider_load_from_string)(gpointer p, const char *css);       // GTK 4.12
	void (*css_provider_load_from_data)(gpointer p, const char *css, gssize n);  // before
	void (*style_context_add_provider_for_display)(GdkDisplay *d, gpointer p, guint priority);
} S;

#define BIND(lib, field, name) *(void **)&S.field = dlsym(lib, name)

// shumate loads libshumate, once: false (S.err why) when it isn't there.
static bool shumate(void) {
	if (S.tried) return !S.err;
	S.tried = true;
	if (!asuka_linux() || !G(gtk_widget_add_css_class)) {
		S.err = asuka_strdup("maps need GTK 4");
		return false;
	}
	void *gtk = RTLD_DEFAULT;  // libgtk-4, loaded global by asuka_linux()
	BIND(gtk, label_new, "gtk_label_new");
	BIND(gtk, label_set_wrap, "gtk_label_set_wrap");
	BIND(gtk, gesture_click_new, "gtk_gesture_click_new");
	BIND(gtk, widget_add_controller, "gtk_widget_add_controller");
	BIND(gtk, widget_get_width, "gtk_widget_get_width");
	BIND(gtk, widget_get_height, "gtk_widget_get_height");
	BIND(gtk, widget_set_can_target, "gtk_widget_set_can_target");
	BIND(gtk, widget_set_tooltip_text, "gtk_widget_set_tooltip_text");
	BIND(gtk, image_new_from_icon_name, "gtk_image_new_from_icon_name");
	BIND(gtk, image_set_pixel_size, "gtk_image_set_pixel_size");
	BIND(gtk, css_provider_new, "gtk_css_provider_new");
	BIND(gtk, css_provider_load_from_string, "gtk_css_provider_load_from_string");
	BIND(gtk, css_provider_load_from_data, "gtk_css_provider_load_from_data");
	BIND(gtk, style_context_add_provider_for_display, "gtk_style_context_add_provider_for_display");
	void *lib = dlopen("libshumate-1.0.so.1", RTLD_NOW);
	if (!lib) lib = dlopen("libshumate-1.0.so", RTLD_NOW);
	if (!lib) {
		const char *why = dlerror();
		S.err = asuka_sprintf("maps need libshumate (libshumate-1.0-1): %s", why ? why : "not found");
		return false;
	}
	BIND(lib, simple_map_new, "shumate_simple_map_new");
	BIND(lib, simple_map_set_map_source, "shumate_simple_map_set_map_source");
	BIND(lib, simple_map_get_map, "shumate_simple_map_get_map");
	BIND(lib, simple_map_get_viewport, "shumate_simple_map_get_viewport");
	BIND(lib, simple_map_add_overlay_layer, "shumate_simple_map_add_overlay_layer");
	BIND(lib, simple_map_set_show_zoom_buttons, "shumate_simple_map_set_show_zoom_buttons");
	BIND(lib, map_center_on, "shumate_map_center_on");
	BIND(lib, viewport_set_zoom_level, "shumate_viewport_set_zoom_level");
	BIND(lib, viewport_widget_coords_to_location, "shumate_viewport_widget_coords_to_location");
	BIND(lib, location_get_latitude, "shumate_location_get_latitude");
	BIND(lib, location_get_longitude, "shumate_location_get_longitude");
	BIND(lib, location_set_location, "shumate_location_set_location");
	BIND(lib, registry_new_with_defaults, "shumate_map_source_registry_new_with_defaults");
	BIND(lib, registry_get_by_id, "shumate_map_source_registry_get_by_id");
	BIND(lib, marker_layer_new, "shumate_marker_layer_new");
	BIND(lib, marker_layer_add_marker, "shumate_marker_layer_add_marker");
	BIND(lib, marker_layer_remove_all, "shumate_marker_layer_remove_all");
	BIND(lib, marker_new, "shumate_marker_new");
	BIND(lib, marker_set_child, "shumate_marker_set_child");
	void **need[] = {(void **)&S.simple_map_new, (void **)&S.simple_map_set_map_source, (void **)&S.simple_map_get_map,
	                 (void **)&S.simple_map_get_viewport, (void **)&S.simple_map_add_overlay_layer, (void **)&S.simple_map_set_show_zoom_buttons,
	                 (void **)&S.map_center_on, (void **)&S.viewport_set_zoom_level, (void **)&S.viewport_widget_coords_to_location,
	                 (void **)&S.location_get_latitude, (void **)&S.location_get_longitude, (void **)&S.location_set_location,
	                 (void **)&S.registry_new_with_defaults, (void **)&S.registry_get_by_id, (void **)&S.marker_layer_new,
	                 (void **)&S.marker_layer_add_marker, (void **)&S.marker_layer_remove_all, (void **)&S.marker_new,
	                 (void **)&S.marker_set_child, (void **)&S.label_new, (void **)&S.gesture_click_new, (void **)&S.widget_add_controller,
	                 (void **)&S.widget_get_width, (void **)&S.widget_get_height, (void **)&S.widget_set_can_target,
	                 (void **)&S.widget_set_tooltip_text, (void **)&S.image_new_from_icon_name, (void **)&S.image_set_pixel_size,
	                 (void **)&S.css_provider_new, (void **)&S.style_context_add_provider_for_display};
	for (size_t i = 0; i < sizeof need / sizeof *need; i++)
		if (!*need[i]) {
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
	G(g_signal_connect_data)(obj, signal, cb, ref(v), unref_closure, 0);
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
	if (S.widget_get_width(v->map) > 0) {
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
	double w = S.widget_get_width(v->map), h = S.widget_get_height(v->map);
	if (w <= 0 || h <= 0) {
		if (!v->ticking) {
			v->ticking = true;
			G(gtk_widget_add_tick_callback)(v->sm, tick, ref(v), unref_notify);
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
	S.viewport_set_zoom_level(v->vp, fmax(zoom, 0));
	S.map_center_on(v->map, r.center.lat, r.center.lng);
	v->setting = false;
}

// report tells the app the region the user moved the map to, once the
// loop's idle.
static void report(void *p) {
	view *v = p;
	v->reporting = false;
	if (!v->destroyed) {
		double w = S.widget_get_width(v->map), h = S.widget_get_height(v->map);
		double lat0, lng0, lat1, lng1;
		S.viewport_widget_coords_to_location(v->vp, v->map, 0, 0, &lat0, &lng0);
		S.viewport_widget_coords_to_location(v->vp, v->map, w, h, &lat1, &lng1);
		double dl = lng1 - lng0;
		if (dl < 0) dl += 360;
		maps_Region r = {{S.location_get_latitude(v->vp), S.location_get_longitude(v->vp)}, fabs(lat0 - lat1), dl};
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
	S.viewport_widget_coords_to_location(v->vp, v->map, x, y, &at.lat, &at.lng);
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
			pins.provider = S.css_provider_new();
			S.style_context_add_provider_for_display(d, pins.provider, 700);
		}
		char *css = asuka_strndup((const char *)pins.css.data, pins.css.len);
		if (S.css_provider_load_from_string) S.css_provider_load_from_string(pins.provider, css);
		else if (S.css_provider_load_from_data) S.css_provider_load_from_data(pins.provider, css, -1);
		free(css);
	}
	G(gtk_widget_add_css_class)(image, class);
}

static void set_markers(maps_View *self, maps_list_Marker ms) {
	view *v = (view *)self;
	if (!v->sm) return;
	S.marker_layer_remove_all(v->layer);
	for (size_t i = 0; i < ms.len; i++) {
		const maps_Marker *mk = &ms.data[i];
		GtkWidget *marker = S.marker_new();
		GtkWidget *image = S.image_new_from_icon_name("mark-location-symbolic");
		S.image_set_pixel_size(image, 32);
		if (mk->color) color_pin(image, asuka_desktop_color((uint64_t)*mk->color));
		else G(gtk_widget_add_css_class)(image, "accent");
		const char *t = mk->title ? mk->title : "", *s = mk->subtitle ? mk->subtitle : "";
		if (*t || *s) {
			char *tip = *t && *s ? asuka_sprintf("%s\n%s", t, s) : asuka_strdup(*t ? t : s);
			S.widget_set_tooltip_text(image, tip);
			free(tip);
		}
		pin *p = malloc(sizeof *p);
		*p = (pin){ref(v), asuka_strdup(mk->id)};
		gpointer g = S.gesture_click_new();
		G(g_signal_connect_data)(g, "released", (GCallback)pin_clicked, p, pin_free, 0);
		S.widget_add_controller(image, g);
		S.marker_set_child(marker, image);
		S.location_set_location(marker, mk->at.lat, mk->at.lng);
		S.marker_layer_add_marker(v->layer, marker);
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
	if (v->sm) S.widget_set_can_target(v->sm, on);
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
		if (S.label_new) {
			GtkWidget *l = S.label_new("Maps need libshumate");
			G(gtk_widget_add_css_class)(l, "dim-label");
			if (S.label_set_wrap) S.label_set_wrap(l, true);
			v->base.widget = l;
		}
		asuka_desktop_post(emit_error, ref(v));
		return &v->base;
	}
	v->sm = S.simple_map_new();
	v->base.widget = v->sm;
	gpointer reg = S.registry_new_with_defaults();
	gpointer src = S.registry_get_by_id(reg, "osm-mapnik");
	if (src) S.simple_map_set_map_source(v->sm, src);
	G(g_object_unref)(reg);
	S.simple_map_set_show_zoom_buttons(v->sm, false);
	v->map = S.simple_map_get_map(v->sm);
	v->vp = S.simple_map_get_viewport(v->sm);
	v->layer = S.marker_layer_new(v->vp);
	S.simple_map_add_overlay_layer(v->sm, v->layer);
	connect(v->vp, "notify::latitude", (GCallback)moved, v);
	connect(v->vp, "notify::longitude", (GCallback)moved, v);
	connect(v->vp, "notify::zoom-level", (GCallback)moved, v);
	gpointer click = S.gesture_click_new();
	connect(click, "released", (GCallback)clicked, v);
	S.widget_add_controller(v->map, click);
	return &v->base;
}
