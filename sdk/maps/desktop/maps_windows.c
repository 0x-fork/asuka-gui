// maps.View on Windows: a map drawn here, of OpenStreetMap's tiles
// (Windows has no map control for desktop apps: its MapControl is UWP's,
// and needs a Bing Maps key), as GNOME Maps' libshumate does on Linux.
// The tiles come from tile.openstreetmap.org as its usage policy asks: a
// User-Agent naming the app, two downloads at a time, the tiles kept on
// disk (the app's caches, asuka-map-tiles) and used for a week before
// they're asked for again, the attribution "© OpenStreetMap
// contributors" shown on the map, linking to its copyright page. While a
// tile comes, the one above it that's there is drawn bigger.
//
// Satellite and Hybrid have no free imagery source: they show the
// streets, as Muted does; showsUser has no location layer (nothing
// shown, as on Linux). The user moves it with a drag (a finger's pan),
// zooms with the wheel (a touchpad's pinch comes as Ctrl and the wheel),
// a finger's pinch, a double click (Shift: out), and + and -; the arrow
// keys move it. A click on a pin shows its title and subtitle in a
// callout above it. It has no size of its own: its style gives it one.
#include "asuka_windows.h"
#include "maps.asuka.h"

#include <math.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	tile_size = 256,
	max_zoom = 19,
	downloads = 2,    // at a time, as the policy allows
	mem_tiles = 384,  // decoded tiles kept
	timer_wheel = 1,  // the wheel's region report, once it stops
	timer_zoom = 2,   // a double click's zoom, animated
};
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TILE_FRESH_MS (7ULL * 24 * 3600 * 1000)  // the tile policy's least
#define ATTRIBUTION L"© OpenStreetMap contributors"

// ---- GDI+, its flat API ----

typedef float REAL;
static struct {
	bool tried, ok;
	int(WINAPI *CreateFromHDC)(HDC, void **);
	int(WINAPI *DeleteGraphics)(void *);
	int(WINAPI *SetSmoothingMode)(void *, int);
	int(WINAPI *SetPixelOffsetMode)(void *, int);
	int(WINAPI *SetCompositingQuality)(void *, int);
	int(WINAPI *TranslateWorldTransform)(void *, REAL, REAL, int);
	int(WINAPI *CreatePath)(int, void **);
	int(WINAPI *DeletePath)(void *);
	int(WINAPI *AddPathArc)(void *, REAL, REAL, REAL, REAL, REAL, REAL);
	int(WINAPI *AddPathLine)(void *, REAL, REAL, REAL, REAL);
	int(WINAPI *StartPathFigure)(void *);
	int(WINAPI *ClosePathFigure)(void *);
	int(WINAPI *CreateSolidFill)(UINT32, void **);
	int(WINAPI *DeleteBrush)(void *);
	int(WINAPI *CreatePen1)(UINT32, REAL, int, void **);
	int(WINAPI *DeletePen)(void *);
	int(WINAPI *FillPath)(void *, void *, void *);
	int(WINAPI *DrawPath)(void *, void *, void *);
	int(WINAPI *FillEllipse)(void *, void *, REAL, REAL, REAL, REAL);
	int(WINAPI *FillRectangle)(void *, void *, REAL, REAL, REAL, REAL);
} gp;

static bool gdiplus(void) {
	if (gp.tried) return gp.ok;
	gp.tried = true;
	HMODULE m = LoadLibraryExW(L"gdiplus.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!m) return false;
	struct {
		UINT32 version;
		void *debug;
		BOOL no_thread, no_codecs;
	} in = {1, NULL, FALSE, FALSE};
	int(WINAPI * startup)(ULONG_PTR *, const void *, void *) = NULL;
	*(FARPROC *)&startup = GetProcAddress(m, "GdiplusStartup");
	ULONG_PTR token;
	if (!startup || startup(&token, &in, NULL) != 0) return false;
#define F(name) *(FARPROC *)&gp.name = GetProcAddress(m, "Gdip" #name)
	F(CreateFromHDC);
	F(DeleteGraphics);
	F(SetSmoothingMode);
	F(SetPixelOffsetMode);
	F(SetCompositingQuality);
	F(TranslateWorldTransform);
	F(CreatePath);
	F(DeletePath);
	F(AddPathArc);
	F(AddPathLine);
	F(StartPathFigure);
	F(ClosePathFigure);
	F(CreateSolidFill);
	F(DeleteBrush);
	F(CreatePen1);
	F(DeletePen);
	F(FillPath);
	F(DrawPath);
	F(FillEllipse);
	F(FillRectangle);
#undef F
	gp.ok = gp.CreateFromHDC && gp.DeleteGraphics && gp.SetSmoothingMode && gp.SetPixelOffsetMode && gp.SetCompositingQuality &&
	        gp.TranslateWorldTransform && gp.CreatePath && gp.DeletePath && gp.AddPathArc && gp.AddPathLine && gp.StartPathFigure &&
	        gp.ClosePathFigure && gp.CreateSolidFill && gp.DeleteBrush && gp.CreatePen1 && gp.DeletePen && gp.FillPath && gp.DrawPath &&
	        gp.FillEllipse && gp.FillRectangle;
	return gp.ok;
}

static UINT32 argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) { return (UINT32)a << 24 | (UINT32)r << 16 | (UINT32)g << 8 | b; }

// graphics draws antialiased into dc (NULL: no GDI+).
static void *graphics(HDC dc) {
	void *g = NULL;
	if (!gdiplus() || gp.CreateFromHDC(dc, &g) != 0) return NULL;
	gp.SetSmoothingMode(g, 4);       // AntiAlias
	gp.SetPixelOffsetMode(g, 4);     // Half: pixel centers at .5
	gp.SetCompositingQuality(g, 4);  // AssumeLinear
	return g;
}

