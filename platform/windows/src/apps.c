// apps.c: asuka/apps on Windows (its search ranked as every desktop
// host's): the Start menu's All apps, the shell's
// Applications folder (FOLDERID_AppsFolder: desktop apps by their
// shortcuts, packaged apps by their AppUserModelIDs, as PowerToys' Command
// Palette lists them), each item's icon from the shell
// (IShellItemImageFactory), and the item opened with its default verb.
// Each runs on a thread of its own, in a single-threaded apartment.
//
// search ranks names against what the user types, as a launcher does:
// the name is the query; it starts with it; a word of it does (a word
// starts after a space or a mark, at a capital in CamelCase, between
// letters and digits), or the query is the starts of its words in order
// ("vsc": Visual Studio Code); the name has it; it has its letters in
// order. Case and accents don't count (Windows' NFKD and lower case).
#include "modules.h"

#include <bcrypt.h>
#include <shobjidl.h>

// ---- Folding ----

typedef struct {
	uint32_t *v;
	size_t n, cap;
} runes;

static void push(runes *r, uint32_t c) {
	if (r->n == r->cap) {
		r->cap = r->cap ? r->cap * 2 : 16;
		r->v = wh_realloc(r->v, r->cap * sizeof *r->v);
	}
	r->v[r->n++] = c;
}

// decode is UTF-8's code points.
static runes decode(const char *s) {
	runes r = {0};
	const unsigned char *p = (const unsigned char *)s;
	while (*p) {
		uint32_t c = *p;
		int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		if (extra) c &= 0x3F >> extra;
		p++;
		for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++) c = c << 6 | (*p & 0x3F);
		push(&r, c);
	}
	return r;
}

static int utf16_of(uint32_t c, wchar_t out[2]) {
	if (c < 0x10000) {
		out[0] = (wchar_t)c;
		return 1;
	}
	c -= 0x10000;
	out[0] = (wchar_t)(0xD800 + (c >> 10)), out[1] = (wchar_t)(0xDC00 + (c & 0x3FF));
	return 2;
}

enum { K_OTHER, K_UPPER, K_LOWER, K_DIGIT, K_MARK };

static int kind_of(uint32_t c) {
	wchar_t w[2];
	int n = utf16_of(c, w);
	WORD t1 = 0, t3 = 0;
	GetStringTypeW(CT_CTYPE1, w, n, &t1);
	GetStringTypeW(CT_CTYPE3, w, n, &t3);
	if (t3 & C3_NONSPACING) return K_MARK;
	if (t1 & C1_UPPER) return K_UPPER;
	if (t1 & C1_ALPHA) return K_LOWER;
	if (t1 & C1_DIGIT) return K_DIGIT;
	return K_OTHER;
}

static bool is_space(uint32_t c) {
	wchar_t w[2];
	int n = utf16_of(c, w);
	WORD t = 0;
	GetStringTypeW(CT_CTYPE1, w, n, &t);
	return (t & C1_SPACE) != 0;
}

static uint32_t lower(uint32_t c) {
	if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 32 : c;
	wchar_t w[2], o[4];
	int n = utf16_of(c, w);
	int m = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w, n, o, 4, NULL, NULL, 0);
	if (m == 1) return o[0];
	if (m == 2 && o[0] >= 0xD800 && o[0] < 0xDC00) return 0x10000 + ((uint32_t)(o[0] - 0xD800) << 10) + (o[1] - 0xDC00);
	return c;
}

// plain is a letter with no decomposition, as it's typed on a keyboard
// without it ("": none).
static const char *plain(uint32_t c) {
	switch (c) {
	case 0xDF: return "ss";
	case 0xE6: return "ae";
	case 0x153: return "oe";
	case 0xF8: return "o";
	case 0x111: case 0xF0: return "d";
	case 0x142: case 0x140: return "l";
	case 0x127: return "h";
	case 0x131: return "i";
	case 0xFE: return "th";
	case 0x167: return "t";
	}
	return NULL;
}

static void push_plain(runes *out, uint32_t c) {
	const char *p = plain(c);
	if (!p) {
		push(out, c);
		return;
	}
	for (; *p; p++) push(out, (uint8_t)*p);
}

