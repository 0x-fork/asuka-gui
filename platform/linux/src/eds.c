// eds.c: platform/contacts and platform/calendar with Evolution Data Server
// (EDS), the store GNOME's apps share (GNOME Contacts and Calendar,
// Evolution, the online accounts' address books and calendars): the
// module's methods as macOS's.
//
// EDS asks nothing (a desktop app reads what the user's session does), so
// access is full when it has an address book (contacts) or a calendar
// (calendars), else it's as without EDS (system.c: contacts granted but
// not listed, calendars write only, adding opens a file). "list" reads
// every enabled address book's contacts (vCards), sorted by name; "pick"
// is a dialog over the window listing them with a search field (as
// AppKit's picker); "add" asks with an alert (the contact's name, phones
// and emails) and saves it in the default address book. "calendars" are
// the enabled calendars (their color, account, whether they're writable),
// "events" each occurrence in the range (EDS expands repeating ones),
// "add" asks with an alert (the title, the dates, the place) and saves
// the event in its calendar, or the default. platform/calendar has no edit
// or remove: none here either.
//
// EDS's libraries are opened the first time (each optional); its calls
// block, so they run on a thread of their own, one at a time.
#include "host.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void lx_contacts_request(ak_span args, reply *r);
void lx_contacts_list(ak_span args, reply *r);
void lx_contacts_pick(ak_span args, reply *r);
void lx_contacts_add(ak_span args, reply *r);
void lx_calendar_request(ak_span args, reply *r);
void lx_calendar_not_allowed(ak_span args, reply *r);
void lx_calendar_add(ak_span args, reply *r);
void lx_vcard(ak_buf *b, const dsk_json *c);
void lx_vevent(ak_buf *b, const char *uid, const dsk_json *e);

#define BOOKS "Address Book"
#define CALENDARS "Calendar"

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;  // one call at a time
static ESourceRegistry *reg;
static GHashTable *books, *cals;  // connected clients, by source UID

// ---- Loading (main thread) ----

static int loaded_book, loaded_cal;  // 0 not tried, 1 there, -1 not

static bool load_book(void) {
	if (!loaded_book) loaded_book = lx_lib_load(ASUKA_LIB_EDS) && lx_lib_load(ASUKA_LIB_EBOOKC) && lx_lib_load(ASUKA_LIB_EBOOK) && e_source_registry_new_sync && e_book_client_connect_sync && e_contact_new_from_vcard ? 1 : -1;
	return loaded_book > 0;
}

static bool load_cal(void) {
	if (!loaded_cal) loaded_cal = lx_lib_load(ASUKA_LIB_EDS) && lx_lib_load(ASUKA_LIB_ICAL) && lx_lib_load(ASUKA_LIB_ECAL) && e_source_registry_new_sync && e_cal_client_connect_sync && i_cal_component_new_from_string ? 1 : -1;
	return loaded_cal > 0;
}

// ---- EDS's calls (its thread, mu held) ----

static char *error_of(GError *e, const char *fallback) {
	char *s = g_strdup(e && e->message ? e->message : fallback);
	if (e) g_error_free(e);
	return s;
}

static ESourceRegistry *registry(char **err) {
	if (reg) return reg;
	GError *e = NULL;
	reg = e_source_registry_new_sync(NULL, &e);
	if (!reg) *err = error_of(e, "no evolution-data-server registry");
	return reg;
}

// A source: an address book or a calendar.
typedef struct {
	char *uid, *name, *account, *color;
	bool def;
} source_t;

typedef AK_VEC(source_t) sources;

static void sources_free(sources *s) {
	for (uint32_t i = 0; i < s->len; i++) {
		g_free(s->data[i].uid);
		g_free(s->data[i].name);
		g_free(s->data[i].account);
		g_free(s->data[i].color);
	}
	ak_vec_free(s);
}

// list_sources are the enabled sources of an extension (BOOKS,
// CALENDARS).
static bool list_sources(const char *ext, sources *out, char **err) {
	ESourceRegistry *r = registry(err);
	if (!r) return false;
	ESource *def = !strcmp(ext, BOOKS) ? e_source_registry_ref_default_address_book(r) : e_source_registry_ref_default_calendar(r);
	char *def_uid = def ? g_strdup(e_source_get_uid(def)) : NULL;
	if (def) g_object_unref(def);
	GList *list = e_source_registry_list_enabled(r, ext);
	for (GList *l = list; l; l = l->next) {
		ESource *src = l->data;
		source_t s = {g_strdup(e_source_get_uid(src)), g_strdup(e_source_get_display_name(src)), NULL, NULL, false};
		s.def = def_uid && !strcmp(s.uid, def_uid);
		const char *parent = e_source_get_parent(src);
		if (parent && *parent) {
			ESource *ps = e_source_registry_ref_source(r, parent);
			if (ps) {
				s.account = g_strdup(e_source_get_display_name(ps));
				g_object_unref(ps);
			}
		}
		if (!strcmp(ext, CALENDARS)) {
			ESourceExtension *x = e_source_get_extension(src, ext);
			if (x) s.color = g_ascii_strdown(S(e_source_selectable_get_color(x)), -1);
		}
		ak_vec_push(out, s);
		g_object_unref(src);
	}
	g_list_free(list);
	g_free(def_uid);
	return true;
}

