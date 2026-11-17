// apps.c: platform/apps on Linux: the desktop entries a menu shows, read as
// the XDG specs say (the base directories, the desktop entry spec), their
// icons from the GTK icon theme, drawn by GSK's Cairo renderer, and their
// apps launched by GIO with the display's launch context.
//
// search ranks names against what the user types, as a launcher does
// (every desktop host's): the name is the query; it starts with it; a word
// of it does (a word starts after a space or a mark, at a capital in
// CamelCase, between letters and digits), or the query is the starts of
// its words in order ("vsc": Visual Studio Code); the name has it; it has
// its letters in order. Case and accents don't count (GLib's NFKD and
// lower case, the letters with no decomposition as they're typed).
#include "host.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- Folding and ranking ----

typedef AK_VEC(gunichar) runes;

enum { K_OTHER, K_UPPER, K_LOWER, K_DIGIT, K_MARK };

static int kind_of(gunichar c) {
	GUnicodeType t = g_unichar_type(c);
	if (t == G_UNICODE_NON_SPACING_MARK || t == G_UNICODE_ENCLOSING_MARK) return K_MARK;
	if (g_unichar_isupper(c) || g_unichar_istitle(c)) return K_UPPER;
	if (g_unichar_isalpha(c)) return K_LOWER;
	if (g_unichar_isdigit(c) || t == G_UNICODE_LETTER_NUMBER || t == G_UNICODE_OTHER_NUMBER) return K_DIGIT;
	return K_OTHER;
}

// plain is a letter with no decomposition as it's typed on a keyboard
// without it (NULL: none).
static const char *plain(gunichar c) {
	switch (c) {
	case 0xdf: return "ss";
	case 0xe6: return "ae";
	case 0x153: return "oe";
	case 0xf8: return "o";
	case 0x111: return "d";
	case 0xf0: return "d";
	case 0x142: return "l";
	case 0x127: return "h";
	case 0x131: return "i";
	case 0xfe: return "th";
	case 0x140: return "l";
	case 0x167: return "t";
	}
	return NULL;
}

static void push_plain(runes *out, gunichar c, bool *first, void (*mark)(void *ctx, bool first), void *ctx) {
	const char *p = plain(c);
	if (p) {
		for (; *p; p++) {
			ak_vec_push(out, (gunichar)*p);
			if (mark) mark(ctx, *first);
			*first = false;
		}
		return;
	}
	ak_vec_push(out, c);
	if (mark) mark(ctx, *first);
	*first = false;
}

// fold_rune appends c folded: small, without accents, compatibility forms
// as their plain ones (no runes for a combining mark).
static void fold_rune(runes *out, gunichar c, void (*mark)(void *ctx, bool first), void *ctx) {
	bool first = true;
	if (c < 0x80) {
		push_plain(out, g_unichar_tolower(c), &first, mark, ctx);
		return;
	}
	c = g_unichar_tolower(c);
	if (plain(c)) {
		push_plain(out, c, &first, mark, ctx);
		return;
	}
	char buf[8];
	int n = g_unichar_to_utf8(c, buf);
	gchar *d = g_utf8_normalize(buf, n, G_NORMALIZE_ALL);  // NFKD
	for (const char *p = d; p && *p; p = g_utf8_next_char(p)) {
		gunichar x = g_utf8_get_char(p);
		if (g_unichar_type(x) == G_UNICODE_NON_SPACING_MARK) continue;
		push_plain(out, g_unichar_tolower(x), &first, mark, ctx);
	}
	g_free(d);
}

static runes fold(const char *s) {
	runes r = {0};
	for (const char *p = s; *p; p = g_utf8_next_char(p)) fold_rune(&r, g_utf8_get_char(p), NULL, NULL);
	return r;
}

// A name folded for matching: its runes, which of them start a word, and
// which are a word's (letters and digits) rather than what's between
// words.
typedef struct {
	runes text;
	AK_VEC(bool) start, word;
} prepared;

typedef struct {
	prepared *p;
	bool start, word;
} mark_ctx;