static void fill_path(void *g, UINT32 color, void *path) {
	void *b = NULL;
	gp.CreateSolidFill(color, &b);
	gp.FillPath(g, b, path);
	gp.DeleteBrush(b);
}

static void fill_round_rect(void *g, UINT32 color, REAL x, REAL y, REAL w, REAL h, REAL r) {
	void *p = NULL;
	gp.CreatePath(0, &p);
	r = fminf(r, fminf(w / 2, h / 2));
	REAL d = 2 * r;
	gp.StartPathFigure(p);
	gp.AddPathArc(p, x, y, d, d, 180, 90);
	gp.AddPathArc(p, x + w - d, y, d, d, 270, 90);
	gp.AddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
	gp.AddPathArc(p, x, y + h - d, d, d, 90, 90);
	gp.ClosePathFigure(p);
	fill_path(g, color, p);
	gp.DeletePath(p);
}

// ---- The tiles, shared by the maps ----

typedef struct {
	int32_t z, x, y;
} tkey;

static bool same(tkey a, tkey b) { return a.z == b.z && a.x == b.x && a.y == b.y; }

typedef struct view view;

static struct {
	bool made;
	struct {
		tkey k;
		HBITMAP bmp;
		uint64_t used;
	} *bmps;
	size_t nbmps;
	tkey *want;  // asked for, not here yet
	size_t nwant;
	struct {
		tkey k;
		ULONGLONG until;  // couldn't be had: not asked again until then
	} *failed;
	size_t nfailed;
	uint64_t use;
	view **views;
	size_t nviews;
	HDC dc;  // selects a tile to draw it
	int workers;
	char *dir, *agent;
	// The downloads' queue, newest last: what's on screen now first.
	CRITICAL_SECTION mu;
	CONDITION_VARIABLE cv;
	struct {
		tkey k;
		LONG64 frame;
	} *jobs;
	size_t njobs, capjobs;
	volatile LONG64 frame;
} T;

static void tiles(void) {
	if (T.made) return;
	T.made = true;
	InitializeCriticalSection(&T.mu);
	InitializeConditionVariable(&T.cv);
	T.dir = asuka_sprintf("%s\\asuka-map-tiles", asuka_desktop_cache_dir());
	// Asuka and the app (not the user), to OpenStreetMap's servers, as
	// their policies ask.
	const char *app = asuka_desktop_app_id();
	T.agent = asuka_sprintf("Asuka/1 (%s; Windows; +https://github.com/egoist/asuka)", app && *app ? app : asuka_desktop_app_name());
}

// tile_url is where a tile comes from: ASUKA_MAP_TILES ({z}, {x}, {y}),
// else OpenStreetMap's.
static char *tile_url(tkey k) {
	const char *t = getenv("ASUKA_MAP_TILES");
	if (!t || !*t) t = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
	asuka_writer w = {0};
	for (const char *p = t; *p;) {
		int v = -1;
		if (!strncmp(p, "{z}", 3)) v = k.z;
		else if (!strncmp(p, "{x}", 3)) v = k.x;
		else if (!strncmp(p, "{y}", 3)) v = k.y;
		if (v >= 0) {
			char n[16];
			int len = snprintf(n, sizeof n, "%d", v);
			asuka_writer_put(&w, n, (size_t)len);
			p += 3;
		} else {
			asuka_writer_put(&w, p++, 1);
		}
	}
	asuka_writer_u8(&w, 0);
	char *s = asuka_strdup((char *)w.data);
	asuka_writer_free(&w);
	return s;
}

// decode is an image file's pixels as a 256×256 tile's BGRA (of
// malloc's), or NULL.
static uint8_t *decode(const char *path) {
	UINT w, h;
	uint8_t *px = NULL;
	if (!asuka_win_image_load(path, &w, &h, &px)) return NULL;
	if (w == tile_size && h == tile_size) return px;
	uint8_t *out = malloc(tile_size * tile_size * 4);  // another size: its nearest pixels
	for (int y = 0; y < tile_size; y++)
		for (int x = 0; x < tile_size; x++)
			memcpy(out + (y * tile_size + x) * 4, px + ((size_t)(y * h / tile_size) * w + x * w / tile_size) * 4, 4);
	free(px);
	return out;
}

static ULONGLONG now_ms(void) { return GetTickCount64(); }

