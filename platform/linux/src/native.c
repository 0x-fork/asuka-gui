// native.c: Native views (ui.Native): a native package's component. Its
// Type prop names it ("<path>#<Name>"); the package registry
// (asuka_desktop.h) makes its GtkWidget, which is the node's control: its
// view holds it and keeps it filling it as the core places it. Its props
// (the Prop op: string name, u8 type, u32 length, the encoded value) go to
// the package's set; its events come back through asuka_native_emit; it's
// measured as GTK measures the widget.
#include "host.h"

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
	case AK_PROP_NATIVE_PROP: {
		if (!v->widget) return true;
		ak_reader r = {(const uint8_t *)val->s.p, val->s.len, 0, false};
		uint32_t nl;
		const uint8_t *name = ak_read_bytes(&r, &nl);
		ak_read_u8(&r);  // the value's type: its bytes, encoded
		uint32_t vl;
		const uint8_t *value = ak_read_bytes(&r, &vl);
		if (r.err) return true;
		char *prop = str_dup(ak_strn((const char *)name, nl));
		asuka_native_view_set(v->kind, v->widget, prop, value, vl);
		free(prop);
		return true;
	}
	default:
		return false;
	}
}

const area lx_area_native = {
	.name = "native",
	.create = nv_create,
	.prop = nv_prop,
};