static void mark(void *data, bool first) {
	mark_ctx *m = data;
	bool s = m->start && first;
	ak_vec_push(&m->p->start, s);
	ak_vec_push(&m->p->word, m->word);
}

// prepare folds s rune by rune, marking where its words start: at its
// first letter or digit, after what's between words, at a capital after a
// small letter (TextEdit), at the last capital of a run before a small
// letter (XMLParser: Parser), and between letters and digits (Office365).
// Spaces run together into one; those at the ends go.
static prepared prepare(const char *s) {
	prepared p = {0};
	gunichar *rs = g_utf8_to_ucs4_fast(s, -1, NULL);
	int prev = K_OTHER;
	for (size_t i = 0; rs[i]; i++) {
		int k = kind_of(rs[i]);
		if (k == K_MARK) continue;  // its letter's (folded away)
		int next = K_OTHER;
		for (size_t j = i + 1; rs[j]; j++) {
			int nk = kind_of(rs[j]);
			if (nk != K_MARK) {
				next = nk;
				break;
			}
		}
		bool is_word = k != K_OTHER;
		bool start = is_word && (prev == K_OTHER || (prev == K_LOWER && k == K_UPPER) || (prev == K_UPPER && k == K_UPPER && next == K_LOWER) ||
		                         ((prev == K_DIGIT) != (k == K_DIGIT)));
		prev = k;
		if (!is_word && g_unichar_isspace(rs[i])) {
			if (!p.text.len || p.text.data[p.text.len - 1] == ' ') continue;
			ak_vec_push(&p.text, (gunichar)' ');
			ak_vec_push(&p.start, false);
			ak_vec_push(&p.word, false);
			continue;
		}
		mark_ctx m = {&p, start, is_word};
		fold_rune(&p.text, rs[i], mark, &m);
	}
	g_free(rs);
	if (p.text.len && p.text.data[p.text.len - 1] == ' ') {
		p.text.len--;
		p.start.len--;
		p.word.len--;
	}
	return p;
}

static void prepared_free(prepared *p) {
	ak_vec_free(&p->text);
	ak_vec_free(&p->start);
	ak_vec_free(&p->word);
}

static bool has_prefix(const gunichar *s, size_t n, const runes *q) { return n >= q->len && !memcmp(s, q->data, q->len * sizeof *s); }

static bool contains(const runes *s, const runes *q) {
	for (size_t i = 0; i + q->len <= s->len; i++)
		if (!memcmp(s->data + i, q->data, q->len * sizeof *q->data)) return true;
	return false;
}

static bool subsequence(const runes *s, const runes *q) {
	size_t j = 0;
	for (size_t i = 0; i < s->len && j < q->len; i++)
		if (s->data[i] == q->data[j]) j++;
	return j == q->len;
}

typedef struct {
	const gunichar *p;
	size_t n;
} word_t;

// initials reports whether q is made of starts of words, in order: one
// start (a letter or more) per word, words skipped between them.
static bool initials_from(const word_t *words, size_t nw, const runes *q, size_t w, size_t i, uint8_t *failed) {
	if (i == q->len) return true;
	if (w == nw || failed[w * (q->len + 1) + i]) return false;
	size_t n = 0;
	while (n < words[w].n && i + n < q->len && words[w].p[n] == q->data[i + n]) n++;
	for (size_t l = n; l > 0; l--)
		if (initials_from(words, nw, q, w + 1, i + l, failed)) return true;
	if (initials_from(words, nw, q, w + 1, i, failed)) return true;
	failed[w * (q->len + 1) + i] = 1;
	return false;
}

static bool initials(const prepared *p, const runes *q) {
	AK_VEC(word_t) words = {0};
	for (size_t i = 0; i < p->text.len;) {
		if (!p->start.data[i]) {
			i++;
			continue;
		}
		size_t j = i + 1;
		while (j < p->text.len && p->word.data[j] && !p->start.data[j]) j++;
		ak_vec_push(&words, ((word_t){p->text.data + i, j - i}));
		i = j;
	}
	uint8_t *failed = calloc((words.len + 1) * (q->len + 1), 1);
	if (!failed) abort();
	bool ok = initials_from(words.data, words.len, q, 0, 0, failed);
	free(failed);
	ak_vec_free(&words);
	return ok;
}