// age is how long ago a file was written (ms), or false if it isn't there.
static bool age(const char *path, ULONGLONG *ms) {
	wchar_t *w = asuka_wide(path);
	WIN32_FILE_ATTRIBUTE_DATA a;
	bool ok = GetFileAttributesExW(w, GetFileExInfoStandard, &a);
	free(w);
	if (!ok) return false;
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULONGLONG now = (ULONGLONG)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
	ULONGLONG at = (ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime;
	*ms = now > at ? (now - at) / 10000 : 0;
	return true;
}

// load is a tile's pixels: the disk's, if fresh; else the server's, kept
// on disk; else the disk's, however old. Off the main thread.
static uint8_t *load(tkey k, char **err) {
	char *path = asuka_sprintf("%s\\%d\\%d\\%d.png", T.dir, k.z, k.x, k.y);
	ULONGLONG ms;
	bool stale = false;
	if (age(path, &ms)) {
		if (ms < TILE_FRESH_MS) {
			uint8_t *px = decode(path);
			if (px) {
				free(path);
				return px;
			}
		} else {
			stale = true;
		}
	}
	char *url = tile_url(k);
	asuka_header ua = {"User-Agent", T.agent};
	asuka_response r = {0};
	asuka_error e = {0};
	uint8_t *px = NULL;
	if (!asuka_fetch("GET", url, &ua, 1, NULL, 0, 30000, NULL, &r, &e)) {
		*err = asuka_strdup(e.message ? e.message : "no answer");
	} else if (r.status != 200) {
		*err = asuka_sprintf("HTTP %d", r.status);
	} else {
		char *dir = asuka_sprintf("%s\\%d\\%d", T.dir, k.z, k.x);
		wchar_t *wd = asuka_wide(dir);
		SHCreateDirectoryExW(NULL, wd, NULL);
		free(wd);
		free(dir);
		char *tmp = asuka_sprintf("%s.%lu.tmp", path, GetCurrentThreadId());
		wchar_t *wt = asuka_wide(tmp), *wp = asuka_wide(path);
		HANDLE f = CreateFileW(wt, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		DWORD n = 0;
		bool written = f != INVALID_HANDLE_VALUE && WriteFile(f, r.body.data, (DWORD)r.body.len, &n, NULL) && n == r.body.len;
		if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
		px = written ? decode(tmp) : NULL;
		if (px) MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING);
		else DeleteFileW(wt);
		if (!px) *err = asuka_strdup(written ? "not an image" : "can't keep it");
		free(wt);
		free(wp);
		free(tmp);
	}
	asuka_error_free(&e);
	asuka_response_free(&r);
	free(url);
	if (!px && stale) {
		px = decode(path);
		if (px) {
			free(*err);
			*err = NULL;
		}
	}
	free(path);
	return px;
}

static void unwant(tkey k) {
	for (size_t i = 0; i < T.nwant; i++)
		if (same(T.want[i], k)) {
			T.want[i] = T.want[--T.nwant];
			return;
		}
}

typedef struct {
	tkey k;
	uint8_t *px;
	char *err;
} came;

static void tile_came(view *v);
static void tile_failed(view *v, const char *err);

static void trim(void) {
	// The least recently drawn go, down to three quarters.
	while (T.nbmps > mem_tiles * 3 / 4) {
		size_t oldest = 0;
		for (size_t i = 1; i < T.nbmps; i++)
			if (T.bmps[i].used < T.bmps[oldest].used) oldest = i;
		DeleteObject(T.bmps[oldest].bmp);
		T.bmps[oldest] = T.bmps[--T.nbmps];
	}
}

static HBITMAP dib(int w, int h, void **bits) {
	BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
	return CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

// loaded keeps a tile that came, and redraws the maps; one that couldn't
// be had is asked again in half a minute, and the maps say so once.
static void loaded(void *p) {
	came *c = p;
	unwant(c->k);
	if (!c->px) {
		T.failed = realloc(T.failed, (T.nfailed + 1) * sizeof *T.failed);
		T.failed[T.nfailed].k = c->k;
		T.failed[T.nfailed++].until = now_ms() + 30000;
		for (size_t i = 0; i < T.nviews; i++) tile_failed(T.views[i], c->err ? c->err : "unknown");
	} else {
		void *bits = NULL;
		HBITMAP bmp = dib(tile_size, tile_size, &bits);
		if (bmp) {
			memcpy(bits, c->px, tile_size * tile_size * 4);
			T.bmps = realloc(T.bmps, (T.nbmps + 1) * sizeof *T.bmps);
			T.bmps[T.nbmps].k = c->k;
			T.bmps[T.nbmps].bmp = bmp;
			T.bmps[T.nbmps++].used = ++T.use;
			if (T.nbmps > mem_tiles) trim();
			for (size_t i = 0; i < T.nviews; i++) tile_came(T.views[i]);
		}
	}
	free(c->px);
	free(c->err);
	free(c);
}

static void forget_want(void *p) {
	tkey *k = p;
	unwant(*k);
	free(k);
}

// work loads tiles, the newest asked first; those asked frames ago (off
// screen since) are let go.
static void work(void *unused) {
	for (;;) {
		EnterCriticalSection(&T.mu);
		while (T.njobs == 0) SleepConditionVariableCS(&T.cv, &T.mu, INFINITE);
		tkey k = T.jobs[T.njobs - 1].k;
		LONG64 frame = T.jobs[--T.njobs].frame;
		LeaveCriticalSection(&T.mu);
		if (InterlockedCompareExchange64(&T.frame, 0, 0) > frame + 2) {
			tkey *kk = malloc(sizeof *kk);
			*kk = k;
			asuka_desktop_post(forget_want, kk);
			continue;
		}
		came *c = calloc(1, sizeof *c);
		c->k = k;
		c->px = load(k, &c->err);
		asuka_desktop_post(loaded, c);
	}
}

// get is a tile's bitmap, if it's here; else it's asked for (once).
static HBITMAP get(tkey k, bool ask) {
	for (size_t i = 0; i < T.nbmps; i++)
		if (same(T.bmps[i].k, k)) {
			T.bmps[i].used = ++T.use;
			return T.bmps[i].bmp;
		}
	if (!ask) return NULL;
	for (size_t i = 0; i < T.nwant; i++)
		if (same(T.want[i], k)) return NULL;
	for (size_t i = 0; i < T.nfailed; i++)
		if (same(T.failed[i].k, k)) {
			if (now_ms() < T.failed[i].until) return NULL;
			T.failed[i] = T.failed[--T.nfailed];
			break;
		}
	T.want = realloc(T.want, (T.nwant + 1) * sizeof *T.want);
	T.want[T.nwant++] = k;
	EnterCriticalSection(&T.mu);
	if (T.njobs == T.capjobs) {
		T.capjobs = T.capjobs ? T.capjobs * 2 : 64;
		T.jobs = realloc(T.jobs, T.capjobs * sizeof *T.jobs);
	}
	T.jobs[T.njobs].k = k;
	T.jobs[T.njobs++].frame = InterlockedCompareExchange64(&T.frame, 0, 0);
	while (T.workers < downloads && asuka_desktop_spawn(work, NULL)) T.workers++;
	WakeConditionVariable(&T.cv);
	LeaveCriticalSection(&T.mu);
	return NULL;
}

// ---- The view ----

typedef struct {
	char *id, *title, *subtitle;
	double lat, lng;
	bool colored;
	uint32_t rgba;
} marker;

struct view {
	maps_View base;
	maps_View_events events;
	int refs;  // the view's and its window's
	HWND hwnd;
	// Where it shows: the center, as fractions of the world (Web
	// Mercator, x east from 180°W, y south from 85°N), and the zoom (the
	// world is 256·2^zoom points wide).
	double cx, cy, zoom;
	bool has_pending;
	maps_Region pending;  // waits for the view's size
	marker *markers;
	size_t nmarkers;
	char *selected;  // the marker whose callout shows
	bool interactive;
	// The mouse.
	bool down, dragging, doubled, moved;
	POINT down_at, last_up_at, gesture;
	double down_cx, down_cy;
	ULONGLONG last_up;
	ULONGLONG gesture_arg;
	// A double click's zoom, animated.
	double zoom_from, zoom_to;
	POINT zoom_at;
	ULONGLONG zoom_start;
	HBITMAP back;  // the back buffer
	SIZE back_size;
	double attr_k;  // the attribution's width, measured at this scale
	int attr_w;
	bool failed;  // tiles couldn't be had: said once
	bool destroyed;
};

static void unref(view *v) {
	if (--v->refs > 0) return;
	for (size_t i = 0; i < v->nmarkers; i++) {
		free(v->markers[i].id);
		free(v->markers[i].title);
		free(v->markers[i].subtitle);
	}
	free(v->markers);
	free(v->selected);
	if (v->back) DeleteObject(v->back);
	free(v);
}

static double scale(view *v) {
	static UINT(WINAPI * dpi_for)(HWND);
	static bool looked;
	if (!looked) {
		looked = true;
		*(FARPROC *)&dpi_for = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	}
	UINT dpi = dpi_for && v->hwnd ? dpi_for(v->hwnd) : 0;
	if (!dpi) {
		HDC dc = GetDC(NULL);
		dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
		ReleaseDC(NULL, dc);
	}
	return dpi ? dpi / 96.0 : 1;
}

static RECT client(view *v) {
	RECT r = {0};
	if (v->hwnd) GetClientRect(v->hwnd, &r);
	return r;
}

// size is the view's size in points.
static void size(view *v, double *w, double *h) {
	RECT r = client(v);
	double k = scale(v);
	*w = (r.right - r.left) / k;
	*h = (r.bottom - r.top) / k;
}

// world is the world's width in points at the zoom.
static double world(view *v) { return tile_size * exp2(v->zoom); }

static double merc_y(double lat) {
	lat = fmax(fmin(lat, 85.05112878), -85.05112878) * M_PI / 180;
	return (1 - log(tan(lat) + 1 / cos(lat)) / M_PI) / 2;
}

static double merc_lat(double y) { return atan(sinh(M_PI * (1 - 2 * y))) * 180 / M_PI; }

static void invalidate(view *v) {
	if (v->hwnd) InvalidateRect(v->hwnd, NULL, FALSE);
}

// clamp keeps the zoom in range and the map's poles in view; the world
// wraps east and west.
static void clamp(view *v) {
	double w, h;
	size(v, &w, &h);
	double least = h > 0 ? log2(fmax(h, 1) / tile_size) : 0;
	v->zoom = fmax(fmin(v->zoom, max_zoom), fmax(least, 0));
	v->cx -= floor(v->cx);
	double half = h / 2 / world(v);
	v->cy = fmax(fmin(v->cy, 1 - half), half);
}

// apply shows the pending region, once the view has a size: its center,
// zoomed to show its span.
static void apply(view *v) {
	double w, h;
	size(v, &w, &h);
	if (!v->has_pending || w <= 0 || h <= 0) return;
	maps_Region r = v->pending;
	v->has_pending = false;
	double zoom = max_zoom;
	if (r.lngDelta > 0) zoom = fmin(zoom, log2(w * 360 / (tile_size * r.lngDelta)));
	if (r.latDelta > 0) {
		double span = fabs(merc_y(r.center.lat + r.latDelta / 2) - merc_y(r.center.lat - r.latDelta / 2));
		if (span > 0) zoom = fmin(zoom, log2(h / (tile_size * span)));
	}
	v->zoom = fmax(zoom, 0);
	v->cx = (r.center.lng + 180) / 360;
	v->cy = merc_y(r.center.lat);
	clamp(v);
	invalidate(v);
}

// report tells the app the region the user moved the map to.
static void report(view *v) {
	if (!v->moved || v->destroyed) return;
	v->moved = false;
	double w, h;
	size(v, &w, &h);
	double W = world(v);
	double top = merc_lat(v->cy - h / 2 / W), bottom = merc_lat(v->cy + h / 2 / W);
	maps_Region r = {{merc_lat(v->cy), v->cx * 360 - 180}, fabs(top - bottom), fmin(w / W * 360, 360)};
	maps_View_regionChange(v->events, &r);
}

// at is the place at a point of the view (pixels).
static maps_Coordinate at(view *v, POINT p) {
	RECT r = client(v);
	double k = scale(v), W = world(v);
	double x = v->cx + (p.x / k - (r.right - r.left) / k / 2) / W;
	double y = v->cy + (p.y / k - (r.bottom - r.top) / k / 2) / W;
	x -= floor(x);
	return (maps_Coordinate){merc_lat(y), x * 360 - 180};
}

// point is where a place is in the view (pixels): the copy of the world
// nearest the center.
static void point(view *v, double lat, double lng, double *x, double *y) {
	RECT r = client(v);
	double k = scale(v), W = world(v);
	double dx = (lng + 180) / 360 - v->cx;
	dx -= round(dx);
	double dy = merc_y(lat) - v->cy;
	*x = (r.right - r.left) / 2.0 + dx * W * k;
	*y = (r.bottom - r.top) / 2.0 + dy * W * k;
}

// zoom_by zooms by dz around a point (pixels), which stays where it is.
static void zoom_by(view *v, double dz, POINT p) {
	RECT r = client(v);
	double k = scale(v);
	double ox = (p.x - (r.right - r.left) / 2.0) / k, oy = (p.y - (r.bottom - r.top) / 2.0) / k;
	double W0 = world(v);
	double px = v->cx + ox / W0, py = v->cy + oy / W0;
	v->zoom += dz;
	clamp(v);
	double W = world(v);
	v->cx = px - ox / W;
	v->cy = py - oy / W;
	clamp(v);
	v->moved = true;
	invalidate(v);
}

// pan_by moves the map by dx, dy pixels.
static void pan_by(view *v, double dx, double dy) {
	double k = scale(v), W = world(v);
	v->cx -= dx / k / W;
	v->cy -= dy / k / W;
	clamp(v);
	v->moved = true;
	invalidate(v);
}

static void tile_came(view *v) { invalidate(v); }

static void tile_failed(view *v, const char *err) {
	if (v->failed || v->destroyed) return;
	v->failed = true;
	char *m = asuka_sprintf("maps: the map's tiles can't be loaded: %s", err);
	maps_View_error(v->events, m);
	free(m);
}

// pin is a marker's pin's box (pixels): its tip at the place.
static void pin(view *v, const marker *m, double *x0, double *y0, double *x1, double *y1) {
	double k = scale(v), x, y;
	point(v, m->lat, m->lng, &x, &y);
	double w = 26 * k, h = 36 * k;
	*x0 = x - w / 2;
	*y0 = y - h;
	*x1 = x + w / 2;
	*y1 = y;
}

// marker_at is the marker whose pin is at p, the one drawn on top.
static const marker *marker_at(view *v, POINT p) {
	for (size_t i = v->nmarkers; i-- > 0;) {
		double x0, y0, x1, y1;
		pin(v, &v->markers[i], &x0, &y0, &x1, &y1);
		if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) return &v->markers[i];
	}
	return NULL;
}

// font is Segoe UI at pt points.
static HFONT font(double k, double pt, int weight) {
	LOGFONTW lf = {0};
	lf.lfHeight = -(LONG)lround(pt * k);
	lf.lfWeight = weight;
	lf.lfQuality = CLEARTYPE_QUALITY;
	wcscpy(lf.lfFaceName, L"Segoe UI");
	return CreateFontIndirectW(&lf);
}

// attribution is the attribution's box (pixels), at the bottom right.
static RECT attribution(view *v) {
	RECT r = client(v);
	double k = scale(v);
	if (v->attr_k != k) {
		HFONT f = font(k, 11, 400);
		HDC dc = CreateCompatibleDC(NULL);
		HGDIOBJ old = SelectObject(dc, f);
		RECT m = {0, 0, 4000, 4000};
		DrawTextW(dc, ATTRIBUTION, -1, &m, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		SelectObject(dc, old);
		DeleteDC(dc);
		DeleteObject(f);
		v->attr_k = k;
		v->attr_w = m.right - m.left;
	}
	LONG w = v->attr_w + (LONG)lround(10 * k), h = (LONG)lround(16 * k);
	return (RECT){r.right - w, r.bottom - h, r.right, r.bottom};
}

static bool attribution_hit(view *v, POINT p) {
	RECT a = attribution(v);
	return p.x >= a.left && p.x < a.right && p.y >= a.top && p.y < a.bottom;
}

static void blit(HDC dc, HBITMAP bmp, int x, int y, int w, int h, int sx, int sy, int ss) {
	HGDIOBJ old = SelectObject(T.dc, bmp);
	if (w == ss && h == ss) BitBlt(dc, x, y, w, h, T.dc, sx, sy, SRCCOPY);
	else StretchBlt(dc, x, y, w, h, T.dc, sx, sy, ss, ss, SRCCOPY);
	SelectObject(T.dc, old);
}

// paint_tiles draws the tiles of the zoom level nearest the view's at its
// pixels (a 2× display: the next level's), each scaled to fit; one that
// isn't here yet is drawn from one above it that is.
static void paint_tiles(view *v, HDC dc, RECT r) {
	InterlockedIncrement64(&T.frame);
	if (!T.dc) T.dc = CreateCompatibleDC(NULL);
	double k = scale(v);
	double zf = v->zoom + log2(k);
	int z = (int)fmax(0, fmin(round(zf), max_zoom));
	int n = 1 << z;
	double world_px = tile_size * exp2(zf), tile_px = world_px / n;
	int W = r.right - r.left, H = r.bottom - r.top;
	double ox = v->cx * world_px - W / 2.0, oy = v->cy * world_px - H / 2.0;
	int tx0 = (int)floor(ox / tile_px), tx1 = (int)floor((ox + W - 1) / tile_px);
	int ty0 = (int)fmax(floor(oy / tile_px), 0), ty1 = (int)fmin(floor((oy + H - 1) / tile_px), n - 1);
	for (int ty = ty0; ty <= ty1; ty++)
		for (int tx = tx0; tx <= tx1; tx++) {
			int x0 = (int)floor(tx * tile_px - ox), x1 = (int)floor((tx + 1) * tile_px - ox);
			int y0 = (int)floor(ty * tile_px - oy), y1 = (int)floor((ty + 1) * tile_px - oy);
			int wx = ((tx % n) + n) % n;
			HBITMAP bmp = get((tkey){z, wx, ty}, true);
			if (bmp) {
				blit(dc, bmp, x0, y0, x1 - x0, y1 - y0, 0, 0, tile_size);
				continue;
			}
			for (int dz = 1; dz <= z && dz <= 6; dz++) {
				bmp = get((tkey){z - dz, wx >> dz, ty >> dz}, false);
				if (bmp) {
					int part = tile_size >> dz;
					blit(dc, bmp, x0, y0, x1 - x0, y1 - y0, (wx & ((1 << dz) - 1)) * part, (ty & ((1 << dz) - 1)) * part, part);
					break;
				}
			}
		}
}

// paint_callout draws a marker's title and subtitle in a bubble above
// its pin.
static void paint_callout(view *v, HDC dc, void *g, const marker *m) {
	double k = scale(v);
	double x0, y0, x1, y1;
	pin(v, m, &x0, &y0, &x1, &y1);
	HFONT tf = font(k, 14, 600), sf = font(k, 12, 400);
	wchar_t *title = asuka_wide(m->title), *sub = asuka_wide(m->subtitle);
	RECT tr = {0}, sr = {0};
	struct {
		HFONT f;
		wchar_t *s;
		RECT *r;
		COLORREF color;
	} lines[2] = {{tf, title, &tr, RGB(0x1A, 0x1A, 0x1A)}, {sf, sub, &sr, RGB(0x5F, 0x5F, 0x5F)}};
	for (int i = 0; i < 2; i++)
		if (*lines[i].s) {
			*lines[i].r = (RECT){0, 0, 4000, 4000};
			HGDIOBJ old = SelectObject(dc, lines[i].f);
			DrawTextW(dc, lines[i].s, -1, lines[i].r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, old);
		}
	LONG pad = (LONG)lround(10 * k);
	LONG tw = tr.right - tr.left, sw = sr.right - sr.left;
	LONG w = (tw > sw ? tw : sw) + 2 * pad;
	LONG h = (tr.bottom - tr.top) + (sr.bottom - sr.top) + 2 * (LONG)lround(7 * k);
	LONG cx = (LONG)((x0 + x1) / 2), bottom = (LONG)y0 - (LONG)lround(6 * k);
	RECT box = {cx - w / 2, bottom - h, cx + w / 2, bottom};
	fill_round_rect(g, argb(40, 0, 0, 0), (REAL)box.left, (REAL)box.top + (REAL)k, (REAL)(box.right - box.left), (REAL)(box.bottom - box.top),
	                (REAL)(8 * k));
	fill_round_rect(g, argb(255, 255, 255, 255), (REAL)box.left, (REAL)box.top, (REAL)(box.right - box.left), (REAL)(box.bottom - box.top),
	                (REAL)(8 * k));
	SetBkMode(dc, TRANSPARENT);
	LONG y = box.top + (LONG)lround(7 * k);
	for (int i = 0; i < 2; i++) {
		if (!*lines[i].s) continue;
		HGDIOBJ old = SelectObject(dc, lines[i].f);
		SetTextColor(dc, lines[i].color);
		LONG lh = lines[i].r->bottom - lines[i].r->top;
		RECT rr = {box.left + pad, y, box.right - pad, y + lh};
		DrawTextW(dc, lines[i].s, -1, &rr, DT_SINGLELINE | DT_NOPREFIX);
		SelectObject(dc, old);
		y += lh;
	}
	free(title);
	free(sub);
	DeleteObject(tf);
	DeleteObject(sf);
}

// paint_markers draws each marker's pin, and the selected one's callout.
static void paint_markers(view *v, HDC dc) {
	if (!v->nmarkers) return;
	void *g = graphics(dc);
	if (!g) return;
	REAL k = (REAL)scale(v);
	const marker *sel = NULL;
	for (size_t i = 0; i < v->nmarkers; i++) {
		const marker *m = &v->markers[i];
		double x0, y0, x1, y1;
		pin(v, m, &x0, &y0, &x1, &y1);
		REAL cx = (REAL)(x0 + x1) / 2, w = (REAL)(x1 - x0), tip = (REAL)y1, top = (REAL)y0, rad = w / 2;
		// A teardrop: a circle, its sides running down to the tip.
		void *p = NULL;
		gp.CreatePath(0, &p);
		gp.AddPathArc(p, cx - rad, top, w, w, 120, 300);
		gp.AddPathLine(p, cx + rad * 0.5f, top + rad + rad * (REAL)(sqrt(3) / 2), cx, tip);
		gp.ClosePathFigure(p);
		gp.TranslateWorldTransform(g, 0, k, 0);
		fill_path(g, argb(60, 0, 0, 0), p);
		gp.TranslateWorldTransform(g, 0, -k, 0);
		UINT32 color = m->colored ? argb((uint8_t)m->rgba, (uint8_t)(m->rgba >> 24), (uint8_t)(m->rgba >> 16), (uint8_t)(m->rgba >> 8))
		                          : argb(255, 0xE8, 0x11, 0x23);  // Windows's red
		fill_path(g, color, p);
		void *pen = NULL;
		gp.CreatePen1(argb(255, 255, 255, 255), 1.5f * k, 2, &pen);  // UnitPixel
		gp.DrawPath(g, pen, p);
		gp.DeletePen(pen);
		gp.DeletePath(p);
		void *dot = NULL;
		gp.CreateSolidFill(argb(255, 255, 255, 255), &dot);
		gp.FillEllipse(g, dot, cx - rad * 0.38f, top + rad - rad * 0.38f, rad * 0.76f, rad * 0.76f);
		gp.DeleteBrush(dot);
		if (v->selected && !strcmp(m->id, v->selected) && (*m->title || *m->subtitle)) sel = m;
	}
	if (sel) paint_callout(v, dc, g, sel);
	gp.DeleteGraphics(g);
}

// paint_attribution draws "© OpenStreetMap contributors" at the bottom
// right, as OpenStreetMap asks of maps of its data.
static void paint_attribution(view *v, HDC dc) {
	RECT a = attribution(v);
	double k = scale(v);
	void *g = graphics(dc);
	if (g) {
		void *b = NULL;
		gp.CreateSolidFill(argb(190, 255, 255, 255), &b);
		gp.FillRectangle(g, b, (REAL)a.left, (REAL)a.top, (REAL)(a.right - a.left), (REAL)(a.bottom - a.top));
		gp.DeleteBrush(b);
		gp.DeleteGraphics(g);
	}
	HFONT f = font(k, 11, 400);
	HGDIOBJ old = SelectObject(dc, f);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(0x33, 0x33, 0x33));
	RECT r = a;
	r.right -= (LONG)lround(5 * k);
	DrawTextW(dc, ATTRIBUTION, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_RIGHT | DT_VCENTER);
	SelectObject(dc, old);
	DeleteObject(f);
}

// paint draws the map into dc: the tiles, the pins, a callout, the
// attribution; through a back buffer.
static void paint(view *v, HDC dc) {
	RECT r = client(v);
	int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return;
	if (!v->back || v->back_size.cx != w || v->back_size.cy != h) {
		if (v->back) DeleteObject(v->back);
		void *bits;
		v->back = dib(w, h, &bits);
		v->back_size = (SIZE){w, h};
		if (!v->back) return;
	}
	HDC mem = CreateCompatibleDC(dc);
	HGDIOBJ old = SelectObject(mem, v->back);
	HBRUSH land = CreateSolidBrush(RGB(0xE8, 0xE4, 0xDE));  // before its tiles come
	FillRect(mem, &r, land);
	DeleteObject(land);
	paint_tiles(v, mem, r);
	paint_markers(v, mem);
	paint_attribution(v, mem);
	BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old);
	DeleteDC(mem);
}