// client is a connected client of a source (cached); uid NULL or "" is
// the default source.
static EClient *client(const char *uid, bool book, char **err) {
	ESourceRegistry *r = registry(err);
	if (!r) return NULL;
	ESource *src;
	if (str_empty(uid)) {
		src = book ? e_source_registry_ref_default_address_book(r) : e_source_registry_ref_default_calendar(r);
		if (!src) {
			*err = g_strdup(book ? "no default address book" : "no default calendar");
			return NULL;
		}
	} else {
		src = e_source_registry_ref_source(r, uid);
		if (!src) {
			*err = g_strdup_printf("no %s \"%s\"", book ? "address book" : "calendar", uid);
			return NULL;
		}
	}
	const char *id = e_source_get_uid(src);
	GHashTable **cache = book ? &books : &cals;
	if (!*cache) *cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
	EClient *c = g_hash_table_lookup(*cache, id);
	if (!c) {
		GError *e = NULL;
		c = book ? (EClient *)e_book_client_connect_sync(src, 30, NULL, &e) : (EClient *)e_cal_client_connect_sync(src, 0 /* events */, 30, NULL, &e);
		if (!c)
			*err = error_of(e, "couldn't open it");
		else
			g_hash_table_insert(*cache, g_strdup(id), c);
	}
	g_object_unref(src);
	return c;
}

// ---- vCards and iCalendar ----

// A vCard's or iCalendar's property: its name (upper case, no group), its
// TYPE values (a bare parameter is one), its raw value.
typedef struct {
	char name[64];
	char types[256];  // upper case, ';' between
	const char *raw;
} vprop;

// next_prop reads the next of the lines (unfolded, NUL-ended), the name
// and parameters ending at the first ':' outside quotes: false at the end.
static bool next_prop(char **at, vprop *p) {
	while (**at) {
		char *line = *at, *end = strchr(line, '\n');
		*at = end ? end + 1 : line + strlen(line);
		if (end) *end = 0;
		bool quoted = false;
		char *colon = NULL;
		for (char *c = line; *c; c++) {
			if (*c == '"') quoted = !quoted;
			if (*c == ':' && !quoted) {
				colon = c;
				break;
			}
		}
		if (!colon) continue;
		*colon = 0;
		p->raw = colon + 1;
		p->types[0] = 0;
		char *semi = strchr(line, ';');
		if (semi) *semi = 0;
		char *dot = strrchr(line, '.');
		snprintf(p->name, sizeof p->name, "%s", dot ? dot + 1 : line);
		for (char *c = p->name; *c; c++) *c = (char)g_ascii_toupper(*c);
		for (char *kv = semi ? semi + 1 : NULL; kv;) {
			char *next = strchr(kv, ';');
			if (next) *next++ = 0;
			char *eq = strchr(kv, '=');
			const char *v = kv;
			if (eq) {
				*eq = 0;
				v = g_ascii_strcasecmp(kv, "TYPE") ? NULL : eq + 1;
			}
			if (v) {
				size_t k = strlen(p->types);
				snprintf(p->types + k, sizeof p->types - k, "%s%s;", k ? "" : ";", v);
			}
			kv = next;
		}
		for (char *c = p->types; *c; c++) *c = *c == ',' || *c == '"' ? (*c == ',' ? ';' : ' ') : (char)g_ascii_toupper(*c);
		return true;
	}
	return false;
}

// unfold is s with its folded lines joined and CRLFs made LFs (malloc'd).
static char *unfold(const char *s) {
	ak_buf b = {0};
	for (; *s; s++) {
		if (*s == '\r' && s[1] == '\n') continue;
		if (*s == '\n' && (s[1] == ' ' || s[1] == '\t')) {
			s++;
			continue;
		}
		ak_buf_u8(&b, (uint8_t)*s);
	}
	char *out = str_dup(ak_strn((const char *)b.data, b.len));
	ak_buf_free(&b);
	return out;
}

// text is a text value, unescaped (malloc'd); stop ends it at an
// unescaped ';' (a structured value's components) and says where.
static char *text(const char *raw, const char **stop) {
	ak_buf b = {0};
	const char *s = raw;
	for (; *s; s++) {
		if (stop && *s == ';') break;
		if (*s == '\\' && s[1]) {
			s++;
			ak_buf_u8(&b, *s == 'n' || *s == 'N' ? '\n' : (uint8_t)*s);
			continue;
		}
		ak_buf_u8(&b, (uint8_t)*s);
	}
	if (stop) *stop = *s == ';' ? s + 1 : NULL;
	char *out = str_dup(ak_strn((const char *)b.data, b.len));
	ak_buf_free(&b);
	return out;
}

static bool has_type(const vprop *p, const char *t) {
	char want[64];
	snprintf(want, sizeof want, ";%s;", t);
	return strstr(p->types, want) != NULL;
}

// label is a phone's or an email's label, of its TYPEs: the label
// platform/contacts has on the other platforms ("mobile", "home"...).
static const char *label(const vprop *p) {
	if (has_type(p, "CELL")) return "mobile";
	if (has_type(p, "FAX") && has_type(p, "HOME")) return "home fax";
	if (has_type(p, "FAX") && has_type(p, "WORK")) return "work fax";
	if (has_type(p, "FAX")) return "fax";
	if (has_type(p, "PAGER")) return "pager";
	if (has_type(p, "IPHONE")) return "iPhone";
	if (has_type(p, "MAIN")) return "main";
	if (has_type(p, "HOME")) return "home";
	if (has_type(p, "WORK")) return "work";
	if (has_type(p, "OTHER")) return "other";
	return "";
}

// birthday is a BDAY as platform/contacts has it: YYYY-MM-DD, or --MM-DD
// without a year; "" if it's not a date.
static void birthday(const char *s, char out[16]) {
	char d[16] = "";
	size_t n = 0;
	bool no_year = s[0] == '-' && s[1] == '-';
	for (const char *p = s; *p && *p != 'T' && n < sizeof d - 1; p++)
		if (*p != '-' && *p != ' ') d[n++] = *p;
	d[n] = 0;
	out[0] = 0;
	if (no_year && n == 4)
		snprintf(out, 16, "--%.2s-%.2s", d, d + 2);
	else if (n == 8)
		snprintf(out, 16, "%.4s-%.2s-%.2s", d, d + 4, d + 6);
}