enum { R_EXACT, R_PREFIX, R_WORD, R_SUBSTRING, R_SUBSEQUENCE };

// rank is how a name matches the query (q folded, qc without what's
// between its words), or -1 if it doesn't.
static int rank(const prepared *name, const runes *q, const runes *qc) {
	const runes *n = &name->text;
	if (n->len == q->len && !memcmp(n->data, q->data, q->len * sizeof *q->data)) return R_EXACT;
	if (has_prefix(n->data, n->len, q)) return R_PREFIX;
	for (size_t i = 0; i < n->len; i++)
		if (name->start.data[i] && has_prefix(n->data + i, n->len - i, q)) return R_WORD;
	if (qc->len && initials(name, qc)) return R_WORD;
	if (contains(n, q)) return R_SUBSTRING;
	if (qc->len && subsequence(n, qc)) return R_SUBSEQUENCE;
	return -1;
}

static int cmp_runes(const runes *a, const runes *b) {
	size_t n = a->len < b->len ? a->len : b->len;
	for (size_t i = 0; i < n; i++)
		if (a->data[i] != b->data[i]) return a->data[i] < b->data[i] ? -1 : 1;
	return a->len < b->len ? -1 : a->len > b->len;
}

typedef struct {
	int rank;
	runes key;
	const char *name;
	size_t i;
} hit;

static int by_hit(const void *a, const void *b) {
	const hit *x = a, *y = b;
	if (x->rank != y->rank) return x->rank - y->rank;
	int c = cmp_runes(&x->key, &y->key);
	if (c) return c;
	c = strcmp(x->name, y->name);
	if (c) return c;
	return x->i < y->i ? -1 : x->i > y->i;
}

// lx_app_rank writes the indices of the names that match query, best
// first, into out (n at most) and returns how many; an empty query (or
// only spaces) is every index as it is.
size_t lx_app_rank(const char *const *names, size_t n, const char *query, size_t *out) {
	prepared q = prepare(query);
	if (!q.text.len) {
		prepared_free(&q);
		for (size_t i = 0; i < n; i++) out[i] = i;
		return n;
	}
	runes qc = {0};
	for (size_t i = 0; i < q.text.len; i++)
		if (q.word.data[i]) ak_vec_push(&qc, q.text.data[i]);
	hit *hits = malloc((n ? n : 1) * sizeof *hits);
	if (!hits) abort();
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		prepared p = prepare(names[i]);
		int r = rank(&p, &q.text, &qc);
		prepared_free(&p);
		if (r >= 0) hits[k++] = (hit){r, fold(names[i]), names[i], i};
	}
	if (k > 1) qsort(hits, k, sizeof *hits, by_hit);
	for (size_t i = 0; i < k; i++) {
		out[i] = hits[i].i;
		ak_vec_free(&hits[i].key);
	}
	free(hits);
	ak_vec_free(&qc);
	prepared_free(&q);
	return k;
}

// ---- Off the main thread ----

typedef struct {
	char *(*work)(void *ctx, char **err);  // the answer (malloc'd)
	void *ctx;
	reply *r;
	char *out, *err;
} job;

static gboolean job_done(gpointer data) {
	job *j = data;
	if (j->err)
		reply_err(j->r, "%s", j->err);
	else
		reply_str(j->r, S(j->out));
	free(j->out);
	free(j->err);
	free(j);
	return 0;
}

static gpointer job_thread(gpointer data) {
	job *j = data;
	j->out = j->work(j->ctx, &j->err);
	g_idle_add_full(G_PRIORITY_DEFAULT, job_done, j, NULL);
	return NULL;
}

static void off_main(char *(*work)(void *ctx, char **err), void *ctx, reply *r) {
	job *j = calloc(1, sizeof *j);
	if (!j) abort();
	j->work = work;
	j->ctx = ctx;
	j->r = r;
	g_thread_unref(g_thread_new("asuka-apps", job_thread, j));
}

// ---- Desktop entries ----