static void animate_zoom(view *v, double dz, POINT p) {
	v->zoom_from = v->zoom;
	v->zoom_to = fmax(fmin(v->zoom + dz, max_zoom), 0);
	v->zoom_at = p;
	v->zoom_start = now_ms();
	SetTimer(v->hwnd, timer_zoom, 15, NULL);
}

static void step_zoom(view *v) {
	double f = fmin((now_ms() - v->zoom_start) / 200.0, 1);
	f = 1 - (1 - f) * (1 - f);  // easing out
	zoom_by(v, v->zoom_from + (v->zoom_to - v->zoom_from) * f - v->zoom, v->zoom_at);
	if (f >= 1) {
		KillTimer(v->hwnd, timer_zoom);
		report(v);
	}
}

static void set_selected(view *v, const char *id) {
	free(v->selected);
	v->selected = id ? asuka_strdup(id) : NULL;
	invalidate(v);
}

// click is a click that didn't move the map: on the attribution, a
// marker, or the map.
static void click(view *v, POINT p) {
	if (attribution_hit(v, p)) {
		ShellExecuteW(NULL, L"open", L"https://www.openstreetmap.org/copyright", NULL, NULL, SW_SHOWNORMAL);
		return;
	}
	const marker *m = marker_at(v, p);
	if (m) {
		char *id = asuka_strdup(m->id);
		set_selected(v, id);
		maps_View_markerPress(v->events, id);
		free(id);
		return;
	}
	if (v->selected) set_selected(v, NULL);
	maps_Coordinate c = at(v, p);
	maps_View_press(v->events, &c);
}

