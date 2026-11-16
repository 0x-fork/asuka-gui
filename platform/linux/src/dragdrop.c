// dragdrop.c: drag and drop (DESIGN.md, "Interaction states, drag and
// drop"), with GTK 4's own.
//
//   - A view with DragItems has a GtkDragSource: a move with the button
//     down past the drag threshold (a touchscreen's too) starts a drag of
//     a content provider of its items: its files (a GdkFileList, which
//     other apps get as text/uri-list), its texts (a string; its links'
//     URLs if it has no text), its links (text/uri-list), and a private
//     type of the app's, the process's and the view's IDs, so a drag of
//     only values is offered anyway, but no other app takes it. The drag
//     shows the view's image as it lifts. Dragging: Began as it starts,
//     then Dropped (a target took it) or Cancelled.
//   - A view with DropAccept has a GtkDropTargetAsync; GTK offers a drag
//     to the innermost widget under the pointer whose target accepts it,
//     then its ancestors'. A drag is taken if one of its items' MIME types
//     matches a pattern: the app's own drag (gdk_drop_get_drag is the drag
//     the app started) by its items' types; another app's by its text
//     (text/plain), and its URIs, read as it comes over the app (a
//     GdkFileList): files by their names' types, others as links
//     (text/uri-list). DragOver as it comes, moves and leaves; Drop with
//     the source view and no items for the app's own drop (the core has
//     them), with the items the target takes for another app's (its
//     files the files themselves), once they're read.
#include "host.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

void lx_drag_took(node *n);

// The private type the app's drags carry.
#define DRAG_TYPE_LOCAL "application/x-asuka-drag"

typedef struct {
	ak_buf bytes;  // the DragItems prop, which items points into
	ak_item_vec items;
} item_list;

struct dnd_state {
	// A drag source's.
	GtkDragSource *source;
	item_list drag;
	double at[2];  // where the drag was grabbed, in the view
	bool cancelled;
	// A drop target's.
	GtkDropTargetAsync *target;
	ak_buf accept_bytes;
	ak_str_vec accept;
	GdkDrop *over;  // the drop over it (NULL: none)
	float over_at[2];  // its place in the view, last reported
};

// What a drag over the app carries, read from its GdkDrop.
typedef AK_VEC(char *) str_list;
typedef struct drop_read drop_read;
typedef struct {
	ak_id target;
	double x, y;
} drop_wait;
struct drop_read {
	GdkDrop *drop;  // a reference
	bool local;
	str_list mimes;  // its items' MIME types, as far as known
	bool has_text, has_uris;
	bool loading;  // its URIs are being read
	str_list files;
	str_list links;
	AK_VEC(drop_wait) waiting;  // drops waiting for the URIs
	bool forgotten;
};

static AK_VEC(drop_read *) reads;

// The app's drag under way.
static struct {
	GdkDrag *drag;
	ak_id source;
	item_list items;
} app_drag;

static void items_set(item_list *l, ak_span b) {
	ak_buf_reset(&l->bytes);
	ak_buf_put(&l->bytes, b.p, b.len);
	l->items.len = 0;
	ak_decode_drag_items(l->bytes.data, l->bytes.len, &l->items);
}

static void items_free(item_list *l) {
	ak_buf_free(&l->bytes);
	ak_vec_free(&l->items);
}

static dnd_state *dnd_of(node *n) {
	if (!n->dnd) {
		n->dnd = calloc(1, sizeof *n->dnd);
		if (!n->dnd) abort();
	}
	return n->dnd;
}

// lx_draggable reports whether a drag of n starts a drag and drop (which
// comes before a pan).
bool lx_draggable(node *n) { return n->dnd && n->dnd->source; }

static void free_strs(str_list *s) {
	for (uint32_t i = 0; i < s->len; i++) free(s->data[i]);
	s->len = 0;
}

// ---- Drag sources ----