// A contact as platform/contacts' JSON, and its name to sort by.
typedef struct {
	char *json, *name, *key, *subtitle, *haystack;
} contact_t;

static void contact_free(contact_t *c) {
	free(c->json);
	free(c->name);
	g_free(c->key);
	free(c->subtitle);
	g_free(c->haystack);
}

static contact_t contact_of(const char *card) {
	char *s = unfold(card), *at = s;
	char *id = NULL, *fn = NULL, *given = NULL, *family = NULL, *org = NULL, *title = NULL, *note = NULL;
	char bday[16] = "";
	ak_buf phones = {0}, emails = {0}, hay = {0};
	char *first_phone = NULL, *first_email = NULL;
	vprop p;
	while (next_prop(&at, &p)) {
		char **slot = !strcmp(p.name, "UID") ? &id : !strcmp(p.name, "FN") ? &fn : !strcmp(p.name, "TITLE") ? &title : !strcmp(p.name, "NOTE") ? &note : NULL;
		if (slot) {
			free(*slot);
			*slot = text(p.raw, NULL);
		} else if (!strcmp(p.name, "N")) {
			const char *rest;
			free(family);
			free(given);
			family = text(p.raw, &rest);
			given = rest ? text(rest, &rest) : str_dup(ak_cstr(""));
		} else if (!strcmp(p.name, "ORG")) {
			const char *rest;
			free(org);
			org = text(p.raw, &rest);
		} else if (!strcmp(p.name, "TEL") || !strcmp(p.name, "EMAIL")) {
			bool tel = !strcmp(p.name, "TEL");
			ak_buf *b = tel ? &phones : &emails;
			char *v = text(p.raw, NULL);
			dsk_jw w = {0};
			dsk_jw_obj(&w);
			dsk_jw_key(&w, "label");
			dsk_jw_str(&w, label(&p));
			dsk_jw_key(&w, "value");
			dsk_jw_str(&w, v);
			dsk_jw_end(&w);
			size_t n;
			char *j = dsk_jw_take(&w, &n);
			if (b->len) ak_buf_u8(b, ',');
			ak_buf_put(b, j, n);
			free(j);
			ak_buf_printf(&hay, "\n%s", v);
			if (tel && !first_phone) first_phone = str_dup(ak_cstr(v));
			if (!tel && !first_email) first_email = str_dup(ak_cstr(v));
			free(v);
		} else if (!strcmp(p.name, "BDAY")) {
			char *v = text(p.raw, NULL);
			birthday(v, bday);
			free(v);
		}
	}
	free(s);
	char *name = str_dup(ak_cstr(S(fn)));
	if (!*name) {
		free(name);
		name = g_strstrip(g_strdup_printf("%s %s", S(given), S(family)));
		char *t = str_dup(ak_cstr(name));
		g_free(name);
		name = t;
		if (!*name) {
			free(name);
			name = str_dup(ak_cstr(S(org)));
		}
	}
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	const char *keys[] = {"id", "name", "givenName", "familyName", "organization", "jobTitle", "birthday", "note"};
	const char *vals[] = {S(id), name, S(given), S(family), S(org), S(title), bday, S(note)};
	for (size_t i = 0; i < AK_LEN(keys); i++) {
		dsk_jw_key(&w, keys[i]);
		dsk_jw_str(&w, vals[i]);
	}
	ak_buf list = {0};
	ak_buf_printf(&list, "[%.*s]", (int)phones.len, phones.data ? (const char *)phones.data : "");
	dsk_jw_key(&w, "phones");
	dsk_jw_json(&w, (const char *)list.data, list.len);
	ak_buf_reset(&list);
	ak_buf_printf(&list, "[%.*s]", (int)emails.len, emails.data ? (const char *)emails.data : "");
	dsk_jw_key(&w, "emails");
	dsk_jw_json(&w, (const char *)list.data, list.len);
	ak_buf_free(&list);
	dsk_jw_end(&w);
	size_t n;
	contact_t c = {dsk_jw_take(&w, &n), name, g_utf8_strdown(name, -1), NULL, NULL};
	c.subtitle = str_dup(ak_cstr(first_phone ? first_phone : first_email ? first_email : S(org)));
	char *h = g_strdup_printf("%s\n%s%s", name, S(org), lx_buf_cstr(&hay));
	c.haystack = g_utf8_strdown(h, -1);
	g_free(h);
	free(id);
	free(fn);
	free(given);
	free(family);
	free(org);
	free(title);
	free(note);
	free(first_phone);
	free(first_email);
	ak_buf_free(&phones);
	ak_buf_free(&emails);
	ak_buf_free(&hay);
	return c;
}

typedef AK_VEC(contact_t) contacts;

static int by_name(const void *a, const void *b) { return strcmp(((const contact_t *)a)->key, ((const contact_t *)b)->key); }