// An entry's [Desktop Entry] group, what a launcher reads of it.
typedef struct {
	char *type, *exec, *try_exec, *icon;
	AK_VEC(char *) name_keys;  // Name[locale] ("" Name's)
	AK_VEC(char *) names;
	bool no_display, hidden, dbus;
	char *only_show_in, *not_show_in;
} entry;

static void entry_free(entry *e) {
	free(e->type);
	free(e->exec);
	free(e->try_exec);
	free(e->icon);
	for (uint32_t i = 0; i < e->names.len; i++) {
		free(e->names.data[i]);
		free(e->name_keys.data[i]);
	}
	ak_vec_free(&e->names);
	ak_vec_free(&e->name_keys);
	free(e->only_show_in);
	free(e->not_show_in);
}

// unescape reads a string value's escapes: \s, \n, \t, \r, \\.
static char *unescape(const char *v) {
	ak_buf b = {0};
	for (; *v; v++) {
		if (*v != '\\' || !v[1]) {
			ak_buf_u8(&b, (uint8_t)*v);
			continue;
		}
		v++;
		switch (*v) {
		case 's': ak_buf_u8(&b, ' '); break;
		case 'n': ak_buf_u8(&b, '\n'); break;
		case 't': ak_buf_u8(&b, '\t'); break;
		case 'r': ak_buf_u8(&b, '\r'); break;
		default: ak_buf_u8(&b, (uint8_t)*v);
		}
	}
	char *out = str_dup(ak_strn((const char *)b.data, b.len));
	ak_buf_free(&b);
	return out;
}

static bool truthy(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1"); }

static void trim(char **s) {
	while (**s == ' ' || **s == '\t') (*s)++;
	size_t n = strlen(*s);
	while (n && ((*s)[n - 1] == ' ' || (*s)[n - 1] == '\t' || (*s)[n - 1] == '\r')) (*s)[--n] = 0;
}

// parse_entry reads a desktop entry's main group; false if it has none.
static bool parse_entry(char *text, entry *e) {
	bool in = false, found = false;
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		trim(&line);
		if (!*line || *line == '#') continue;
		if (*line == '[') {
			if (found && in) break;  // the main group's done: actions and others follow
			in = !strcmp(line, "[Desktop Entry]");
			found |= in;
			continue;
		}
		char *eq = strchr(line, '=');
		if (!in || !eq) continue;
		*eq = 0;
		char *k = line, *v = eq + 1;
		trim(&k);
		trim(&v);
		char *locale = "";
		char *br = strchr(k, '[');
		if (br && br > k && k[strlen(k) - 1] == ']') {
			*br = 0;
			locale = br + 1;
			locale[strlen(locale) - 1] = 0;
		}
		if (!strcmp(k, "Name")) {
			bool dup = false;
			for (uint32_t i = 0; i < e->name_keys.len; i++) dup |= !strcmp(e->name_keys.data[i], locale);
			if (!dup) {
				ak_vec_push(&e->name_keys, str_dup(ak_cstr(locale)));
				ak_vec_push(&e->names, unescape(v));
			}
		}
		if (*locale) continue;
		char **slot = !strcmp(k, "Type") ? &e->type : !strcmp(k, "Exec") ? &e->exec : !strcmp(k, "TryExec") ? &e->try_exec : !strcmp(k, "Icon") ? &e->icon : !strcmp(k, "OnlyShowIn") ? &e->only_show_in : !strcmp(k, "NotShowIn") ? &e->not_show_in : NULL;
		if (slot) {
			free(*slot);
			*slot = !strcmp(k, "Type") || !strcmp(k, "Exec") || !strcmp(k, "OnlyShowIn") || !strcmp(k, "NotShowIn") ? str_dup(ak_cstr(v)) : unescape(v);
		}
		if (!strcmp(k, "NoDisplay")) e->no_display = truthy(v);
		if (!strcmp(k, "Hidden")) e->hidden = truthy(v);
		if (!strcmp(k, "DBusActivatable")) e->dbus = truthy(v);
	}
	return found;
}