static POINT point_of(LPARAM lp) { return (POINT){(short)LOWORD(lp), (short)HIWORD(lp)}; }

static bool message(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *result) {
	view *v = ctx;
	*result = 0;
	switch (m) {
	case WM_NCDESTROY:
		v->hwnd = NULL;
		unref(v);
		return false;
	case WM_ERASEBKGND: *result = 1; return true;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		paint(v, dc);
		EndPaint(hwnd, &ps);
		return true;
	}
	case WM_SIZE:
		if (v->has_pending) apply(v);
		else clamp(v);
		return false;
	case WM_GETDLGCODE: *result = DLGC_WANTARROWS | DLGC_WANTCHARS; return true;
	case WM_SETCURSOR:
		if (v->interactive && LOWORD(lp) == HTCLIENT) {
			POINT p;
			GetCursorPos(&p);
			ScreenToClient(hwnd, &p);
			SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(attribution_hit(v, p) || marker_at(v, p) ? 32649 : 32512)));  // IDC_HAND, IDC_ARROW
			*result = 1;
			return true;
		}
		return false;
	case WM_LBUTTONDOWN: {
		POINT p = point_of(lp);
		SetFocus(hwnd);
		if (!v->interactive) return true;
		SetCapture(hwnd);
		v->down = true;
		v->dragging = false;
		v->down_at = p;
		v->down_cx = v->cx;
		v->down_cy = v->cy;
		v->doubled = v->last_up && now_ms() - v->last_up < GetDoubleClickTime() && labs(p.x - v->last_up_at.x) <= 4 && labs(p.y - v->last_up_at.y) <= 4;
		if (v->doubled && !marker_at(v, p) && !attribution_hit(v, p)) animate_zoom(v, wp & MK_SHIFT ? -1 : 1, p);
		return true;
	}
	case WM_MOUSEMOVE: {
		if (!v->down) return false;
		POINT p = point_of(lp);
		if (!v->dragging && (labs(p.x - v->down_at.x) > 3 || labs(p.y - v->down_at.y) > 3)) v->dragging = true;
		if (v->dragging) {
			v->cx = v->down_cx;
			v->cy = v->down_cy;
			pan_by(v, p.x - v->down_at.x, p.y - v->down_at.y);
		}
		return true;
	}
	case WM_LBUTTONUP: {
		if (!v->down) return false;
		POINT p = point_of(lp);
		v->down = false;
		ReleaseCapture();
		if (v->dragging) {
			v->dragging = false;
			report(v);
			return true;
		}
		if (v->doubled) {
			v->doubled = false;
			v->last_up = 0;
			return true;
		}
		v->last_up = now_ms();
		v->last_up_at = p;
		click(v, p);
		return true;
	}
	case WM_CAPTURECHANGED:
		if (v->down && (HWND)lp != hwnd) {
			v->down = v->dragging = false;
			report(v);
		}
		return false;
	case WM_MOUSEWHEEL: {
		if (!v->interactive) return false;
		double delta = GET_WHEEL_DELTA_WPARAM(wp) / 120.0;
		POINT p = point_of(lp);
		ScreenToClient(hwnd, &p);
		// A notch is half a level; a touchpad's pinch (Ctrl) comes in
		// small steps, as fine as it is.
		zoom_by(v, delta / 2, p);
		SetTimer(hwnd, timer_wheel, 300, NULL);
		return true;
	}
	case WM_TIMER:
		if (wp == timer_wheel) {
			KillTimer(hwnd, timer_wheel);
			report(v);
		} else if (wp == timer_zoom) {
			step_zoom(v);
		}
		return true;
	case WM_KEYDOWN: {
		if (!v->interactive) return false;
		RECT r = client(v);
		LONG w = r.right - r.left, h = r.bottom - r.top;
		POINT mid = {w / 2, h / 2};
		double step = (w < h ? w : h) / 8.0;
		switch (wp) {
		case VK_OEM_PLUS:
		case VK_ADD: animate_zoom(v, 1, mid); break;
		case VK_OEM_MINUS:
		case VK_SUBTRACT: animate_zoom(v, -1, mid); break;
		case VK_LEFT: pan_by(v, step, 0); break;
		case VK_RIGHT: pan_by(v, -step, 0); break;
		case VK_UP: pan_by(v, 0, step); break;
		case VK_DOWN: pan_by(v, 0, -step); break;
		default: return false;
		}
		report(v);
		return true;
	}
	case WM_GESTURE: {
		GESTUREINFO gi = {sizeof gi};
		if (!GetGestureInfo((HGESTUREINFO)lp, &gi) || !v->interactive) return false;
		POINT p = {gi.ptsLocation.x, gi.ptsLocation.y};
		ScreenToClient(hwnd, &p);
		switch (gi.dwID) {
		case GID_ZOOM:
			if (!(gi.dwFlags & GF_BEGIN) && v->gesture_arg > 0 && gi.ullArguments > 0) zoom_by(v, log2((double)gi.ullArguments / v->gesture_arg), p);
			v->gesture_arg = gi.ullArguments;
			break;
		case GID_PAN:
			if (!(gi.dwFlags & GF_BEGIN)) pan_by(v, p.x - v->gesture.x, p.y - v->gesture.y);
			v->gesture = p;
			break;
		case GID_END:
			v->gesture_arg = 0;
			report(v);
			break;
		default: return false;
		}
		CloseGestureInfoHandle((HGESTUREINFO)lp);
		return true;
	}
	}
	return false;
}

