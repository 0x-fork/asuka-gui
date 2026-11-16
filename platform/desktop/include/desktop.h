// desktop.h: the C the Windows and Linux hosts share (RFC 0008, "Shared
// C for Windows and Linux"), kept small: what the Go runtime's packages
// did for both and isn't a platform's.
//
//   - JSON, read into a tree and written (the app's settings, the state
//     files, a module's arguments);
//   - the app's settings (internal/boot's asuka-app.json);
//   - the app's directories (internal/appdir's rules);
//   - the state files: where the windows were (internal/winstate's
//     asuka-window.json) and where their routers were (internal/
//     routestate's asuka-routes.json);
//   - files by UTF-8 path (Windows' are UTF-16);
//   - where a flyout window goes against its anchor (internal/flyout);
//   - animated images, where the platform decodes stills only (anim.h:
//     internal/animimage's GIF, APNG and WebP).
//
// Nothing here is thread-safe unless it says so: the hosts call it on
// their main thread. It's portable C17 (POSIX on Linux, Win32 on
// Windows), with nothing of either host's: platform/desktop/desktop_test.go
// tests it on any system.
//
// The API is stable: what's added is added in commits of platform/desktop
// alone, each noted here:
//   - dsk_flyout_place, dsk_flyout_center: flyouts' and panels' places.
#ifndef ASUKA_DESKTOP_SHARED_H
#define ASUKA_DESKTOP_SHARED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- JSON ----

enum { DSK_JNULL, DSK_JFALSE, DSK_JTRUE, DSK_JNUM, DSK_JSTR, DSK_JARR, DSK_JOBJ };

// A value of a parsed document. Strings are NUL-terminated UTF-8 (len
// without the NUL); an array's or object's items are kids (an object's
// with their keys).
typedef struct dsk_json {
	int type;
	double num;
	char *str;
	size_t len;
	struct dsk_json *kids;
	char **keys;
	size_t n;
} dsk_json;

// dsk_json_parse parses text: NULL if it isn't JSON. dsk_json_free frees
// what it returned.
dsk_json *dsk_json_parse(const char *text, size_t len);
void dsk_json_free(dsk_json *v);
// dsk_json_get is an object's member, NULL if it has none (or v isn't an
// object).
const dsk_json *dsk_json_get(const dsk_json *v, const char *key);
// The member's value as a type, or def.
const char *dsk_json_str(const dsk_json *v, const char *key, const char *def);
double dsk_json_num(const dsk_json *v, const char *key, double def);
bool dsk_json_bool(const dsk_json *v, const char *key, bool def);

// A JSON writer: a growing buffer. dsk_jw_take hands its text over (the
// caller frees it); a writer that failed to allocate has err set.
typedef struct {
	char *p;
	size_t len, cap;
	bool err;
	bool comma;  // a value written in the open object or array
} dsk_jw;

void dsk_jw_raw(dsk_jw *w, const char *s, size_t n);
void dsk_jw_obj(dsk_jw *w);   // {
void dsk_jw_end(dsk_jw *w);   // }
void dsk_jw_arr(dsk_jw *w);   // [
void dsk_jw_aend(dsk_jw *w);  // ]
void dsk_jw_key(dsk_jw *w, const char *key);
void dsk_jw_str(dsk_jw *w, const char *s);
void dsk_jw_strn(dsk_jw *w, const char *s, size_t n);
void dsk_jw_num(dsk_jw *w, double v);
void dsk_jw_int(dsk_jw *w, int64_t v);
void dsk_jw_bool(dsk_jw *w, bool v);
// dsk_jw_value writes a parsed value back.
void dsk_jw_value(dsk_jw *w, const dsk_json *v);
char *dsk_jw_take(dsk_jw *w, size_t *len);
void dsk_jw_free(dsk_jw *w);

// ---- Files ----

// dsk_read_file reads a whole file (NUL-terminated past *len): NULL if it
// can't. Free it with free.
char *dsk_read_file(const char *path, size_t *len);
// dsk_write_file writes a file whole, through a temporary file renamed
// into place, making its directory first.
bool dsk_write_file(const char *path, const void *data, size_t len);
// dsk_exists reports whether a file or directory is at path.
bool dsk_exists(const char *path);
// dsk_mkdirs makes a directory and its parents.
bool dsk_mkdirs(const char *path);
// dsk_join is a, the platform's separator and b (free it).
char *dsk_join(const char *a, const char *b);
// dsk_getenv is an environment variable, UTF-8, or NULL when it's unset
// or empty (free it).
char *dsk_getenv(const char *name);
// dsk_exe_path is this executable's path (free it).
char *dsk_exe_path(void);
// dsk_dir is path's directory (free it); dsk_base its last element.
char *dsk_dir(const char *path);
const char *dsk_base(const char *path);