// in_list reports whether a list value (semicolons between, \; one in an
// item) has one of the current desktops.
static bool in_list(const char *list, char **desktops) {
	if (!list) return false;
	ak_buf item = {0};
	bool hit = false;
	for (const char *p = list;; p++) {
		if (*p == '\\' && p[1] == ';') {
			ak_buf_u8(&item, ';');
			p++;
			continue;
		}
		if (*p == ';' || !*p) {
			if (item.len)
				for (char **d = desktops; d && *d; d++) hit |= !g_ascii_strcasecmp(*d, lx_buf_cstr(&item));
			ak_buf_reset(&item);
			if (!*p) break;
			continue;
		}
		ak_buf_u8(&item, (uint8_t)*p);
	}
	ak_buf_free(&item);
	return hit;
}

// shown reports whether a menu shows the entry on the current desktops:
// an application's, not hidden from menus or deleted, meant for one of
// the desktops and not kept from them, its TryExec there.
static bool shown(const entry *e, char **desktops) {
	if (!e->type || strcmp(e->type, "Application") || e->hidden || e->no_display || (str_empty(e->exec) && !e->dbus)) return false;
	if ((e->only_show_in && *e->only_show_in && !in_list(e->only_show_in, desktops)) || in_list(e->not_show_in, desktops)) return false;
	if (!str_empty(e->try_exec)) {
		if (e->try_exec[0] == '/') {
			struct stat st;
			if (stat(e->try_exec, &st) != 0 || S_ISDIR(st.st_mode) || !(st.st_mode & 0111)) return false;
		} else {
			gchar *p = g_find_program_in_path(e->try_exec);
			bool ok = p != NULL;
			g_free(p);
			if (!ok) return false;
		}
	}
	return true;
}

// localized is the entry's name in the first of the user's languages it
// has, else its Name.
static const char *localized(const entry *e, const gchar *const *langs) {
	for (const gchar *const *l = langs; *l; l++)
		for (uint32_t i = 0; i < e->names.len; i++)
			if (!strcmp(e->name_keys.data[i], *l) && *e->names.data[i]) return e->names.data[i];
	for (uint32_t i = 0; i < e->names.len; i++)
		if (!*e->name_keys.data[i]) return e->names.data[i];
	return "";
}

typedef struct {
	char *name, *id, *path;
	runes key;
} app_t;

typedef struct {
	AK_VEC(app_t) apps;
	GHashTable *seen;
	char **desktops;
	const gchar *const *langs;
} walk_t;

// walk reads a folder's desktop entries, its subfolders' too: an entry's
// ID is its path under the folder, / made -, and the first entry of an ID
// hides the later ones, whatever it says.
static void walk(walk_t *w, const char *dir, const char *rel) {
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		char *p = dsk_join(dir, de->d_name);
		char *r = *rel ? g_strdup_printf("%s-%s", rel, de->d_name) : g_strdup(de->d_name);
		struct stat st;
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
			walk(w, p, r);
		} else if (g_str_has_suffix(de->d_name, ".desktop") && !g_hash_table_contains(w->seen, r)) {
			size_t n;
			char *text = dsk_read_file(p, &n);  // through a link: Flatpak's exports are
			if (text) {
				g_hash_table_add(w->seen, g_strdup(r));
				entry e = {0};
				if (parse_entry(text, &e) && shown(&e, w->desktops)) {
					const char *name = localized(&e, w->langs);
					if (*name) {
						app_t a = {str_dup(ak_cstr(name)), str_dup(ak_cstr(r)), str_dup(ak_cstr(p)), fold(name)};
						ak_vec_push(&w->apps, a);
					}
				}
				entry_free(&e);
				free(text);
			}
		}
		g_free(r);
		free(p);
	}
	closedir(d);
}

