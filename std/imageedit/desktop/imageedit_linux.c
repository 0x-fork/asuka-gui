// asuka/imageedit on Linux: GdkPixbuf decodes (what its loaders read:
// JPEG, PNG, WebP, HEIF where installed), upright as its EXIF orientation
// says, and writes JPEG and PNG; GStreamer takes a video's frame.
#include "asuka_linux.h"
#include "imageedit.h"

#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct _GdkPixbuf GdkPixbuf;

static struct {
	const char *missing;
	GdkPixbuf *(*new_from_file)(const char *, GError **);
	GdkPixbuf *(*new_from_data)(const guchar *, int, gboolean, int, int, int, int, void (*)(guchar *, gpointer), gpointer);
	GdkPixbuf *(*apply_orientation)(GdkPixbuf *);
	int (*width)(const GdkPixbuf *), (*height)(const GdkPixbuf *), (*rowstride)(const GdkPixbuf *), (*channels)(const GdkPixbuf *);
	const guint8 *(*pixels)(const GdkPixbuf *);
	gpointer (*file_info)(const char *, gint *, gint *);
	gboolean (*save)(GdkPixbuf *, gchar **, gsize *, const char *, char **, char **, GError **);
	// GStreamer's, beyond asuka_linux.h's.
	int (*get_state)(GstElement *, int *, int *, guint64);
	gboolean (*seek_simple)(GstElement *, int, int, gint64);
	gpointer (*try_pull_preroll)(GstElement *, guint64);
} px;

static void load(void) {
	if (!asuka_linux()) {
		px.missing = "GLib isn't installed";
		return;
	}
	void *l = dlopen("libgdk_pixbuf-2.0.so.0", RTLD_NOW | RTLD_GLOBAL);
	if (!l) {
		px.missing = "GdkPixbuf isn't installed (Ubuntu, Debian: libgdk-pixbuf-2.0-0; Fedora: gdk-pixbuf2)";
		return;
	}
	*(void **)&px.new_from_file = dlsym(l, "gdk_pixbuf_new_from_file");
	*(void **)&px.new_from_data = dlsym(l, "gdk_pixbuf_new_from_data");
	*(void **)&px.apply_orientation = dlsym(l, "gdk_pixbuf_apply_embedded_orientation");
	*(void **)&px.width = dlsym(l, "gdk_pixbuf_get_width");
	*(void **)&px.height = dlsym(l, "gdk_pixbuf_get_height");
	*(void **)&px.rowstride = dlsym(l, "gdk_pixbuf_get_rowstride");
	*(void **)&px.channels = dlsym(l, "gdk_pixbuf_get_n_channels");
	*(void **)&px.pixels = dlsym(l, "gdk_pixbuf_read_pixels");
	*(void **)&px.file_info = dlsym(l, "gdk_pixbuf_get_file_info");
	*(void **)&px.save = dlsym(l, "gdk_pixbuf_save_to_bufferv");
	if (!px.new_from_file || !px.new_from_data || !px.apply_orientation || !px.pixels || !px.save)
		px.missing = "GdkPixbuf is too old (2.32 or later)";
}

static bool loaded(asuka_error *err) {
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, load);
	if (px.missing) asuka_error_unsupported(err, "%s", px.missing);
	return !px.missing;
}

// premultiplied is GdkPixbuf's RGB or RGBA, premultiplied.
static ie_image premultiplied(GdkPixbuf *pb) {
	int w = px.width(pb), h = px.height(pb), stride = px.rowstride(pb), ch = px.channels(pb);
	const guint8 *data = px.pixels(pb);
	ie_image img = {w, h, malloc((size_t)w * h * 4)};
	for (int y = 0; y < h; y++) {
		const guint8 *s = data + (size_t)y * stride;
		uint8_t *d = img.px + (size_t)y * w * 4;
		for (int x = 0; x < w; x++, s += ch, d += 4) {
			unsigned a = ch == 4 ? s[3] : 255;
			d[0] = (uint8_t)(s[0] * a / 255), d[1] = (uint8_t)(s[1] * a / 255), d[2] = (uint8_t)(s[2] * a / 255), d[3] = (uint8_t)a;
		}
	}
	return img;
}