// fold_rune appends c folded: small, without accents, compatibility forms
// as their plain ones; nothing for a combining mark.
static void fold_rune(runes *out, uint32_t c) {
	if (c < 0x80) {
		push(out, lower(c));
		return;
	}
	c = lower(c);
	if (plain(c)) {
		push_plain(out, c);
		return;
	}
	wchar_t w[2], d[32];
	int n = utf16_of(c, w);
	int m = NormalizeString(NormalizationKD, w, n, d, WH_LEN(d));
	if (m <= 0) {
		push(out, c);
		return;
	}
	for (int i = 0; i < m; i++) {
		uint32_t x = d[i];
		if (x >= 0xD800 && x < 0xDC00 && i + 1 < m) x = 0x10000 + ((x - 0xD800) << 10) + (d[++i] - 0xDC00);
		if (kind_of(x) == K_MARK) continue;
		push_plain(out, lower(x));
	}
}

// fold is s folded, UTF-8 (free it).
static char *fold(const char *s) {
	runes in = decode(s), out = {0};
	for (size_t i = 0; i < in.n; i++) fold_rune(&out, in.v[i]);
	char *o = wh_alloc(out.n * 4 + 1);
	size_t k = 0;
	for (size_t i = 0; i < out.n; i++) {
		uint32_t c = out.v[i];
		if (c < 0x80) o[k++] = (char)c;
		else if (c < 0x800) o[k++] = (char)(0xC0 | c >> 6), o[k++] = (char)(0x80 | (c & 0x3F));
		else if (c < 0x10000) o[k++] = (char)(0xE0 | c >> 12), o[k++] = (char)(0x80 | ((c >> 6) & 0x3F)), o[k++] = (char)(0x80 | (c & 0x3F));
		else
			o[k++] = (char)(0xF0 | c >> 18), o[k++] = (char)(0x80 | ((c >> 12) & 0x3F)), o[k++] = (char)(0x80 | ((c >> 6) & 0x3F)),
			o[k++] = (char)(0x80 | (c & 0x3F));
	}
	free(in.v), free(out.v);
	return o;
}

// prepared is a name folded for matching: its runes, which of them start
// a word, and which are a word's rather than what's between words.
typedef struct {
	runes text;
	bool *start, *word;
} prepared;

static void prep_push(prepared *p, uint32_t c, bool start, bool word) {
	push(&p->text, c);
	p->start = wh_realloc(p->start, p->text.cap * sizeof *p->start);
	p->word = wh_realloc(p->word, p->text.cap * sizeof *p->word);
	p->start[p->text.n - 1] = start, p->word[p->text.n - 1] = word;
}

static prepared prepare(const char *s) {
	prepared p = {0};
	runes rs = decode(s);
	int prev = K_OTHER;
	for (size_t i = 0; i < rs.n; i++) {
		uint32_t r = rs.v[i];
		int k = kind_of(r);
		if (k == K_MARK) continue;  // its letter's (folded away)
		int next = K_OTHER;
		for (size_t j = i + 1; j < rs.n; j++) {
			int nk = kind_of(rs.v[j]);
			if (nk != K_MARK) {
				next = nk;
				break;
			}
		}
		bool word = k != K_OTHER;
		bool start = word && (prev == K_OTHER || (prev == K_LOWER && k == K_UPPER) || (prev == K_UPPER && k == K_UPPER && next == K_LOWER) ||
			((prev == K_DIGIT) != (k == K_DIGIT)));
		prev = k;
		if (!word && is_space(r)) {
			if (!p.text.n || p.text.v[p.text.n - 1] == ' ') continue;
			prep_push(&p, ' ', false, false);
			continue;
		}
		runes f = {0};
		fold_rune(&f, r);
		for (size_t j = 0; j < f.n; j++) prep_push(&p, f.v[j], start && j == 0, word);
		free(f.v);
	}
	if (p.text.n && p.text.v[p.text.n - 1] == ' ') p.text.n--;
	free(rs.v);
	return p;
}

static void prep_free(prepared *p) { free(p->text.v), free(p->start), free(p->word); }

static bool has_prefix(const uint32_t *s, size_t n, const uint32_t *q, size_t m) { return n >= m && !memcmp(s, q, m * sizeof *q); }