// all_contacts are the contacts of every enabled address book whose name
// has name (all: ""), sorted by name.
static bool all_contacts(const char *name, contacts *out, char **err) {
	sources srcs = {0};
	if (!list_sources(BOOKS, &srcs, err)) return false;
	char *sexp;
	if (*name) {
		ak_buf q = {0};
		ak_buf_u8(&q, '"');
		for (const char *p = name; *p; p++) {
			if (*p == '\\' || *p == '"') ak_buf_u8(&q, '\\');
			ak_buf_u8(&q, (uint8_t)*p);
		}
		ak_buf_u8(&q, '"');
		const char *qs = lx_buf_cstr(&q);
		sexp = g_strdup_printf("(or (contains \"full_name\" %s) (contains \"given_name\" %s) (contains \"family_name\" %s) (contains \"nickname\" %s))", qs, qs, qs, qs);
		ak_buf_free(&q);
	} else {
		sexp = g_strdup("(contains \"x-evolution-any-field\" \"\")");
	}
	char *first = NULL;
	for (uint32_t i = 0; i < srcs.len; i++) {
		char *e = NULL;
		EClient *c = client(srcs.data[i].uid, true, &e);
		GSList *list = NULL;
		GError *ge = NULL;
		if (c && !e_book_client_get_contacts_sync((EBookClient *)c, sexp, &list, NULL, &ge)) e = error_of(ge, "couldn't read the contacts");
		if (e) {
			if (!first)
				first = e;
			else
				g_free(e);
			continue;  // an account offline: the others
		}
		for (GSList *l = list; l; l = l->next) {
			char *card = e_vcard_to_string(l->data, 1);  // EVC_FORMAT_VCARD_30
			ak_vec_push(out, contact_of(S(card)));
			g_free(card);
			g_object_unref(l->data);
		}
		g_slist_free(list);
	}
	g_free(sexp);
	sources_free(&srcs);
	if (!out->len && first) {
		*err = first;
		return false;
	}
	g_free(first);
	if (out->len > 1) qsort(out->data, out->len, sizeof *out->data, by_name);
	return true;
}

// ---- Off the main thread ----

typedef struct job job;
struct job {
	void (*work)(job *j);  // on EDS's thread, mu held
	void (*done)(job *j);  // then on the main thread
	reply *r;
	char *err;
	ak_buf out;
	dsk_json *args;
	char *text;  // a vCard or a VEVENT to add
	contacts cs;
	bool has;  // the source kind's there
	bool book;
};

static void job_free(job *j) {
	ak_buf_free(&j->out);
	dsk_json_free(j->args);
	free(j->text);
	for (uint32_t i = 0; i < j->cs.len; i++) contact_free(&j->cs.data[i]);
	ak_vec_free(&j->cs);
	g_free(j->err);
	free(j);
}

static gboolean job_back(gpointer data) {
	job *j = data;
	j->done(j);
	return 0;
}

static gpointer job_thread(gpointer data) {
	job *j = data;
	pthread_mutex_lock(&mu);
	j->work(j);
	pthread_mutex_unlock(&mu);
	g_idle_add_full(G_PRIORITY_DEFAULT, job_back, j, NULL);
	return NULL;
}

static void run_job(job *j) { g_thread_unref(g_thread_new("asuka-eds", job_thread, j)); }

static void answer_out(job *j) {
	if (j->err)
		reply_err(j->r, "%s", j->err);
	else
		reply_ok(j->r, j->out.data, j->out.len);
	job_free(j);
}

// ---- EDS or not ----

typedef struct {
	method_fn eds, fallback;
	ak_buf args;
	reply *r;
} either_t;

static void has_work(job *j) {
	sources s = {0};
	char *err = NULL;
	j->has = list_sources(j->book ? BOOKS : CALENDARS, &s, &err) && s.len > 0;
	sources_free(&s);
	g_free(err);
}

static void has_done(job *j) {
	either_t *e = (either_t *)j->text;
	j->text = NULL;
	method_fn fn = j->has ? e->eds : e->fallback;
	fn(ak_strn((const char *)e->args.data, e->args.len), e->r);
	ak_buf_free(&e->args);
	free(e);
	job_free(j);
}

// either runs a method EDS's way when it has a source of the kind, else
// fallback's.
static void either(bool book, method_fn eds, method_fn fallback, ak_span args, reply *r) {
	if (!(book ? load_book() : load_cal())) {
		fallback(args, r);
		return;
	}
	either_t *e = calloc(1, sizeof *e);
	job *j = calloc(1, sizeof *j);
	if (!e || !j) abort();
	*e = (either_t){eds, fallback, {0}, r};
	ak_buf_put(&e->args, args.p, args.len);
	j->work = has_work;
	j->done = has_done;
	j->book = book;
	j->text = (char *)e;
	run_job(j);
}

// ---- The confirming alert ----

typedef struct {
	void (*done)(void *ctx, bool add);
	void *ctx;
} confirm_t;

static void on_confirm(AdwAlertDialog *d, const char *response, gpointer data) {
	(void)d;
	confirm_t *c = data;
	c->done(c->ctx, !strcmp(response, "add"));
	free(c);
}

// confirm asks with an alert over the window: its Add button (the
// default) or Cancel (Escape).
static void confirm(const char *title, const char *body, const char *add, void (*done)(void *ctx, bool add), void *ctx) {
	AdwDialog *d = adw_alert_dialog_new(title, body);
	AdwAlertDialog *a = (AdwAlertDialog *)d;
	adw_alert_dialog_add_response(a, "cancel", lx_word("cancel"));
	adw_alert_dialog_add_response(a, "add", add);
	adw_alert_dialog_set_response_appearance(a, "add", ADW_RESPONSE_SUGGESTED);
	adw_alert_dialog_set_default_response(a, "add");
	adw_alert_dialog_set_close_response(a, "cancel");
	confirm_t *c = malloc(sizeof *c);
	if (!c) abort();
	*c = (confirm_t){done, ctx};
	g_signal_connect(d, "response", G_CALLBACK(on_confirm), c);
	win *w = H.active_win ? H.active_win : H.main;
	adw_dialog_present(d, w ? (GtkWidget *)w->window : NULL);
}

// ---- Contacts ----