static GdkContentProvider *provider_bytes(const char *mime, const void *p, size_t n) {
	GBytes *b = g_bytes_new(p, n);
	GdkContentProvider *c = gdk_content_provider_new_for_bytes(mime, b);
	g_bytes_unref(b);
	return c;
}

// drag_content is the content of a drag of view id's items: its files,
// its texts (its links' if it has none), its links, and the app's private
// type. An item that's only a value goes nowhere else.
static GdkContentProvider *drag_content(ak_id id, const ak_item_vec *items) {
	GdkContentProvider *ps[4];
	int np = 0;
	AK_VEC(GFile *) files = {0};
	ak_buf links = {0}, texts = {0}, link_text = {0};
	for (uint32_t i = 0; i < items->len; i++) {
		const ak_item *it = &items->data[i];
		if (it->path.len) {
			char *p = str_dup(it->path);
			ak_vec_push(&files, g_file_new_for_path(p));
			free(p);
		} else if (it->kind == AK_SHARE_KIND_LINK && it->text.len) {
			ak_buf_put(&links, it->text.p, it->text.len);
			ak_buf_put(&links, "\r\n", 2);
			if (link_text.len) ak_buf_u8(&link_text, '\n');
			ak_buf_put(&link_text, it->text.p, it->text.len);
		} else if (it->text.len) {
			if (texts.len) ak_buf_u8(&texts, '\n');
			ak_buf_put(&texts, it->text.p, it->text.len);
		}
	}
	if (files.len) {
		GdkFileList *list = gdk_file_list_new_from_array(files.data, files.len);
		for (uint32_t i = 0; i < files.len; i++) g_object_unref(files.data[i]);
		GValue v = {0};
		g_value_init(&v, gdk_file_list_get_type());
		g_value_take_boxed(&v, list);
		ps[np++] = gdk_content_provider_new_for_value(&v);
		g_value_unset(&v);
	} else if (links.len) {
		ps[np++] = provider_bytes("text/uri-list", links.data, links.len);
	}
	ak_buf *text = texts.len ? &texts : &link_text;
	if (text->len) {
		GValue v = {0};
		g_value_init(&v, G_TYPE_STRING);
		g_value_set_string(&v, lx_buf_cstr(text));
		ps[np++] = gdk_content_provider_new_for_value(&v);
		g_value_unset(&v);
	}
	char local[48];
	int k = snprintf(local, sizeof local, "%d %u", (int)getpid(), (unsigned)id);
	ps[np++] = provider_bytes(DRAG_TYPE_LOCAL, local, (size_t)k);
	ak_vec_free(&files);
	ak_buf_free(&links);
	ak_buf_free(&texts);
	ak_buf_free(&link_text);
	return gdk_content_provider_new_union(ps, (gsize)np);
}

static GdkContentProvider *on_prepare(GtkDragSource *src, double x, double y, node *n) {
	(void)src;
	dnd_state *s = n->dnd;
	if (!s || !s->source) return NULL;
	s->at[0] = x;
	s->at[1] = y;
	return drag_content(n->id, &s->drag.items);
}

static void on_drag_begin(GtkDragSource *src, GdkDrag *drag, node *n) {
	(void)src;
	dnd_state *s = n->dnd;
	app_drag.drag = drag;
	app_drag.source = n->id;
	items_set(&app_drag.items, ak_strn((const char *)s->drag.bytes.data, s->drag.bytes.len));
	s->cancelled = false;
	// The view's image as it is now (its Dragging style comes after).
	if (n->widget) {
		GdkPaintable *live = gtk_widget_paintable_new(n->widget);
		GdkPaintable *img = gdk_paintable_get_current_image(live);
		g_object_unref(live);
		if (img) {
			gtk_drag_source_set_icon(s->source, img, (int)s->at[0], (int)s->at[1]);
			g_object_unref(img);
		}
	}
	lx_enter_begin();
	lx_drag_took(n);
	lx_emit(n, (ak_event){.kind = AK_EVENT_DRAGGING, .i = AK_DRAG_PHASE_BEGAN});
	lx_enter_end();
}