// initials reports whether q is made of starts of words, in order: one
// start (a letter or more) per word, words skipped between them.
static bool match_initials(uint32_t **words, size_t *lens, size_t nw, const uint32_t *q, size_t nq, size_t w, size_t i, uint8_t *failed) {
	if (i == nq) return true;
	if (w == nw || failed[w * (nq + 1) + i]) return false;
	size_t n = 0;
	while (n < lens[w] && i + n < nq && words[w][n] == q[i + n]) n++;
	for (size_t l = n; l > 0; l--)
		if (match_initials(words, lens, nw, q, nq, w + 1, i + l, failed)) return true;
	if (match_initials(words, lens, nw, q, nq, w + 1, i, failed)) return true;
	failed[w * (nq + 1) + i] = 1;
	return false;
}

static bool initials(const prepared *p, const uint32_t *q, size_t nq) {
	uint32_t **words = NULL;
	size_t *lens = NULL, nw = 0;
	for (size_t i = 0; i < p->text.n;) {
		if (!p->start[i]) {
			i++;
			continue;
		}
		size_t j = i + 1;
		while (j < p->text.n && p->word[j] && !p->start[j]) j++;
		words = wh_realloc(words, (nw + 1) * sizeof *words);
		lens = wh_realloc(lens, (nw + 1) * sizeof *lens);
		words[nw] = p->text.v + i, lens[nw++] = j - i;
		i = j;
	}
	uint8_t *failed = wh_alloc((nw + 1) * (nq + 1));
	bool ok = match_initials(words, lens, nw, q, nq, 0, 0, failed);
	free(failed), free(words), free(lens);
	return ok;
}

enum { RANK_EXACT, RANK_PREFIX, RANK_WORD, RANK_SUBSTRING, RANK_SUBSEQUENCE };

static int rank(const prepared *name, const runes *q, const runes *qc) {
	const uint32_t *n = name->text.v;
	size_t nl = name->text.n;
	if (nl == q->n && !memcmp(n, q->v, nl * sizeof *n)) return RANK_EXACT;
	if (has_prefix(n, nl, q->v, q->n)) return RANK_PREFIX;
	for (size_t i = 0; i < nl; i++)
		if (name->start[i] && has_prefix(n + i, nl - i, q->v, q->n)) return RANK_WORD;
	if (qc->n && initials(name, qc->v, qc->n)) return RANK_WORD;
	for (size_t i = 0; i + q->n <= nl; i++)
		if (!memcmp(n + i, q->v, q->n * sizeof *n)) return RANK_SUBSTRING;
	if (qc->n) {
		size_t j = 0;
		for (size_t i = 0; i < nl && j < qc->n; i++)
			if (n[i] == qc->v[j]) j++;
		if (j == qc->n) return RANK_SUBSEQUENCE;
	}
	return -1;
}

typedef struct {
	int rank;
	char *key;
	const char *name;
	int i;
} hit;

static int hit_cmp(const void *a, const void *b) {
	const hit *x = a, *y = b;
	if (x->rank != y->rank) return x->rank - y->rank;
	int c = strcmp(x->key, y->key);
	if (c) return c;
	c = strcmp(x->name, y->name);
	if (c) return c;
	return x->i - y->i;
}

// ---- The Applications folder ----

typedef struct {
	char *name, *id, *path;
} app;