static bool is_file(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

bool ie_decode(const char *path, ie_image *img, asuka_error *err) {
	if (!loaded(err)) return false;
	GError *e = NULL;
	GdkPixbuf *pb = is_file(path) ? px.new_from_file(path, &e) : NULL;
	if (e) G(g_error_free)(e);
	if (!pb) {
		ie_no_image(err);
		return false;
	}
	GdkPixbuf *up = px.apply_orientation(pb);
	G(g_object_unref)(pb);
	if (!up) {
		ie_no_image(err);
		return false;
	}
	*img = premultiplied(up);
	G(g_object_unref)(up);
	return true;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned u16(const uint8_t *p, bool le) { return le ? (unsigned)p[1] << 8 | p[0] : be16(p); }
static uint32_t u32(const uint8_t *p, bool le) {
	return le ? (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]
	          : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// jpeg_orientation is a JPEG's EXIF orientation (1 to 8; 0: none).
static int jpeg_orientation(const uint8_t *b, size_t n) {
	if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) return 0;
	for (size_t i = 2; i + 4 <= n;) {
		if (b[i] != 0xFF) return 0;
		unsigned marker = b[i + 1], len = be16(b + i + 2);
		if (marker == 0xDA || len < 2) return 0;  // the image data: no EXIF before it
		const uint8_t *seg = b + i + 4;
		size_t sn = i + 2 + len < n ? len - 2 : n - i - 4;
		if (marker == 0xE1 && sn > 14 && !memcmp(seg, "Exif\0\0", 6)) {
			const uint8_t *t = seg + 6;
			size_t tn = sn - 6;
			bool le = t[0] == 'I' && t[1] == 'I';
			size_t ifd = u32(t + 4, le);
			if (ifd + 2 > tn) return 0;
			unsigned count = u16(t + ifd, le);
			for (unsigned k = 0; k < count; k++) {
				size_t e = ifd + 2 + 12 * k;
				if (e + 12 > tn) return 0;
				if (u16(t + e, le) == 0x0112) return (int)u16(t + e + 8, le);
			}
			return 0;
		}
		i += 2 + len;
	}
	return 0;
}

bool ie_size(const char *path, int *w, int *h, asuka_error *err) {
	if (!loaded(err)) return false;
	if (!is_file(path)) {
		ie_no_image(err);
		return false;
	}
	gint iw = 0, ih = 0;
	if (px.file_info && px.file_info(path, &iw, &ih) && iw > 0 && ih > 0) {
		FILE *f = fopen(path, "rb");
		int o = 0;
		if (f) {
			uint8_t *head = malloc(128 << 10);
			size_t n = fread(head, 1, 128 << 10, f);
			fclose(f);
			o = jpeg_orientation(head, n);
			free(head);
		}
		bool quarter = o >= 5 && o <= 8;
		*w = quarter ? ih : iw, *h = quarter ? iw : ih;
		return true;
	}
	ie_image img;
	if (!ie_decode(path, &img, err)) return false;
	*w = img.w, *h = img.h;
	free(img.px);
	return true;
}

bool ie_video_frame(const char *source, bool local, double seconds, ie_image *img, asuka_error *err) {
	if (!asuka_linux_gst()) {
		asuka_error_unsupported(err, "video frames need GStreamer (Ubuntu, Debian: libgstreamer1.0-0, gstreamer1.0-plugins-base)");
		return false;
	}
	if (!px.get_state) {
		*(void **)&px.get_state = dlsym(RTLD_DEFAULT, "gst_element_get_state");
		*(void **)&px.seek_simple = dlsym(RTLD_DEFAULT, "gst_element_seek_simple");
		*(void **)&px.try_pull_preroll = dlsym(RTLD_DEFAULT, "gst_app_sink_try_pull_preroll");
	}
	if (!px.get_state || !px.seek_simple || !px.try_pull_preroll || !G(gst_parse_launch)) {
		asuka_error_unsupported(err, "video frames need GStreamer's app library (Ubuntu, Debian: libgstreamer-plugins-base1.0-0)");
		return false;
	}
	char *uri;
	if (local) {
		GFile *f = G(g_file_new_for_path)(source);
		gchar *u = G(g_file_get_uri)(f);
		uri = asuka_strdup(u);
		G(g_free)(u);
		G(g_object_unref)(f);
	} else {
		uri = asuka_strdup(source);
	}
	GError *e = NULL;
	GstElement *pipe = G(gst_parse_launch)(
	    "uridecodebin name=source ! videoconvert ! videoflip method=automatic ! videoconvert ! video/x-raw,format=RGBA ! appsink name=frames sync=false",
	    &e);
	if (!pipe) {
		free(uri);
		asuka_linux_fail(err, e, "GStreamer");
		return false;
	}
	if (e) G(g_error_free)(e);
	G(g_object_ref_sink)(pipe);
	GstElement *src = G(gst_bin_get_by_name)(pipe, "source");
	if (src) {
		G(g_object_set)(src, "uri", uri, NULL);  // as it is: no quoting for gst-launch's syntax
		G(gst_object_unref)(src);
	}
	free(uri);
	GstElement *sink = G(gst_bin_get_by_name)(pipe, "frames");
	bool ok = false;
	const guint64 wait = 15000000000ull;  // ns
	int state, pending;
	enum { failure = 0 };  // GST_STATE_CHANGE_FAILURE
	if (sink) {
		G(gst_element_set_state)(pipe, GST_STATE_PAUSED);
		ok = px.get_state(pipe, &state, &pending, wait) != failure;
		if (ok && seconds > 0)
			ok = px.seek_simple(pipe, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, (gint64)(seconds * 1e9)) &&
			     px.get_state(pipe, &state, &pending, wait) != failure;
		gpointer sample = ok ? px.try_pull_preroll(sink, wait) : NULL;
		ok = false;
		if (sample) {
			gpointer buf = G(gst_sample_get_buffer)(sample);
			GstCaps *caps = G(gst_sample_get_caps)(sample);
			gpointer st = caps ? G(gst_caps_get_structure)(caps, 0) : NULL;
			gint w = 0, h = 0;
			GstMapInfo info;
			if (buf && st && G(gst_structure_get_int)(st, "width", &w) && G(gst_structure_get_int)(st, "height", &h) && w > 0 && h > 0 &&
			    G(gst_buffer_map)(buf, &info, GST_MAP_READ)) {
				// RGBA rows are 4-byte aligned: GStreamer's stride is the width's.
				size_t n = (size_t)w * h * 4;
				if (info.size >= n) {
					*img = (ie_image){w, h, malloc(n)};
					memcpy(img->px, info.data, n);
					for (size_t i = 3; i < n; i += 4) img->px[i] = 255;  // video is opaque
					ok = true;
				}
				G(gst_buffer_unmap)(buf, &info);
			}
			G(gst_mini_object_unref)(sample);
		}
		G(gst_object_unref)(sink);
	}
	G(gst_element_set_state)(pipe, GST_STATE_NULL);
	G(gst_object_unref)(pipe);
	if (!ok) ie_no_image(err);
	return ok;
}

bool ie_encode(const ie_image *img, bool png, double quality, asuka_bytes *out, asuka_error *err) {
	if (!loaded(err)) return false;
	int ch = png ? 4 : 3, stride = img->w * ch;
	guchar *data = malloc((size_t)stride * img->h);
	size_t n = (size_t)img->w * img->h;
	for (size_t i = 0; i < n; i++) {
		const uint8_t *s = img->px + i * 4;
		guchar *d = data + i * ch;
		unsigned a = s[3];
		if (!png) {
			d[0] = s[0], d[1] = s[1], d[2] = s[2];
		} else if (a == 0) {
			d[0] = d[1] = d[2] = d[3] = 0;
		} else {  // straight alpha
			for (int c = 0; c < 3; c++) {
				unsigned v = (s[c] * 255 + a / 2) / a;
				d[c] = (guchar)(v > 255 ? 255 : v);
			}
			d[3] = (guchar)a;
		}
	}
	GdkPixbuf *pb = px.new_from_data(data, 0, png, 8, img->w, img->h, stride, NULL, NULL);  // GDK_COLORSPACE_RGB
	gchar *buf = NULL;
	gsize len = 0;
	GError *e = NULL;
	char q[8];
	int qi = (int)lround(quality * 100);
	snprintf(q, sizeof q, "%d", qi < 1 ? 1 : qi > 100 ? 100 : qi);
	char *keys[] = {png ? NULL : "quality", NULL}, *values[] = {png ? NULL : q, NULL};
	gboolean ok = pb && px.save(pb, &buf, &len, png ? "png" : "jpeg", keys, values, &e);
	if (pb) G(g_object_unref)(pb);
	free(data);
	if (!ok) {
		asuka_linux_fail(err, e, png ? "writing a PNG" : "writing a JPEG");
		return false;
	}
	*out = asuka_bytes_copy(buf, len);
	G(g_free)(buf);
	return true;
}