static gboolean on_drag_cancel(GtkDragSource *src, GdkDrag *drag, int reason, node *n) {
	(void)src;
	(void)drag;
	(void)reason;
	n->dnd->cancelled = true;
	return 0;
}

static void on_drag_end(GtkDragSource *src, GdkDrag *drag, gboolean delete_data, node *n) {
	(void)src;
	(void)delete_data;
	int phase = n->dnd->cancelled || !gdk_drag_get_selected_action(drag) ? AK_DRAG_PHASE_CANCELLED : AK_DRAG_PHASE_DROPPED;
	if (app_drag.drag == drag) {
		app_drag.drag = NULL;
		app_drag.source = 0;
		app_drag.items.items.len = 0;
	}
	if (n->dead) return;  // the drop may have removed it
	lx_enter_begin();
	lx_emit(n, (ak_event){.kind = AK_EVENT_DRAGGING, .i = phase});
	lx_enter_end();
}

static void set_drag_items(node *n, ak_span b) {
	dnd_state *s = dnd_of(n);
	items_set(&s->drag, b);
	if (!s->drag.items.len || !n->widget) {
		if (s->source) {
			gtk_widget_remove_controller(n->widget, (GtkEventController *)s->source);
			s->source = NULL;
		}
		return;
	}
	if (!s->source) {
		s->source = gtk_drag_source_new();
		lx_connect(n, s->source, "prepare", G_CALLBACK(on_prepare));
		lx_connect(n, s->source, "drag-begin", G_CALLBACK(on_drag_begin));
		lx_connect(n, s->source, "drag-cancel", G_CALLBACK(on_drag_cancel));
		lx_connect(n, s->source, "drag-end", G_CALLBACK(on_drag_end));
		gtk_widget_add_controller(n->widget, (GtkEventController *)s->source);
	}
	// The files the app shares aren't moved away from it.
	GdkDragAction actions = GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK;
	for (uint32_t i = 0; i < s->drag.items.len; i++)
		if (s->drag.items.data[i].path.len) actions = GDK_ACTION_COPY | GDK_ACTION_LINK;
	gtk_drag_source_set_actions(s->source, actions);
}

// ---- Drop targets ----

// lx_file_mime is a file's MIME type from its name (shared-mime-info's).
char *lx_file_mime(const char *path) {
	gboolean uncertain;
	char *t = g_content_type_guess(path, NULL, 0, &uncertain);
	char *out = str_dup(ak_cstr(t && *t && strcmp(t, "application/x-zerosize") ? t : "application/octet-stream"));
	g_free(t);
	return out;
}

static int file_share_kind(const char *m) {
	if (!strncmp(m, "image/", 6)) return AK_SHARE_KIND_IMAGE;
	if (!strncmp(m, "video/", 6)) return AK_SHARE_KIND_VIDEO;
	if (!strncmp(m, "audio/", 6)) return AK_SHARE_KIND_AUDIO;
	return AK_SHARE_KIND_FILE;
}

// describe sets another app's drag's MIME types from what's known.
static void describe(drop_read *r) {
	free_strs(&r->mimes);
	for (uint32_t i = 0; i < r->files.len; i++) ak_vec_push(&r->mimes, lx_file_mime(r->files.data[i]));
	if (r->links.len) ak_vec_push(&r->mimes, str_dup(ak_cstr("text/uri-list")));
	if (r->has_text) ak_vec_push(&r->mimes, str_dup(ak_cstr("text/plain")));
}

static bool takes(drop_read *r, const ak_str_vec *accept) {
	for (uint32_t i = 0; i < r->mimes.len; i++)
		if (ak_match_mime(accept->data, accept->len, ak_cstr(r->mimes.data[i]))) return true;
	return false;
}

static void free_read(drop_read *r) {
	free_strs(&r->mimes);
	free_strs(&r->files);
	free_strs(&r->links);
	ak_vec_free(&r->mimes);
	ak_vec_free(&r->files);
	ak_vec_free(&r->links);
	ak_vec_free(&r->waiting);
	g_object_unref(r->drop);
	free(r);
}