// ---- The app's settings (asuka-app.json) ----

typedef struct {
	char *name, *id, *version, *build, *accent, *scheme, *file_types;
	char *lang, *direction;
	bool background, keep_running, multiple_instances;
	char *update_feed, *update_key, *update_target;
} dsk_config;

// dsk_config_load reads the settings: from ASUKA_APP_CONFIG, else
// asuka-app.json beside the executable, else in an install prefix's
// share/<exe>/ (Linux: bin/<exe>). Missing, it's all empty.
void dsk_config_load(dsk_config *c);
void dsk_config_free(dsk_config *c);

// ---- The app's directories ----

// The platforms the rules differ by.
enum { DSK_WINDOWS, DSK_LINUX };

// dsk_dirs is where the app keeps its files, by internal/appdir's rules:
// ASUKA_DATA (else data, which the host names: Local AppData\<Name>,
// $XDG_DATA_HOME/<id>), ASUKA_CONFIG, ASUKA_CACHE, ASUKA_LOGS overriding
// each. The strings are the caller's to free.
typedef struct {
	char *data, *config, *cache, *logs;
} dsk_dirs;

void dsk_dirs_find(dsk_dirs *d, int platform, const char *data);
void dsk_dirs_free(dsk_dirs *d);

// ---- Where the windows were (asuka-window.json) ----

// A store of states by key, each a JSON object the host makes (Windows:
// its rectangle in pixels, DPI and maximized; Linux: its size). A file an
// older host wrote, the main window's fields alone, reads as "main"'s.
typedef struct dsk_winstate dsk_winstate;

// dsk_winstate_open reads the file at path (NULL: a store that keeps
// nothing, for tests and automation runs).
dsk_winstate *dsk_winstate_open(const char *path);
void dsk_winstate_close(dsk_winstate *s);
// dsk_winstate_load is key's state, NULL if none (the store's).
const dsk_json *dsk_winstate_load(dsk_winstate *s, const char *key);
// dsk_winstate_save keeps key's state (JSON text of an object), writing
// the file if it changed.
void dsk_winstate_save(dsk_winstate *s, const char *key, const char *json);
void dsk_winstate_forget(dsk_winstate *s, const char *key);

// ---- Where the routers were (asuka-routes.json) ----

typedef struct dsk_routes dsk_routes;

// dsk_routes_path is the file's path ("" keeps none: tests, automation,
// asuka dev, no data directory; free it).
char *dsk_routes_path(void);
dsk_routes *dsk_routes_open(const char *path);
void dsk_routes_close(dsk_routes *s);
// dsk_routes_location reports where key's router was: its href and state
// (the store's), false if nowhere.
bool dsk_routes_location(dsk_routes *s, const char *key, const char **href, const char **state, size_t *state_len);
// dsk_routes_set keeps key's address; it reports whether that changed
// (the host then writes it a moment later with dsk_routes_flush, off the
// main thread if it likes: flush takes the store's lock).
bool dsk_routes_set(dsk_routes *s, const char *key, const char *href, size_t href_len, const char *state, size_t state_len);
void dsk_routes_flush(dsk_routes *s);

// ---- Flyouts and panels (internal/flyout) ----

// A rectangle, y down, in a host's own units (points or pixels).
typedef struct {
	double x, y, w, h;
} dsk_rect;

// dsk_flyout_center is a w by h rectangle centered across r, its top a
// third of the way down the room left (as Spotlight's panel shows), within
// r.
dsk_rect dsk_flyout_center(double w, double h, dsk_rect r);
// dsk_flyout_place is the frame of a w by h flyout next to anchor, gap
// away, as placement (an AK_PLACEMENT_*: the side, then start, center or
// end) says: flipped to the other side where that has no room and the
// other has, lined up along the side (flipped to the other end where that
// has no room), then slid back into work (the work area of the anchor's
// display) and shrunk to it, as GTK places menus.
dsk_rect dsk_flyout_place(dsk_rect anchor, double w, double h, int placement, double gap, dsk_rect work);

#ifdef __cplusplus
}
#endif

#endif
