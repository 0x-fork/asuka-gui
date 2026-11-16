// imageedit.h: what imageedit.c (the actions, the files, both platforms)
// and each platform's codecs (imageedit_linux.c: GdkPixbuf and
// GStreamer; imageedit_windows.c: WIC and Media Foundation) share.
#ifndef IMAGEEDIT_H
#define IMAGEEDIT_H

#include "imageedit.asuka.h"

#include <stdbool.h>
#include <stdint.h>

// An image: rows of premultiplied RGBA, w*4 bytes each, of malloc's.
typedef struct {
	int w, h;
	uint8_t *px;
} ie_image;

// The platform's, called on a worker. Each fails err (the package's
// noImage when there's no image to read) and returns false.

// ie_decode decodes the image at a file (path), upright as its EXIF
// orientation says.
bool ie_decode(const char *path, ie_image *img, asuka_error *err);
// ie_size is the image's size upright, read from its header where it can.
bool ie_size(const char *path, int *w, int *h, asuka_error *err);
// ie_video_frame is the frame of a video (a file's path, or an http(s)
// URL as it is) at seconds, upright; opaque.
bool ie_video_frame(const char *source, bool local, double seconds, ie_image *img, asuka_error *err);
// ie_encode encodes img as PNG, or as JPEG (img is opaque then) of
// quality 0 to 1.
bool ie_encode(const ie_image *img, bool png, double quality, asuka_bytes *out, asuka_error *err);

// ie_no_image fails err with the package's noImage.
void ie_no_image(asuka_error *err);

#endif