static void c_request(ak_span args, reply *r) {
	(void)args;
	reply_ok(r, "\3", 1);  // full
}

static void list_work(job *j) {
	if (!all_contacts(dsk_json_str(j->args, "name", ""), &j->cs, &j->err)) return;
	int limit = (int)dsk_json_num(j->args, "limit", 0);
	ak_buf_u8(&j->out, '[');
	for (uint32_t i = 0; i < j->cs.len && (limit <= 0 || (int)i < limit); i++) {
		if (i) ak_buf_u8(&j->out, ',');
		ak_buf_put(&j->out, j->cs.data[i].json, strlen(j->cs.data[i].json));
	}
	ak_buf_u8(&j->out, ']');
}

static void c_list(ak_span args, reply *r) {
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->work = list_work;
	j->done = answer_out;
	j->r = r;
	j->args = dsk_json_parse(args.p, args.len);
	run_job(j);
}

// The pick dialog.
typedef struct {
	AdwDialog *dialog;
	GtkWidget *list, *search, *cancel;
	AK_VEC(GtkWidget *) rows;
	job *j;  // its contacts
	bool done;
} picker;

static picker *picking;

// finish closes the dialog and answers the contact at i (-1: none).
static void finish(picker *p, int i) {
	if (!p || p->done) return;
	p->done = true;
	if (picking == p) picking = NULL;
	if (i >= 0 && (uint32_t)i < p->j->cs.len)
		reply_str(p->j->r, p->j->cs.data[i].json);
	else
		reply_ok(p->j->r, NULL, 0);
	// Its handlers go with it: the dialog says it closed once its closing
	// is done, after p is.
	GObject *with[] = {(GObject *)p->dialog, (GObject *)p->list, (GObject *)p->search, (GObject *)p->cancel};
	for (size_t k = 0; k < sizeof with / sizeof *with; k++) g_signal_handlers_disconnect_matched(with[k], G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, p);
	adw_dialog_force_close(p->dialog);
	g_object_unref(p->dialog);
	job_free(p->j);
	ak_vec_free(&p->rows);
	free(p);
}

static void on_row(GtkListBox *l, GtkListBoxRow *row, gpointer data) {
	(void)l;
	finish(data, gtk_list_box_row_get_index(row));
}

static void on_cancel(GtkButton *b, gpointer data) {
	(void)b;
	finish(data, -1);
}

static void on_closed(AdwDialog *d, gpointer data) {
	(void)d;
	finish(data, -1);
}

// filter shows the rows whose contact has the query in its name,
// organization, phones or emails.
static void on_search(GtkSearchEntry *e, gpointer data) {
	picker *p = data;
	char *q = g_utf8_strdown(gtk_editable_get_text((GtkEditable *)e), -1);
	g_strstrip(q);
	for (uint32_t i = 0; i < p->rows.len; i++) gtk_widget_set_visible(p->rows.data[i], !*q || strstr(p->j->cs.data[i].haystack, q));
	g_free(q);
}

static void pick_done(job *j) {
	if (j->err) {
		answer_out(j);
		return;
	}
	if (picking) finish(picking, -1);  // one at a time
	picker *p = calloc(1, sizeof *p);
	if (!p) abort();
	p->j = j;
	p->dialog = g_object_ref_sink(adw_dialog_new());
	adw_dialog_set_content_width(p->dialog, 420);
	adw_dialog_set_content_height(p->dialog, 560);
	GtkWidget *tv = adw_toolbar_view_new(), *bar = adw_header_bar_new();
	adw_header_bar_set_show_start_title_buttons((AdwHeaderBar *)bar, 0);
	adw_header_bar_set_show_end_title_buttons((AdwHeaderBar *)bar, 0);
	GtkWidget *cancel = gtk_button_new_with_label(lx_word("cancel"));
	adw_header_bar_pack_start((AdwHeaderBar *)bar, cancel);
	p->search = gtk_search_entry_new();
	gtk_search_entry_set_placeholder_text((GtkSearchEntry *)p->search, lx_word("search"));
	gtk_widget_set_hexpand(p->search, 1);
	adw_header_bar_set_title_widget((AdwHeaderBar *)bar, p->search);
	adw_toolbar_view_add_top_bar((AdwToolbarView *)tv, bar);
	p->list = gtk_list_box_new();
	gtk_widget_add_css_class(p->list, "navigation-sidebar");
	for (uint32_t i = 0; i < j->cs.len; i++) {
		GtkWidget *row = adw_action_row_new();
		adw_preferences_row_set_use_markup((AdwPreferencesRow *)row, 0);
		adw_preferences_row_set_title((AdwPreferencesRow *)row, j->cs.data[i].name);
		if (*j->cs.data[i].subtitle) adw_action_row_set_subtitle((AdwActionRow *)row, j->cs.data[i].subtitle);
		gtk_list_box_row_set_activatable((GtkListBoxRow *)row, 1);
		gtk_list_box_append((GtkListBox *)p->list, row);
		ak_vec_push(&p->rows, row);
	}
	GtkWidget *sw = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy((GtkScrolledWindow *)sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(sw, 1);
	gtk_scrolled_window_set_child((GtkScrolledWindow *)sw, p->list);
	adw_toolbar_view_set_content((AdwToolbarView *)tv, sw);
	adw_dialog_set_child(p->dialog, tv);
	g_signal_connect(p->list, "row-activated", G_CALLBACK(on_row), p);
	g_signal_connect(p->search, "search-changed", G_CALLBACK(on_search), p);
	g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), p);
	p->cancel = cancel;
	g_signal_connect(p->dialog, "closed", G_CALLBACK(on_closed), p);
	picking = p;
	win *w = H.active_win ? H.active_win : H.main;
	adw_dialog_present(p->dialog, w ? (GtkWidget *)w->window : NULL);
	gtk_widget_grab_focus(p->search);
}

