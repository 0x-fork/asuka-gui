// asuka/device on Windows and Linux (macOS runs the package's Swift): the
// system's open dialog for the pickers (their files copied to the app's
// caches, as a phone's pickers give them), the system's browser, the
// clipboard. What both share.
#ifdef _WIN32
#include "asuka_windows.h"
#define SEP "\\"
#else
#include <errno.h>
#include <sys/stat.h>
#define SEP "/"
#endif
#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool device_web_url(const char *u) { return strncmp(u, "https://", 8) == 0 || strncmp(u, "http://", 7) == 0; }

const char *device_mime(const char *name) {
	static const char *table[][2] = {
	    {".png", "image/png"},   {".jpg", "image/jpeg"},  {".jpeg", "image/jpeg"}, {".gif", "image/gif"},       {".webp", "image/webp"},
	    {".heic", "image/heic"}, {".heif", "image/heif"}, {".bmp", "image/bmp"},   {".tif", "image/tiff"},      {".tiff", "image/tiff"},
	    {".svg", "image/svg+xml"}, {".ico", "image/vnd.microsoft.icon"}, {".pdf", "application/pdf"}, {".txt", "text/plain; charset=utf-8"},
	    {".csv", "text/csv; charset=utf-8"}, {".md", "text/markdown; charset=utf-8"}, {".html", "text/html; charset=utf-8"},
	    {".htm", "text/html; charset=utf-8"}, {".json", "application/json"}, {".xml", "text/xml; charset=utf-8"}, {".zip", "application/zip"},
	    {".mp3", "audio/mpeg"},  {".m4a", "audio/mp4"},   {".aac", "audio/aac"},   {".wav", "audio/wav"},       {".flac", "audio/flac"},
	    {".ogg", "audio/ogg"},   {".opus", "audio/ogg"},  {".mp4", "video/mp4"},   {".m4v", "video/mp4"},       {".mov", "video/quicktime"},
	    {".webm", "video/webm"}, {".avi", "video/x-msvideo"}, {".mkv", "video/x-matroska"},
	};
	const char *dot = strrchr(name, '.');
	if (!dot) return "";
	for (size_t i = 0; i < sizeof table / sizeof *table; i++) {
		const char *a = dot, *b = table[i][0];
		while (*a && *b && (*a | 32) == (*b | 32)) a++, b++;
		if (!*a && !*b) return table[i][1];
	}
	return "";
}

static void random_name(char out[25]) {
	static unsigned seq;
	unsigned long long r = 0;
	for (int i = 0; i < 4; i++) r = r << 16 ^ (unsigned long long)rand();
	snprintf(out, 25, "%016llx%08x", r, ++seq);
}

static bool make_dir(const char *p) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(p);
	BOOL ok = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
	free(w);
	return ok;
#else
	return mkdir(p, 0755) == 0 || errno == EEXIST;
#endif
}

static FILE *open_file(const char *p, const char *mode) {
#ifdef _WIN32
	wchar_t *w = asuka_wide(p), *m = asuka_wide(mode);
	FILE *f = _wfopen(w, m);
	free(w);
	free(m);
	return f;
#else
	return fopen(p, mode);
#endif
}

static const char *base_name(const char *p) {
	const char *b = p;
	for (const char *q = p; *q; q++)
		if (*q == '/' || *q == '\\') b = q + 1;
	return b;
}

bool device_copy_picked(char **paths, size_t n, size_t limit, device_list_File *out, asuka_error *err) {
	if (limit > 0 && n > limit) n = limit;
	*out = (device_list_File){calloc(n ? n : 1, sizeof *out->data), 0};
	char *root = asuka_sprintf("%s" SEP "asuka-files", asuka_desktop_cache_dir());
	make_dir(root);
	for (size_t i = 0; i < n; i++) {
		char name[25];
		random_name(name);
		char *dir = asuka_sprintf("%s" SEP "%s", root, name);
		const char *base = base_name(paths[i]);
		char *to = asuka_sprintf("%s" SEP "%s", dir, base);
		bool ok = make_dir(dir);
		FILE *in = ok ? open_file(paths[i], "rb") : NULL;
		FILE *o = in ? open_file(to, "wb") : NULL;
		int64_t size = 0;
		ok = in && o;
		char buf[65536];
		size_t got;
		while (ok && (got = fread(buf, 1, sizeof buf, in)) > 0) {
			ok = fwrite(buf, 1, got, o) == got;
			size += (int64_t)got;
		}
		if (in) fclose(in);
		if (o && fclose(o) != 0) ok = false;
		free(dir);
		if (!ok) {
			asuka_error_failed(err, "device", 0, "couldn't copy %s", paths[i]);
			free(to);
			free(root);
			device_list_File_free(out);
			return false;
		}
		out->data[out->len++] = (device_File){to, asuka_strdup(base), asuka_strdup(device_mime(base)), size};
	}
	free(root);
	return true;
}

char *device_write_cache(const char *dir, const char *ext, const void *data, size_t len, asuka_error *err) {
	char *d = asuka_sprintf("%s" SEP "%s", asuka_desktop_cache_dir(), dir);
	make_dir(d);
	char name[25];
	random_name(name);
	char *p = asuka_sprintf("%s" SEP "%s.%s", d, name, ext);
	free(d);
	FILE *f = open_file(p, "wb");
	bool ok = f && fwrite(data, 1, len, f) == len;
	if (f && fclose(f) != 0) ok = false;
	if (!ok) {
		asuka_error_failed(err, "device", 0, "couldn't write %s", p);
		free(p);
		return NULL;
	}
	return p;
}

device_list_string device_photos(device_list_File *files) {
	device_list_string out = {calloc(files->len ? files->len : 1, sizeof(char *)), files->len};
	for (size_t i = 0; i < files->len; i++) {
		out.data[i] = files->data[i].path;
		files->data[i].path = NULL;
	}
	device_list_File_free(files);
	return out;
}