static const GUID FOLDERID_AppsFolder_ = {0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
static const GUID BHID_EnumItems_ = {0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};

static char *item_name(IShellItem *item, SIGDN kind) {
	PWSTR p = NULL;
	if (FAILED(IShellItem_GetDisplayName(item, kind, &p)) || !p) return NULL;
	char *s = wh_utf8(p, -1, NULL);
	CoTaskMemFree(p);
	return s;
}

static int app_cmp(const void *a, const void *b) {
	const app *x = a, *y = b;
	char *kx = fold(x->name), *ky = fold(y->name);
	int c = strcmp(kx, ky);
	free(kx), free(ky);
	if (c) return c;
	c = strcmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

typedef struct {
	wh_reply *r;
	char *path, *out, *err;
	int px;
	uint8_t *args;
	size_t len;
} apps_job;

// list_work lists the Applications folder's items: each named as the
// Start menu names it, its ID its parsing name in the folder, its path
// the item's (shell:AppsFolder\<ID>); each once, by name.
static void list_work(void *ctx) {
	apps_job *j = ctx;
	IShellItem *folder = NULL;
	HRESULT hr = SHGetKnownFolderItem(&FOLDERID_AppsFolder_, 0, NULL, &IID_IShellItem, (void **)&folder);
	if (FAILED(hr) || !folder) {
		j->err = wh_alloc(64);
		sprintf(j->err, "the Applications folder (0x%lx)", (unsigned long)hr);
		return;
	}
	IEnumShellItems *items = NULL;
	hr = IShellItem_BindToHandler(folder, NULL, &BHID_EnumItems_, &IID_IEnumShellItems, (void **)&items);
	IShellItem_Release(folder);
	if (FAILED(hr) || !items) {
		j->err = wh_alloc(64);
		sprintf(j->err, "the Applications folder's items (0x%lx)", (unsigned long)hr);
		return;
	}
	app *v = NULL;
	size_t n = 0;
	for (;;) {
		IShellItem *item = NULL;
		ULONG got = 0;
		if (IEnumShellItems_Next(items, 1, &item, &got) != S_OK || !got || !item) break;
		char *name = item_name(item, SIGDN_NORMALDISPLAY), *id = item_name(item, SIGDN_PARENTRELATIVEPARSING);
		IShellItem_Release(item);
		if (!name || !*name || !id || !*id) {
			free(name), free(id);
			continue;
		}
		// Each once: the first of an ID.
		bool seen = false;
		for (size_t i = 0; i < n && !seen; i++) seen = !strcmp(v[i].id, id);
		if (seen) {
			free(name), free(id);
			continue;
		}
		v = wh_realloc(v, (n + 1) * sizeof *v);
		v[n].name = name, v[n].id = id;
		v[n].path = wh_alloc(strlen(id) + 20);
		sprintf(v[n].path, "shell:AppsFolder\\%s", id);
		n++;
	}
	IEnumShellItems_Release(items);
	qsort(v, n, sizeof *v, app_cmp);
	dsk_jw w = {0};
	dsk_jw_arr(&w);
	for (size_t i = 0; i < n; i++) {
		dsk_jw_obj(&w);
		dsk_jw_key(&w, "name"), dsk_jw_str(&w, v[i].name);
		dsk_jw_key(&w, "id"), dsk_jw_str(&w, v[i].id);
		dsk_jw_key(&w, "path"), dsk_jw_str(&w, v[i].path);
		dsk_jw_end(&w);
		free(v[i].name), free(v[i].id), free(v[i].path);
	}
	dsk_jw_aend(&w);
	free(v);
	j->out = dsk_jw_take(&w, NULL);
}

static void job_done(void *ctx) {
	apps_job *j = ctx;
	if (j->err) wh_reply_fail(j->r, "%s", j->err);
	else wh_reply_str(j->r, j->out ? j->out : "");
	free(j->path), free(j->out), free(j->err), free(j->args);
	free(j);
}

static void m_list(wh_reply *r, const uint8_t *a, size_t n) {
	(void)a, (void)n;
	apps_job *j = wh_alloc(sizeof *j);
	j->r = r;
	wh_off_main(list_work, job_done, j);
}

// search: {"names": [...], "query": "..."}, the answer a JSON array of
// the matching names' indices, best first.
static void search_work(void *ctx) {
	apps_job *j = ctx;
	dsk_json *in = dsk_json_parse((const char *)j->args, j->len);
	if (!in || in->type != DSK_JOBJ) {
		dsk_json_free(in);
		j->err = wh_strdup("apps.search: bad arguments");
		return;
	}
	const dsk_json *names = dsk_json_get(in, "names");
	size_t nn = names && names->type == DSK_JARR ? names->n : 0;
	prepared q = prepare(dsk_json_str(in, "query", ""));
	dsk_jw w = {0};
	dsk_jw_arr(&w);
	if (!q.text.n) {
		for (size_t i = 0; i < nn; i++) dsk_jw_int(&w, (int64_t)i);
	} else {
		runes qc = {0};
		for (size_t i = 0; i < q.text.n; i++)
			if (q.word[i]) push(&qc, q.text.v[i]);
		hit *hits = wh_alloc((nn ? nn : 1) * sizeof *hits);
		size_t nh = 0;
		for (size_t i = 0; i < nn; i++) {
			const char *name = names->kids[i].type == DSK_JSTR ? names->kids[i].str : "";
			prepared p = prepare(name);
			int r = rank(&p, &q.text, &qc);
			prep_free(&p);
			if (r >= 0) hits[nh++] = (hit){r, fold(name), name, (int)i};
		}
		qsort(hits, nh, sizeof *hits, hit_cmp);
		for (size_t i = 0; i < nh; i++) {
			dsk_jw_int(&w, hits[i].i);
			free(hits[i].key);
		}
		free(hits);
		free(qc.v);
	}
	dsk_jw_aend(&w);
	prep_free(&q);
	dsk_json_free(in);
	j->out = dsk_jw_take(&w, NULL);
}

static void m_search(wh_reply *r, const uint8_t *a, size_t n) {
	apps_job *j = wh_alloc(sizeof *j);
	j->r = r;
	j->args = wh_alloc(n + 1);
	memcpy(j->args, a, n);
	j->len = n;
	wh_off_main(search_work, job_done, j);
}

// is_shell_item reports whether path names an item of the shell's
// namespace rather than a file: shell:AppsFolder\..., ::{CLSID}.
static bool is_shell_item(const char *path) { return (strlen(path) > 6 && !_strnicmp(path, "shell:", 6)) || !strncmp(path, "::{", 3); }

static void open_work(void *ctx) {
	apps_job *j = ctx;
	DWORD e = wh_shell_open(NULL, NULL, j->path);
	if (e) j->err = wh_win_error(e);
}

static void m_open(wh_reply *r, const uint8_t *a, size_t n) {
	char *path = wh_strndup((const char *)a, n);
	if (!is_shell_item(path) && !wh_file_exists(path)) {
		wh_reply_fail(r, "no app at %s", path);
		free(path);
		return;
	}
	apps_job *j = wh_alloc(sizeof *j);
	j->r = r, j->path = path;
	wh_off_main(open_work, job_done, j);
}

// ---- Icons ----

// icon_file is where an app's icon px pixels square is kept: a file of
// the app's caches named for its path.
static char *icon_file(const char *path, int px) {
	char *low = wh_strdup(path);
	for (char *c = low; *c; c++) *c = (char)tolower((unsigned char)*c);
	uint8_t sum[32] = {0};
	BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)low, (ULONG)strlen(low), sum, sizeof sum);
	free(low);
	char name[64];
	int k = 0;
	for (int i = 0; i < 10; i++) k += sprintf(name + k, "%02x", sum[i]);
	sprintf(name + k, "-%d.png", px);
	char *dir = wh_cache_path("asuka-apps");
	dsk_mkdirs(dir);
	char *out = dsk_join(dir, name);
	free(dir);
	return out;
}