static void pick_work(job *j) { all_contacts("", &j->cs, &j->err); }

static void c_pick(ak_span args, reply *r) {
	(void)args;
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->work = pick_work;
	j->done = pick_done;
	j->r = r;
	run_job(j);
}

static void add_contact_work(job *j) {
	EClient *c = client(NULL, true, &j->err);
	if (!c) return;
	EContact *contact = e_contact_new_from_vcard(j->text);
	if (!contact) {
		j->err = g_strdup("not a vCard");
		return;
	}
	char *uid = NULL;
	GError *e = NULL;
	if (!e_book_client_add_contact_sync((EBookClient *)c, contact, 0, &uid, NULL, &e))
		j->err = error_of(e, "couldn't save the contact");
	else
		ak_buf_u8(&j->out, 1);
	g_free(uid);
	g_object_unref(contact);
}

static void add_confirmed(void *ctx, bool add) {
	job *j = ctx;
	if (!add) {
		reply_ok(j->r, "\0", 1);
		job_free(j);
		return;
	}
	run_job(j);
}

// contact_alert is the add alert's title and body: the name, then the
// organization, phones and emails, a line each.
static void c_add(ak_span args, reply *r) {
	dsk_json *c = dsk_json_parse(args.p, args.len);
	if (!c) {
		reply_err(r, "contacts.add: bad arguments");
		return;
	}
	const char *given = dsk_json_str(c, "givenName", ""), *family = dsk_json_str(c, "familyName", ""), *org = dsk_json_str(c, "organization", ""), *job_title = dsk_json_str(c, "jobTitle", "");
	char *title = g_strstrip(g_strdup_printf("%s %s", given, family));
	if (!*title) {
		g_free(title);
		title = g_strdup(*dsk_json_str(c, "name", "") ? dsk_json_str(c, "name", "") : org);
	}
	ak_buf body = {0};
	if (*org && strcmp(title, org)) {
		if (*job_title)
			ak_buf_printf(&body, "%s · %s", job_title, org);
		else
			ak_buf_printf(&body, "%s", org);
	}
	static const char *const lists[] = {"phones", "emails"};
	for (int k = 0; k < 2; k++) {
		const dsk_json *l = dsk_json_get(c, lists[k]);
		for (size_t i = 0; l && l->type == DSK_JARR && i < l->n; i++) {
			const char *lb = dsk_json_str(&l->kids[i], "label", ""), *v = dsk_json_str(&l->kids[i], "value", "");
			ak_buf_printf(&body, "%s%s%s%s", body.len ? "\n" : "", lb, *lb ? ": " : "", v);
		}
	}
	const char *bday = dsk_json_str(c, "birthday", "");
	if (*bday) ak_buf_printf(&body, "%s%s", body.len ? "\n" : "", bday);
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	ak_buf card = {0};
	lx_vcard(&card, c);
	j->text = str_dup(ak_strn((const char *)card.data, card.len));
	ak_buf_free(&card);
	j->work = add_contact_work;
	j->done = answer_out;
	j->r = r;
	confirm(title, lx_buf_cstr(&body), lx_word("addToContacts"), add_confirmed, j);
	g_free(title);
	ak_buf_free(&body);
	dsk_json_free(c);
}

// ---- Calendars ----

static void calendars_work(job *j) {
	sources s = {0};
	if (!list_sources(CALENDARS, &s, &j->err)) return;
	dsk_jw w = {0};
	dsk_jw_arr(&w);
	for (uint32_t i = 0; i < s.len; i++) {
		char *e = NULL;
		EClient *c = client(s.data[i].uid, false, &e);
		bool writable = c && !e_client_is_readonly(c);
		g_free(e);
		dsk_jw_obj(&w);
		dsk_jw_key(&w, "id");
		dsk_jw_str(&w, s.data[i].uid);
		dsk_jw_key(&w, "title");
		dsk_jw_str(&w, S(s.data[i].name));
		dsk_jw_key(&w, "color");
		dsk_jw_str(&w, S(s.data[i].color));
		dsk_jw_key(&w, "source");
		dsk_jw_str(&w, S(s.data[i].account));
		dsk_jw_key(&w, "writable");
		dsk_jw_bool(&w, writable);
		dsk_jw_end(&w);
	}
	dsk_jw_aend(&w);
	sources_free(&s);
	size_t n;
	char *json = dsk_jw_take(&w, &n);
	ak_buf_put(&j->out, json, n);
	free(json);
}

static void k_calendars(ak_span args, reply *r) {
	(void)args;
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->work = calendars_work;
	j->done = answer_out;
	j->r = r;
	run_job(j);
}

// ical_ms is an ICalTime in ms since 1970: a date at UTC midnight, a
// floating time in the local zone.
static int64_t ical_ms(ICalTime *t, bool *all_day) {
	*all_day = false;
	if (!t) return 0;
	int y, mo, d, h = 0, mi = 0, s = 0;
	i_cal_time_get_date(t, &y, &mo, &d);
	struct tm tm = {.tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d};
	if (i_cal_time_is_date(t)) {
		*all_day = true;
		return (int64_t)timegm(&tm) * 1000;
	}
	i_cal_time_get_time(t, &h, &mi, &s);
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;
	if (i_cal_time_is_utc(t)) return (int64_t)timegm(&tm) * 1000;
	ICalTimezone *z = i_cal_time_get_timezone(t);
	if (z) return (int64_t)i_cal_time_as_timet_with_zone(t, z) * 1000;
	tm.tm_isdst = -1;
	return (int64_t)mktime(&tm) * 1000;
}