static void dropped_now(node *n, drop_read *r, double x, double y);

static void on_uris_read(GObject *source, GAsyncResult *res, gpointer data) {
	(void)source;
	drop_read *r = data;
	GError *err = NULL;
	const GValue *v = gdk_drop_read_value_finish(r->drop, res, &err);
	if (err) g_error_free(err);
	if (v) {
		GdkFileList *list = g_value_get_boxed(v);
		if (list) {
			GSList *l = gdk_file_list_get_files(list);
			for (GSList *it = l; it; it = it->next) {
				char *p = g_file_get_path(it->data);
				if (p) {
					ak_vec_push(&r->files, str_dup(ak_cstr(p)));
				} else {
					char *u = g_file_get_uri(it->data);
					if (u) ak_vec_push(&r->links, str_dup(ak_cstr(u)));
					g_free(u);
				}
				g_free(p);
			}
			g_slist_free(l);
		}
	}
	r->loading = false;
	describe(r);
	if (r->forgotten) {
		free_read(r);
		return;
	}
	lx_enter_begin();
	for (uint32_t i = 0; i < r->waiting.len; i++) {
		drop_wait w = r->waiting.data[i];
		node *n = node_get(w.target);
		if (n && n->dnd && n->dnd->target) {
			dropped_now(n, r, w.x, w.y);
		} else {
			gdk_drop_finish(r->drop, 0);
		}
	}
	r->waiting.len = 0;
	lx_enter_end();
}

// read_drop is what a drag carries: the app's own drag's items' types, or
// another app's formats, its URIs read as it comes.
static drop_read *read_drop(GdkDrop *drop) {
	for (uint32_t i = 0; i < reads.len; i++)
		if (reads.data[i]->drop == drop) return reads.data[i];
	drop_read *r = calloc(1, sizeof *r);
	if (!r) abort();
	r->drop = g_object_ref(drop);
	ak_vec_push(&reads, r);
	GdkContentFormats *formats = gdk_drop_get_formats(drop);
	GdkDrag *d = gdk_drop_get_drag(drop);
	if (app_drag.drag && (d == app_drag.drag || (!d && gdk_content_formats_contain_mime_type(formats, DRAG_TYPE_LOCAL)))) {
		r->local = true;
		for (uint32_t i = 0; i < app_drag.items.items.len; i++) ak_vec_push(&r->mimes, str_dup(app_drag.items.items.data[i].mime));
		return r;
	}
	gsize nm = 0;
	const char *const *mimes = gdk_content_formats_get_mime_types(formats, &nm);
	for (gsize i = 0; i < nm; i++) {
		if (!strncmp(mimes[i], "text/plain", 10)) r->has_text = true;
		if (!strcmp(mimes[i], "text/uri-list")) r->has_uris = true;
	}
	if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING)) r->has_text = true;
	if (gdk_content_formats_contain_gtype(formats, gdk_file_list_get_type())) r->has_uris = true;
	describe(r);
	if (r->has_uris) {
		r->loading = true;
		gdk_drop_read_value_async(drop, gdk_file_list_get_type(), G_PRIORITY_DEFAULT, NULL, on_uris_read, r);
	}
	return r;
}

static bool target_has(GdkDrop *drop);

// forget_soon forgets what a drag carried once no target has it (it left
// them all, or was dropped).
static gboolean forget_idle(gpointer data) {
	GdkDrop *drop = data;
	if (!target_has(drop)) {
		for (uint32_t i = 0; i < reads.len; i++) {
			drop_read *r = reads.data[i];
			if (r->drop != drop || r->waiting.len) continue;
			reads.data[i] = reads.data[--reads.len];
			if (r->loading)
				r->forgotten = true;  // freed as its read ends
			else
				free_read(r);
			break;
		}
	}
	g_object_unref(drop);
	return 0;
}