static int by_name(const void *a, const void *b) {
	const app_t *x = a, *y = b;
	int c = cmp_runes(&x->key, &y->key);
	if (c) return c;
	c = strcmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

// list_apps reads the desktop entries of the folders a menu reads, the
// first first: the user's ($XDG_DATA_HOME/applications), then each of
// $XDG_DATA_DIRS's, then Flatpak's exports (the user's and the system's)
// and Snap's where $XDG_DATA_DIRS doesn't have them. Sorted by name (case
// and accents aside), then as written, then by path.
static char *list_apps(void *ctx, char **err) {
	(void)ctx;
	(void)err;
	walk_t w = {.seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)};
	const char *cd = getenv("XDG_CURRENT_DESKTOP");
	w.desktops = g_strsplit(cd ? cd : "", ":", -1);
	w.langs = g_get_language_names();
	AK_VEC(char *) dirs = {0};
	ak_vec_push(&dirs, str_dup(ak_cstr(g_get_user_data_dir())));
	for (const gchar *const *d = g_get_system_data_dirs(); *d; d++)
		if ((*d)[0] == '/') ak_vec_push(&dirs, str_dup(ak_cstr(*d)));
	ak_vec_push(&dirs, dsk_join(g_get_user_data_dir(), "flatpak/exports/share"));
	ak_vec_push(&dirs, str_dup(ak_cstr("/var/lib/flatpak/exports/share")));
	GHashTable *done = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (uint32_t i = 0; i < dirs.len; i++) {
		char *canon = g_canonicalize_filename(dirs.data[i], NULL);
		char *apps = dsk_join(canon, "applications");
		if (!g_hash_table_contains(done, apps)) {
			g_hash_table_add(done, g_strdup(apps));
			walk(&w, apps, "");
		}
		free(apps);
		g_free(canon);
		free(dirs.data[i]);
	}
	if (!g_hash_table_contains(done, "/var/lib/snapd/desktop/applications")) walk(&w, "/var/lib/snapd/desktop/applications", "");
	ak_vec_free(&dirs);
	g_hash_table_unref(done);
	g_hash_table_unref(w.seen);
	g_strfreev(w.desktops);
	if (w.apps.len > 1) qsort(w.apps.data, w.apps.len, sizeof *w.apps.data, by_name);
	dsk_jw j = {0};
	dsk_jw_arr(&j);
	for (uint32_t i = 0; i < w.apps.len; i++) {
		app_t *a = &w.apps.data[i];
		dsk_jw_obj(&j);
		dsk_jw_key(&j, "name");
		dsk_jw_str(&j, a->name);
		dsk_jw_key(&j, "id");
		dsk_jw_str(&j, a->id);
		dsk_jw_key(&j, "path");
		dsk_jw_str(&j, a->path);
		dsk_jw_end(&j);
		free(a->name);
		free(a->id);
		free(a->path);
		ak_vec_free(&a->key);
	}
	dsk_jw_aend(&j);
	ak_vec_free(&w.apps);
	size_t n;
	return dsk_jw_take(&j, &n);
}

static void apps_list(ak_span args, reply *r) {
	(void)args;
	off_main(list_apps, NULL, r);
}

// search: {"names": [...], "query": "..."}, the answer a JSON array of
// the matching names' indices, best first.
static char *search(void *ctx, char **err) {
	char *args = ctx;
	dsk_json *j = dsk_json_parse(args, strlen(args));
	free(args);
	const dsk_json *names = dsk_json_get(j, "names");
	if (!j || (names && names->type != DSK_JARR)) {
		dsk_json_free(j);
		*err = str_dup(ak_cstr("apps.search: bad arguments"));
		return NULL;
	}
	size_t n = names ? names->n : 0;
	const char **list = malloc((n ? n : 1) * sizeof *list);
	size_t *out = malloc((n ? n : 1) * sizeof *out);
	if (!list || !out) abort();
	for (size_t i = 0; i < n; i++) list[i] = names->kids[i].type == DSK_JSTR ? names->kids[i].str : "";
	size_t k = lx_app_rank(list, n, dsk_json_str(j, "query", ""), out);
	dsk_jw w = {0};
	dsk_jw_arr(&w);
	for (size_t i = 0; i < k; i++) dsk_jw_int(&w, (int64_t)out[i]);
	dsk_jw_aend(&w);
	free(list);
	free(out);
	dsk_json_free(j);
	size_t len;
	return dsk_jw_take(&w, &len);
}

static void apps_search(ak_span args, reply *r) { off_main(search, str_dup(args), r); }

// ---- Icons ----