typedef AK_VEC(struct {
	int64_t start;
	char *json;
}) event_list;

typedef struct {
	const char *calendar;
	event_list *events;
} inst_ctx;

static gboolean on_instance(ICalComponent *comp, ICalTime *start, ICalTime *end, gpointer data, GCancellable *cancellable, GError **error) {
	(void)cancellable;
	(void)error;
	inst_ctx *c = data;
	bool all_day, ignore;
	int64_t s = ical_ms(start, &all_day), e = end ? ical_ms(end, &ignore) : s;
	char *ical = i_cal_component_as_ical_string(comp);
	char *u = unfold(S(ical)), *at = u;
	g_free(ical);
	char *id = NULL, *title = NULL, *loc = NULL, *notes = NULL, *url = NULL;
	int depth = 0;
	vprop p;
	while (next_prop(&at, &p)) {
		if (!strcmp(p.name, "BEGIN")) {
			depth++;
			continue;
		}
		if (!strcmp(p.name, "END")) {
			depth--;
			continue;
		}
		if (depth != 1) continue;  // the event's own, not an alarm's
		char **slot = !strcmp(p.name, "UID") ? &id : !strcmp(p.name, "SUMMARY") ? &title : !strcmp(p.name, "LOCATION") ? &loc : !strcmp(p.name, "DESCRIPTION") ? &notes : NULL;
		if (slot) {
			free(*slot);
			*slot = text(p.raw, NULL);
		} else if (!strcmp(p.name, "URL")) {
			free(url);
			url = str_dup(ak_cstr(p.raw));
		}
	}
	free(u);
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	const char *keys[] = {"id", "calendarId", "title", "location", "notes", "url"};
	const char *vals[] = {S(id), c->calendar, S(title), S(loc), S(notes), S(url)};
	for (size_t i = 0; i < AK_LEN(keys); i++) {
		dsk_jw_key(&w, keys[i]);
		dsk_jw_str(&w, vals[i]);
	}
	dsk_jw_key(&w, "startMs");
	dsk_jw_int(&w, s);
	dsk_jw_key(&w, "endMs");
	dsk_jw_int(&w, e);
	dsk_jw_key(&w, "allDay");
	dsk_jw_bool(&w, all_day);
	dsk_jw_end(&w);
	size_t n;
	ak_vec_reserve(c->events, 1);
	c->events->data[c->events->len].start = s;
	c->events->data[c->events->len].json = dsk_jw_take(&w, &n);
	c->events->len++;
	free(id);
	free(title);
	free(loc);
	free(notes);
	free(url);
	return 1;  // keep going
}

static int by_start(const void *a, const void *b) {
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return x < y ? -1 : x > y;
}

// events: {"from", "to" (ms), "calendars" (all: none)}: the occurrences
// sorted by start.
static void events_work(job *j) {
	sources s = {0};
	if (!list_sources(CALENDARS, &s, &j->err)) return;
	const dsk_json *want = dsk_json_get(j->args, "calendars");
	time_t from = (time_t)(dsk_json_num(j->args, "from", 0) / 1000), to = (time_t)(dsk_json_num(j->args, "to", 0) / 1000);
	event_list events = {0};
	char *first = NULL;
	for (uint32_t i = 0; i < s.len; i++) {
		bool wanted = !want || want->type != DSK_JARR || !want->n;
		for (size_t k = 0; !wanted && k < want->n; k++) wanted = want->kids[k].type == DSK_JSTR && !strcmp(want->kids[k].str, s.data[i].uid);
		if (!wanted) continue;
		char *e = NULL;
		EClient *c = client(s.data[i].uid, false, &e);
		if (!c) {
			if (!first)
				first = e;
			else
				g_free(e);
			continue;
		}
		inst_ctx ic = {s.data[i].uid, &events};
		e_cal_client_generate_instances_sync((ECalClient *)c, from, to, NULL, on_instance, &ic);
	}
	sources_free(&s);
	if (!events.len && first) {
		j->err = first;
		ak_vec_free(&events);
		return;
	}
	g_free(first);
	if (events.len > 1) qsort(events.data, events.len, sizeof *events.data, by_start);
	ak_buf_u8(&j->out, '[');
	for (uint32_t i = 0; i < events.len; i++) {
		if (i) ak_buf_u8(&j->out, ',');
		ak_buf_put(&j->out, events.data[i].json, strlen(events.data[i].json));
		free(events.data[i].json);
	}
	ak_buf_u8(&j->out, ']');
	ak_vec_free(&events);
}

static void k_events(ak_span args, reply *r) {
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->work = events_work;
	j->done = answer_out;
	j->r = r;
	j->args = dsk_json_parse(args.p, args.len);
	run_job(j);
}

static void add_event_work(job *j) {
	EClient *c = client(dsk_json_str(j->args, "calendarId", ""), false, &j->err);
	if (!c) return;
	ICalComponent *comp = i_cal_component_new_from_string(j->text);
	if (!comp) {
		j->err = g_strdup("not an event");
		return;
	}
	char *uid = NULL;
	GError *e = NULL;
	if (!e_cal_client_create_object_sync((ECalClient *)c, comp, 0, &uid, NULL, &e))
		j->err = error_of(e, "couldn't save the event");
	else
		ak_buf_u8(&j->out, 1);
	g_free(uid);
	g_object_unref(comp);
}

// event_dates are an event's dates as the user's locale writes them
// (GLib's %x), with its times (the user's clock).
static char *format_ms(int64_t ms, const char *f, bool all_day) {
	GDateTime *dt;
	if (all_day) {  // the days, wherever the user is
		time_t t = (time_t)(ms / 1000);
		struct tm u;
		gmtime_r(&t, &u);
		dt = g_date_time_new_local(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, 0, 0, 0);
	} else {
		dt = g_date_time_new_from_unix_local(ms / 1000);
	}
	char *s = g_date_time_format(dt, f);
	g_date_time_unref(dt);
	return g_strstrip(s ? s : g_strdup(""));
}