static void forget_soon(GdkDrop *drop) { g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, forget_idle, g_object_ref(drop), NULL); }

static AK_VEC(node *) targets;  // the views with a drop target

static bool target_has(GdkDrop *drop) {
	for (uint32_t i = 0; i < targets.len; i++)
		if (targets.data[i]->dnd->over == drop) return true;
	return false;
}

static void emit_over(node *n, int state, float x, float y) {
	lx_emit(n, (ak_event){.kind = AK_EVENT_DRAG_OVER, .g = {.state = (uint8_t)state, .x = x, .y = y, .scale = 1}});
}

// leave ends the drag over n: Cancelled.
static void leave(node *n) {
	dnd_state *t = n->dnd;
	if (!t || !t->over) return;
	t->over = NULL;
	emit_over(n, AK_GESTURE_STATE_CANCELLED, t->over_at[0], t->over_at[1]);
}

static GdkDragAction drop_action(drop_read *r, GdkDragAction allowed) {
	GdkDragAction prefer[3] = {GDK_ACTION_COPY, GDK_ACTION_MOVE, GDK_ACTION_LINK};
	if (r->local) {
		prefer[0] = GDK_ACTION_MOVE;
		prefer[1] = GDK_ACTION_COPY;
	}
	for (int i = 0; i < 3; i++)
		if (allowed & prefer[i]) return prefer[i];
	return 0;
}

static gboolean on_accept(GtkDropTargetAsync *tg, GdkDrop *drop, node *n) {
	(void)tg;
	dnd_state *t = n->dnd;
	if (!t || !t->target) return 0;
	drop_read *r = read_drop(drop);
	if (takes(r, &t->accept) || r->loading) return 1;
	forget_soon(drop);
	return 0;
}

// over reports a drag over n at x, y (Began as it comes, Changed as it
// moves) and returns the action a drop there would take: a move for the
// app's own drags, a copy for other apps', else what the source allows;
// none if n doesn't take it.
static GdkDragAction on_over(GtkDropTargetAsync *tg, GdkDrop *drop, double x, double y, node *n) {
	(void)tg;
	dnd_state *t = n->dnd;
	drop_read *r = read_drop(drop);
	lx_enter_begin();
	GdkDragAction a = 0;
	if (!t || !t->target || !takes(r, &t->accept)) {
		leave(n);
	} else {
		float at[2] = {(float)x, (float)y};
		if (t->over != drop) {
			t->over = drop;
			t->over_at[0] = at[0];
			t->over_at[1] = at[1];
			emit_over(n, AK_GESTURE_STATE_BEGAN, at[0], at[1]);
		} else if (at[0] != t->over_at[0] || at[1] != t->over_at[1]) {
			t->over_at[0] = at[0];
			t->over_at[1] = at[1];
			emit_over(n, AK_GESTURE_STATE_CHANGED, at[0], at[1]);
		}
		a = drop_action(r, gdk_drop_get_actions(drop));
	}
	lx_enter_end();
	return a;
}

static void on_leave(GtkDropTargetAsync *tg, GdkDrop *drop, node *n) {
	(void)tg;
	lx_enter_begin();
	leave(n);
	lx_enter_end();
	forget_soon(drop);
}

static void emit_drop(node *n, const ak_drop *d, const ak_item *items, uint32_t count) {
	ak_buf b = {0};
	ak_append_drop(&b, d, items, count);
	lx_emit(n, (ak_event){.kind = AK_EVENT_DROP, .s = ak_strn((const char *)b.data, b.len)});
	ak_buf_free(&b);
}

// The items of another app's drop that a target takes: its files, its
// links, and its text, read as the drop comes.
typedef struct {
	ak_id target;
	drop_read *r;
	GdkDrop *drop;
	GdkDragAction action;
	ak_drop d;
	AK_VEC(ak_item) items;
	str_list owned;
} drop_items;