// ---- The package's View: its props ----

static void set_region(maps_View *self, const maps_Region *r) {
	view *v = (view *)self;
	if (r->latDelta <= 0 && r->lngDelta <= 0 && r->center.lat == 0 && r->center.lng == 0) return;
	v->pending = *r;
	v->has_pending = true;
	apply(v);
}

static void set_markers(maps_View *self, maps_list_Marker ms) {
	view *v = (view *)self;
	for (size_t i = 0; i < v->nmarkers; i++) {
		free(v->markers[i].id);
		free(v->markers[i].title);
		free(v->markers[i].subtitle);
	}
	free(v->markers);
	v->markers = calloc(ms.len ? ms.len : 1, sizeof *v->markers);
	v->nmarkers = ms.len;
	bool found = false;
	for (size_t i = 0; i < ms.len; i++) {
		const maps_Marker *m = &ms.data[i];
		marker *mk = &v->markers[i];
		mk->id = asuka_strdup(m->id);
		mk->title = asuka_strdup(m->title);
		mk->subtitle = asuka_strdup(m->subtitle);
		mk->lat = m->at.lat;
		mk->lng = m->at.lng;
		if ((mk->colored = m->color != NULL)) mk->rgba = asuka_desktop_color((uint64_t)*m->color);
		found = found || (v->selected && !strcmp(mk->id, v->selected));
	}
	if (!found && v->selected) {
		free(v->selected);
		v->selected = NULL;
	}
	invalidate(v);
}