static char *event_dates(int64_t start, int64_t end, bool all_day) {
	const char *clock = H.hour_cycle == 12 ? "%l:%M %p" : "%H:%M";
	char *out;
	if (all_day) {
		int64_t last = end - 24 * 3600 * 1000LL;
		char *a = format_ms(start, "%x", true);
		if (last <= start) return a;
		char *b = format_ms(last, "%x", true);
		out = g_strdup_printf("%s – %s", a, b);
		g_free(a);
		g_free(b);
		return out;
	}
	char *s = format_ms(start, "%x", false), *e = format_ms(end, "%x", false), *st = format_ms(start, clock, false), *et = format_ms(end, clock, false);
	if (!strcmp(s, e))
		out = g_strdup_printf("%s, %s – %s", s, st, et);
	else
		out = g_strdup_printf("%s, %s – %s, %s", s, st, e, et);
	g_free(s);
	g_free(e);
	g_free(st);
	g_free(et);
	return out;
}

// add_times are "add"'s start and end (ms) as the event is saved: no
// start, the next hour (all day: today); an end not after the start an
// hour after it (all day: the day after).
static void add_times(int64_t *start, int64_t *end, bool all_day) {
	const int64_t hour = 3600 * 1000LL, day = 24 * hour;
	time_t now = time(NULL);
	if (*start <= 0) {
		if (all_day) {
			struct tm l;
			localtime_r(&now, &l);
			struct tm u = {.tm_year = l.tm_year, .tm_mon = l.tm_mon, .tm_mday = l.tm_mday};
			*start = (int64_t)timegm(&u) * 1000;
		} else {
			*start = ((int64_t)now / 3600 + 1) * hour;
		}
		*end = *start;
	}
	if (all_day) {
		*start -= ((*start % day) + day) % day;
		*end -= ((*end % day) + day) % day;
		if (*end <= *start) *end = *start + day;
		return;
	}
	if (*end <= *start) *end = *start + hour;
}

static void k_add(ak_span args, reply *r) {
	dsk_json *e = dsk_json_parse(args.p, args.len);
	if (!e || e->type != DSK_JOBJ) {
		dsk_json_free(e);
		reply_err(r, "calendar.add: bad arguments");
		return;
	}
	bool all_day = dsk_json_bool(e, "allDay", false);
	int64_t start = (int64_t)dsk_json_num(e, "startMs", 0), end = (int64_t)dsk_json_num(e, "endMs", 0);
	add_times(&start, &end, all_day);
	// The times as saved: the event's JSON again, with them.
	dsk_jw w = {0};
	dsk_jw_obj(&w);
	for (size_t i = 0; i < e->n; i++) {
		if (!strcmp(e->keys[i], "startMs") || !strcmp(e->keys[i], "endMs")) continue;
		dsk_jw_key(&w, e->keys[i]);
		dsk_jw_value(&w, &e->kids[i]);
	}
	dsk_jw_key(&w, "startMs");
	dsk_jw_int(&w, start);
	dsk_jw_key(&w, "endMs");
	dsk_jw_int(&w, end);
	dsk_jw_end(&w);
	size_t n;
	char *json = dsk_jw_take(&w, &n);
	dsk_json_free(e);
	e = dsk_json_parse(json, n);
	free(json);
	const char *title = dsk_json_str(e, "title", "");
	char *dates = event_dates(start, end, all_day);
	const char *loc = dsk_json_str(e, "location", "");
	char *body = *loc ? g_strdup_printf("%s\n%s", dates, loc) : g_strdup(dates);
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	char uid[48];
	snprintf(uid, sizeof uid, "%08x%08x%08x@asuka", g_random_int(), g_random_int(), g_random_int());
	ak_buf ev = {0};
	lx_vevent(&ev, uid, e);
	j->text = str_dup(ak_strn((const char *)ev.data, ev.len));
	ak_buf_free(&ev);
	j->args = e;
	j->work = add_event_work;
	j->done = answer_out;
	j->r = r;
	confirm(*title ? title : lx_word("newEvent"), body, lx_word("addToCalendar"), add_confirmed, j);
	g_free(dates);
	g_free(body);
}

// ---- The methods: EDS's or system.c's ----

#define EITHER(fn, book, eds, fallback) \
	static void fn(ak_span args, reply *r) { either(book, eds, fallback, args, r); }

EITHER(contacts_request, true, c_request, lx_contacts_request)
EITHER(contacts_list, true, c_list, lx_contacts_list)
EITHER(contacts_pick, true, c_pick, lx_contacts_pick)
EITHER(contacts_add, true, c_add, lx_contacts_add)
EITHER(calendar_request, false, c_request, lx_calendar_request)
EITHER(calendar_calendars, false, k_calendars, lx_calendar_not_allowed)
EITHER(calendar_events, false, k_events, lx_calendar_not_allowed)
EITHER(calendar_add, false, k_add, lx_calendar_add)

static const method_t methods[] = {
	{"contacts", "request", contacts_request},
	{"contacts", "list", contacts_list},
	{"contacts", "pick", contacts_pick},
	{"contacts", "add", contacts_add},
	{"calendar", "request", calendar_request},
	{"calendar", "calendars", calendar_calendars},
	{"calendar", "events", calendar_events},
	{"calendar", "add", calendar_add},
};

void lx_eds_modules(void) { lx_register_methods(methods, AK_LEN(methods)); }