static void finish_items(drop_items *x) {
	gdk_drop_finish(x->drop, x->action);
	forget_soon(x->drop);
	node *n = node_get(x->target);
	if (n) {
		lx_enter_begin();
		emit_drop(n, &x->d, x->items.data, x->items.len);
		lx_enter_end();
	}
	free_strs(&x->owned);
	ak_vec_free(&x->owned);
	ak_vec_free(&x->items);
	g_object_unref(x->drop);
	free(x);
}

// is_file_uris reports whether a text is a file manager's list of the
// files it drags, which it offers as text too.
static bool is_file_uris(const char *s) {
	bool any = false;
	for (const char *p = s; *p;) {
		while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
		if (!*p) break;
		if (strncmp(p, "file://", 7) && *p != '/') return false;
		any = true;
		while (*p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
	}
	return any;
}

static void on_text_read(GObject *source, GAsyncResult *res, gpointer data) {
	(void)source;
	drop_items *x = data;
	GError *err = NULL;
	const GValue *v = gdk_drop_read_value_finish(x->drop, res, &err);
	if (err) g_error_free(err);
	const char *s = v ? g_value_get_string(v) : NULL;
	if (s && *s) {
		// A browser's link comes as its text too: once.
		ak_span trimmed = ak_cstr(s);
		while (trimmed.len && (trimmed.p[0] == ' ' || trimmed.p[0] == '\n' || trimmed.p[0] == '\r')) trimmed.p++, trimmed.len--;
		while (trimmed.len && (trimmed.p[trimmed.len - 1] == ' ' || trimmed.p[trimmed.len - 1] == '\n' || trimmed.p[trimmed.len - 1] == '\r')) trimmed.len--;
		bool dup = false;
		for (uint32_t i = 0; i < x->r->links.len; i++)
			if (ak_span_eq(ak_cstr(x->r->links.data[i]), trimmed)) dup = true;
		if (!dup && !(x->r->files.len && is_file_uris(s))) {
			ak_buf t = {0};
			for (const char *p = s; *p; p++)
				if (!(p[0] == '\r' && p[1] == '\n')) ak_buf_u8(&t, (uint8_t)*p);
			char *text = str_dup(ak_strn((const char *)t.data, t.len));
			ak_buf_free(&t);
			ak_vec_push(&x->owned, text);
			ak_vec_push(&x->items, ((ak_item){.kind = AK_SHARE_KIND_TEXT, .text = ak_cstr(text), .mime = AK_STR("text/plain"), .size = -1}));
		}
	}
	finish_items(x);
}

// dropped_now takes a drop on n at x, y, what it carries known.
static void dropped_now(node *n, drop_read *r, double x, double y) {
	dnd_state *t = n->dnd;
	GdkDrop *drop = r->drop;
	if (!takes(r, &t->accept)) {
		leave(n);
		gdk_drop_finish(drop, 0);
		forget_soon(drop);
		return;
	}
	GdkDragAction action = drop_action(r, gdk_drop_get_actions(drop));
	t->over_at[0] = (float)x;
	t->over_at[1] = (float)y;
	leave(n);
	ak_drop d = {.x = (float)x, .y = (float)y};
	if (r->local) {
		d.source = app_drag.source;
		gdk_drop_finish(drop, action);
		forget_soon(drop);
		emit_drop(n, &d, NULL, 0);
		return;
	}
	drop_items *xi = calloc(1, sizeof *xi);
	if (!xi) abort();
	xi->target = n->id;
	xi->r = r;
	xi->drop = g_object_ref(drop);
	xi->action = action;
	xi->d = d;
	for (uint32_t i = 0; i < r->files.len; i++) {
		char *m = lx_file_mime(r->files.data[i]);
		ak_vec_push(&xi->owned, m);
		if (!ak_match_mime(t->accept.data, t->accept.len, ak_cstr(m))) continue;
		const char *p = r->files.data[i];
		struct stat st;
		int64_t size = stat(p, &st) == 0 && !S_ISDIR(st.st_mode) ? (int64_t)st.st_size : -1;
		const char *base = strrchr(p, '/');
		ak_vec_push(&xi->items, ((ak_item){.kind = (uint8_t)file_share_kind(m), .path = ak_cstr(p), .name = ak_cstr(base ? base + 1 : p), .mime = ak_cstr(m), .size = size}));
	}
	if (ak_match_mime(t->accept.data, t->accept.len, AK_STR("text/uri-list")))
		for (uint32_t i = 0; i < r->links.len; i++) ak_vec_push(&xi->items, ((ak_item){.kind = AK_SHARE_KIND_LINK, .text = ak_cstr(r->links.data[i]), .mime = AK_STR("text/uri-list"), .size = -1}));
	if (!r->has_text || !ak_match_mime(t->accept.data, t->accept.len, AK_STR("text/plain"))) {
		finish_items(xi);
		return;
	}
	gdk_drop_read_value_async(drop, G_TYPE_STRING, G_PRIORITY_DEFAULT, NULL, on_text_read, xi);
}

static gboolean on_drop(GtkDropTargetAsync *tg, GdkDrop *drop, double x, double y, node *n) {
	(void)tg;
	drop_read *r = read_drop(drop);
	if (!n->dnd || !n->dnd->target) return 0;
	if (r->loading) {
		ak_vec_push(&r->waiting, ((drop_wait){n->id, x, y}));
		return 1;
	}
	if (!takes(r, &n->dnd->accept)) {
		lx_enter_begin();
		leave(n);
		lx_enter_end();
		gdk_drop_finish(drop, 0);
		forget_soon(drop);
		return 0;
	}
	lx_enter_begin();
	dropped_now(n, r, x, y);
	lx_enter_end();
	return 1;
}

static void forget_target(node *n) {
	for (uint32_t i = 0; i < targets.len; i++)
		if (targets.data[i] == n) {
			targets.data[i] = targets.data[--targets.len];
			return;
		}
}

static void set_drop_accept(node *n, ak_span b) {
	dnd_state *t = dnd_of(n);
	ak_buf_reset(&t->accept_bytes);
	ak_buf_put(&t->accept_bytes, b.p, b.len);
	t->accept.len = 0;
	ak_decode_strings(t->accept_bytes.data, t->accept_bytes.len, &t->accept);
	if (!t->accept.len || !n->widget) {
		if (t->target) {
			leave(n);
			gtk_widget_remove_controller(n->widget, (GtkEventController *)t->target);
			t->target = NULL;
			forget_target(n);
		}
		return;
	}
	if (t->target) return;
	t->target = gtk_drop_target_async_new(NULL, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	lx_connect(n, t->target, "accept", G_CALLBACK(on_accept));
	lx_connect(n, t->target, "drag-enter", G_CALLBACK(on_over));
	lx_connect(n, t->target, "drag-motion", G_CALLBACK(on_over));
	lx_connect(n, t->target, "drag-leave", G_CALLBACK(on_leave));
	lx_connect(n, t->target, "drop", G_CALLBACK(on_drop));
	gtk_widget_add_controller(n->widget, (GtkEventController *)t->target);
	ak_vec_push(&targets, n);
}

static bool dnd_prop(node *n, ak_prop p, const ak_value *v) {
	if (n->kind != AK_KIND_VIEW) return false;  // another kind's prop of the same number (Split's Items)
	switch (p) {
	case AK_PROP_VIEW_DRAG_ITEMS: set_drag_items(n, v->s); return true;
	case AK_PROP_VIEW_DROP_ACCEPT: set_drop_accept(n, v->s); return true;
	default: return false;
	}
}

static void dnd_release(node *n) {
	dnd_state *s = n->dnd;
	if (!s) return;
	if (s->target) forget_target(n);
	items_free(&s->drag);
	ak_buf_free(&s->accept_bytes);
	ak_vec_free(&s->accept);
	free(s);
	n->dnd = NULL;
}

const area lx_area_dnd = {
	.name = "dnd",
	.prop = dnd_prop,
	.release = dnd_release,
};