// The icons drawn since the app started (an item that's no file is drawn
// again once per run).
static SRWLOCK drawn_lock = SRWLOCK_INIT;
static char **drawn;
static uint32_t ndrawn;

static bool was_drawn(const char *png) {
	AcquireSRWLockShared(&drawn_lock);
	bool found = false;
	for (uint32_t i = 0; i < ndrawn && !found; i++) found = !strcmp(drawn[i], png);
	ReleaseSRWLockShared(&drawn_lock);
	return found;
}

static int64_t mtime(const char *path) {
	wchar_t *w = wh_wide(path, -1);
	WIN32_FILE_ATTRIBUTE_DATA a;
	bool ok = GetFileAttributesExW(w, GetFileExInfoStandard, &a);
	free(w);
	if (!ok) return -1;
	return (int64_t)((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime);
}

// shell_icon draws the shell's icon of an item, px pixels square
// (IShellItemImageFactory, without thumbnails), into a PNG at out.
static bool shell_icon(const char *path, int px, const char *out, char **err) {
	wchar_t *w = wh_wide(path, -1);
	IShellItem *item = NULL;
	HRESULT hr = SHCreateItemFromParsingName(w, NULL, &IID_IShellItem, (void **)&item);
	free(w);
	if (FAILED(hr) || !item) {
		*err = wh_alloc(strlen(path) + 16);
		sprintf(*err, "no app at %s", path);
		return false;
	}
	IShellItemImageFactory *f = NULL;
	IShellItem_QueryInterface(item, &IID_IShellItemImageFactory, (void **)&f);
	IShellItem_Release(item);
	if (!f) {
		*err = wh_alloc(strlen(path) + 16);
		sprintf(*err, "no icon for %s", path);
		return false;
	}
	HBITMAP hbm = NULL;
	SIZE size = {px, px};
	hr = IShellItemImageFactory_GetImage(f, size, SIIGBF_ICONONLY, &hbm);
	IShellItemImageFactory_Release(f);
	if (FAILED(hr) || !hbm) {
		*err = wh_alloc(strlen(path) + 32);
		sprintf(*err, "no icon for %s (0x%lx)", path, (unsigned long)hr);
		return false;
	}
	DIBSECTION ds;
	bool ok = false;
	if (GetObjectW(hbm, sizeof ds, &ds) && ds.dsBm.bmBits && ds.dsBm.bmBitsPixel == 32) {
		int bw = ds.dsBm.bmWidth, bh = abs(ds.dsBm.bmHeight), stride = ds.dsBm.bmWidthBytes;
		uint8_t *pix = wh_alloc((size_t)bw * bh * 4);
		const uint8_t *src = ds.dsBm.bmBits;
		for (int y = 0; y < bh; y++) {
			int row = ds.dsBmih.biHeight > 0 ? bh - 1 - y : y;  // bottom-up
			memcpy(pix + (size_t)y * bw * 4, src + (size_t)row * stride, (size_t)bw * 4);
		}
		// Through a file beside it, renamed over it: a reader never sees
		// half a file.
		char *tmp = wh_alloc(strlen(out) + 16);
		sprintf(tmp, "%s.%lu.tmp", out, GetCurrentThreadId());
		wchar_t *wt = wh_wide(tmp, -1), *wo = wh_wide(out, -1);
		ok = wh_encode_image_file(wt, false, pix, bw, bh) && MoveFileExW(wt, wo, MOVEFILE_REPLACE_EXISTING);
		if (!ok) DeleteFileW(wt);
		free(wt), free(wo), free(tmp), free(pix);
	}
	DeleteObject(hbm);
	if (!ok) *err = wh_strdup("the icon isn't a 32-bit bitmap");
	return ok;
}

static void icon_work(void *ctx) {
	apps_job *j = ctx;
	char *out = icon_file(j->path, j->px);
	bool shell = is_shell_item(j->path);
	if (!shell && !wh_file_exists(j->path)) {
		j->err = wh_alloc(strlen(j->path) + 16);
		sprintf(j->err, "no app at %s", j->path);
		free(out);
		return;
	}
	// Used again if drawn since the app's file last changed, or, for an
	// item that's no file, since the app started.
	int64_t t = mtime(out);
	bool fresh = t >= 0 && (shell ? was_drawn(out) : mtime(j->path) < t);
	if (fresh || shell_icon(j->path, j->px, out, &j->err)) {
		if (!fresh) {
			AcquireSRWLockExclusive(&drawn_lock);
			drawn = wh_realloc(drawn, (ndrawn + 1) * sizeof *drawn);
			drawn[ndrawn++] = wh_strdup(out);
			ReleaseSRWLockExclusive(&drawn_lock);
		}
		j->out = out;
		return;
	}
	free(out);
}

// icon: {path, size}: the icon the shell shows for an item, as a PNG in
// the caches, size points square at the system's DPI; answers its path.
static void m_icon(wh_reply *r, const uint8_t *a, size_t n) {
	dsk_json *o = dsk_json_parse((const char *)a, n);
	const char *path = dsk_json_str(o, "path", "");
	double size = dsk_json_num(o, "size", 0);
	if (!o || !*path || !(size > 0)) {
		dsk_json_free(o);
		wh_reply_fail(r, "apps.icon: bad arguments");
		return;
	}
	apps_job *j = wh_alloc(sizeof *j);
	j->r = r, j->path = wh_strdup(path);
	j->px = (int)ceil(size * H.dpi / 96);
	dsk_json_free(o);
	wh_off_main(icon_work, job_done, j);
}

static const wh_method_def apps_methods[] = {
	{"list", m_list}, {"icon", m_icon}, {"search", m_search}, {"open", m_open}, {NULL, NULL},
};

const wh_module_def wh_apps_module = {"apps", apps_methods};