// monitor_scale is the first monitor's scale (fractional), 1 without one.
static double monitor_scale(void) {
	GdkDisplay *d = gdk_display_get_default();
	if (!d) return 1;
	GdkMonitor *m = g_list_model_get_item(gdk_display_get_monitors(d), 0);
	if (!m) return 1;
	double s = gdk_monitor_get_scale ? gdk_monitor_get_scale(m) : 0;
	if (s <= 0) s = gdk_monitor_get_scale_factor(m);
	g_object_unref(m);
	return s < 1 ? 1 : s;
}

// icon_paintable is an entry's Icon at a size and scale, and the file
// it's drawn from (NULL for one of GTK's resources): a file it names, or
// the theme's icon of that name (or of the name without an extension, as
// entries written for other launchers have it), else the theme's
// application-x-executable.
static GdkPaintable *icon_paintable(const char *icon, int size, int scale, char **file) {
	*file = NULL;
	if (icon && icon[0] == '/') {
		struct stat st;
		if (stat(icon, &st) == 0 && !S_ISDIR(st.st_mode)) {
			GFile *f = g_file_new_for_path(icon);
			GtkIconPaintable *p = gtk_icon_paintable_new_for_file(f, size, scale);
			g_object_unref(f);
			if (p) {
				*file = str_dup(ak_cstr(icon));
				return (GdkPaintable *)p;
			}
		}
		icon = NULL;
	}
	GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
	const char *name = "application-x-executable";
	char bare[512] = "";
	if (icon && *icon) {
		snprintf(bare, sizeof bare, "%s", icon);
		char *dot = strrchr(bare, '.');
		if (dot && (!strcmp(dot, ".png") || !strcmp(dot, ".svg") || !strcmp(dot, ".xpm"))) *dot = 0;
		if (gtk_icon_theme_has_icon(theme, icon))
			name = icon;
		else if (*bare && gtk_icon_theme_has_icon(theme, bare))
			name = bare;
	}
	GtkIconPaintable *p = gtk_icon_theme_lookup_icon(theme, name, NULL, size, scale, GTK_TEXT_DIR_LTR, 0);
	if (!p) return NULL;
	GFile *f = gtk_icon_paintable_get_file(p);
	if (f) {
		char *path = g_file_get_path(f);
		if (path) *file = str_dup(ak_cstr(path));
		g_free(path);
		g_object_unref(f);
	}
	return (GdkPaintable *)p;
}

static GskRenderer *renderer;  // a Cairo renderer of the display's, drawing icons without a window

// fresh reports whether the icon at png was drawn after each of sources
// last changed.
static bool fresh(const char *png, const char *a, const char *b) {
	struct stat st, s;
	if (stat(png, &st) != 0) return false;
	const char *src[] = {a, b};
	for (int i = 0; i < 2; i++)
		if (src[i] && stat(src[i], &s) == 0 && s.st_mtime >= st.st_mtime) return false;
	return true;
}

typedef struct {
	GdkTexture *t;
	char *out;
} png_job;

static char *write_png(void *ctx, char **err) {
	png_job *j = ctx;
	GBytes *png = gdk_texture_save_to_png_bytes(j->t);
	gsize n;
	const void *p = g_bytes_get_data(png, &n);
	char *dir = dsk_dir(j->out);
	dsk_mkdirs(dir);
	free(dir);
	char *out = NULL;
	if (dsk_write_file(j->out, p, n))  // whole, then moved in place
		out = j->out;
	else {
		*err = g_strdup_printf("can't write %s", j->out);
		free(j->out);
	}
	g_bytes_unref(png);
	g_object_unref(j->t);
	free(j);
	return out;
}

