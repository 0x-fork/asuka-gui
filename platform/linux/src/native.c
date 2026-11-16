// native.c: Native views (ui.Native): a native package's component. Its
// Type prop names it ("<path>#<Name>"); the package registry
// (asuka_desktop.h) makes its GtkWidget, which is the node's control: its
// view holds it and keeps it filling it as the core places it. Its props
// (the Prop op as it came) go to the package's set; its events come back
// through the registry (asuka_native_emit); it's measured by its package
// where the package measures it, else as GTK measures the widget.
#include "host.h"

#include <math.h>
#include <stdlib.h>

typedef struct {
	char *kind;
	void *widget;
} native_view;

static native_view *nv_of(node *n) { return n->kind == AK_KIND_NATIVE ? n->ext : NULL; }

static void free_nv(node *n) {
	native_view *v = n->ext;
	if (v->widget) asuka_native_view_destroy(v->kind, v->widget);
	free(v->kind);
	free(v);
	n->ext = NULL;
}

static bool nv_create(node *n) {
	if (n->kind != AK_KIND_NATIVE) return false;
	native_view *v = calloc(1, sizeof *v);
	if (!v) abort();
	n->ext = v;  // the package's widget comes with Type
	n->ext_free = free_nv;
	return true;
}

static bool nv_prop(node *n, ak_prop p, const ak_value *val) {
	native_view *v = nv_of(n);
	if (!v) return false;
	switch (p) {
	case AK_PROP_NATIVE_TYPE: {
		if (v->kind || !n->widget) return true;
		v->kind = str_dup(val->s);
		GtkWidget *w = asuka_native_view(v->kind, n->id, n->widget);
		if (!w) return true;
		v->widget = w;
		if (gtk_widget_get_parent(w) == n->widget) {
			n->inner = w;  // the package put it in its parent itself
			gtk_widget_queue_allocate(n->widget);
		} else {
			lx_detach(w);
			lx_set_inner(n, w);  // the node's view takes it (and its floating reference)
		}
		return true;
	}
	case AK_PROP_NATIVE_PROP:
		if (v->widget) asuka_native_view_prop(v->widget, (const uint8_t *)val->s.p, val->s.len);
		return true;
	default:
		return false;
	}
}

// measure asks the package; one that doesn't measure its widget leaves it
// to the host's measuring (GTK's).
static bool nv_measure(node *n, float cw, float ch, float aw, float ah, float *ow, float *oh) {
	native_view *v = nv_of(n);
	if (!v || !v->widget) return false;
	float w = cw == cw ? cw : aw < AK_BIG && aw > -AK_INF ? aw : NAN;
	float h = ch == ch ? ch : NAN;
	(void)ah;
	return asuka_native_view_measure(v->widget, w, h, ow, oh);
}

const area lx_area_native = {
	.name = "native",
	.create = nv_create,
	.prop = nv_prop,
	.measure = nv_measure,
};