static void set_map_type(maps_View *self, maps_MapType t) {}  // OpenStreetMap's streets only
static void set_shows_user(maps_View *self, bool on) {}
static void set_interactive(maps_View *self, bool on) { ((view *)self)->interactive = on; }

// measure: none of its own; its style gives it a size.
static bool measure(maps_View *self, float w, float h, float *ow, float *oh) {
	*ow = isnan(w) ? 0 : w;
	*oh = isnan(h) ? 0 : h;
	return true;
}

static void destroy(maps_View *self) {
	view *v = (view *)self;
	v->destroyed = true;
	for (size_t i = 0; i < T.nviews; i++)
		if (T.views[i] == v) {
			T.views[i] = T.views[--T.nviews];
			break;
		}
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
	tiles();
	view *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	v->events = events;
	v->refs = 1;
	v->interactive = true;
	v->cx = v->cy = 0.5;
	v->zoom = 1;
	T.views = realloc(T.views, (T.nviews + 1) * sizeof *T.views);
	T.views[T.nviews++] = v;
	v->hwnd = asuka_win_window(0, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, (HWND)events.parent, message, v);
	if (v->hwnd) {
		v->refs++;  // the window's, until WM_NCDESTROY
		// Pinches and one finger's pans, with inertia; no rotation.
		GESTURECONFIG c[3] = {{GID_ZOOM, GC_ZOOM, 0}, {GID_ROTATE, 0, GC_ROTATE},
		                      {GID_PAN, GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY | GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY | GC_PAN_WITH_INERTIA,
		                       GC_PAN_WITH_GUTTER}};
		SetGestureConfig(v->hwnd, 0, 3, c, sizeof *c);
	}
	v->base.widget = v->hwnd;
	return &v->base;
}