// icon: {"path", "size"}: the entry's icon as a PNG in the caches, size
// points square at the first monitor's scale, and its path. A file drawn
// since the entry and the icon's file last changed is used again.
static void apps_icon(ak_span args, reply *r) {
	dsk_json *j = dsk_json_parse(args.p, args.len);
	const char *path = dsk_json_str(j, "path", "");
	double size = dsk_json_num(j, "size", 0);
	char *text = NULL, *file = NULL;
	size_t n;
	entry e = {0};
	if (!*path || !(size > 0)) {
		reply_err(r, "apps.icon: bad arguments");
		goto out;
	}
	if (!(text = dsk_read_file(path, &n))) {
		reply_err(r, "no app at %s", path);
		goto out;
	}
	if (!parse_entry(text, &e)) {
		reply_err(r, "%s isn't a desktop entry", path);
		goto out;
	}
	double scale = monitor_scale();
	int px = (int)ceil(size * scale);
	GdkPaintable *p = icon_paintable(e.icon, (int)ceil(size), (int)ceil(scale), &file);
	if (!p) {
		reply_err(r, "no icon for %s", path);
		goto out;
	}
	gchar *sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path, -1);
	char name[96];
	snprintf(name, sizeof name, "asuka-apps/%.20s-%d.png", sum, px);
	g_free(sum);
	char *png = dsk_join(lx_cache_dir(), name);
	if (fresh(png, path, file)) {
		g_object_unref(p);
		reply_str(r, png);
		free(png);
		goto out;
	}
	if (!renderer) {
		GskRenderer *rd = gsk_cairo_renderer_new();
		if (gsk_renderer_realize_for_display ? gsk_renderer_realize_for_display(rd, gdk_display_get_default(), NULL) : gsk_renderer_realize(rd, NULL, NULL))
			renderer = rd;
		else
			g_object_unref(rd);
	}
	GdkTexture *t = NULL;
	if (renderer) {
		GtkSnapshot *s = gtk_snapshot_new();
		gdk_paintable_snapshot(p, (GdkSnapshot *)s, px, px);
		GskRenderNode *node = gtk_snapshot_free_to_node(s);
		if (node) {
			graphene_rect_t vp = {{0, 0}, {(float)px, (float)px}};
			t = gsk_renderer_render_texture(renderer, node, &vp);
			gsk_render_node_unref(node);
		}
	}
	g_object_unref(p);
	if (!t) {
		reply_err(r, "couldn't draw the icon of %s", path);
		free(png);
		goto out;
	}
	png_job *pj = malloc(sizeof *pj);
	if (!pj) abort();
	*pj = (png_job){t, png};
	off_main(write_png, pj, r);
out:
	entry_free(&e);
	free(text);
	free(file);
	dsk_json_free(j);
}

typedef struct {
	GAppInfo *info;
	GAppLaunchContext *ctx;
	reply *r;
} launch_t;

static void launched(GObject *src, GAsyncResult *res, gpointer data) {
	(void)src;
	launch_t *l = data;
	GError *e = NULL;
	if (g_app_info_launch_uris_finish(l->info, res, &e))
		reply_ok(l->r, NULL, 0);
	else
		reply_err(l->r, "%s", e ? e->message : "can't launch it");
	if (e) g_error_free(e);
	g_object_unref(l->info);
	if (l->ctx) g_object_unref(l->ctx);
	free(l);
}

// open: a desktop entry's path: its app launched (D-Bus activation for an
// app that has it), with the display's launch context: the app gets its
// startup notification ID or activation token, and takes the focus.
static void apps_open(ak_span args, reply *r) {
	char *path = str_dup(args);
	struct stat st;
	if (stat(path, &st) != 0) {
		reply_err(r, "no app at %s", path);
		free(path);
		return;
	}
	GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(path);
	if (!info) {
		reply_err(r, "%s isn't a desktop entry of an app", path);
		free(path);
		return;
	}
	free(path);
	launch_t *l = malloc(sizeof *l);
	if (!l) abort();
	l->info = (GAppInfo *)info;
	l->ctx = (GAppLaunchContext *)gdk_display_get_app_launch_context(gdk_display_get_default());
	l->r = r;
	g_app_info_launch_uris_async(l->info, NULL, l->ctx, NULL, launched, l);
}

static const method_t apps_methods[] = {
	{"apps", "list", apps_list},
	{"apps", "icon", apps_icon},
	{"apps", "open", apps_open},
	{"apps", "search", apps_search},
};

static void apps_modules(void) { lx_register_methods(apps_methods, AK_LEN(apps_methods)); }

const area lx_area_apps = {
	.name = "apps",
	.modules = apps_modules,
};
