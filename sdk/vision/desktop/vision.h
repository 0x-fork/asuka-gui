// vision.h: what vision's Windows and Linux C share (vision.c): the
// image decoded upright, its codes, and the boxes and word places both
// platforms' text gives; each platform reads text and finds faces.
#ifndef VISION_H
#define VISION_H

#include "vision.asuka.h"

// vision_image is an image, upright: its pixels (BGRA premultiplied on
// Windows, RGBA on Linux) and its gray levels, w×h.
typedef struct {
	uint8_t *px, *gray;
	int w, h;
} vision_image;

// vision_load decodes the image at path (a file on this computer); the
// platform's.
bool vision_load(const char *path, vision_image *img);
void vision_image_free(vision_image *img);

// vision_text reads the image's text in the languages given (BCP 47,
// most likely first; none: the user's), into lines; the platform's.
// false with err set when it can't.
bool vision_text(const vision_image *img, vision_list_string languages, vision_list_Line *out, asuka_error *err);
// vision_faces finds the faces in the image; the platform's.
bool vision_faces(const vision_image *img, vision_list_Box *out, asuka_error *err);

// vision_gray is an image's gray levels from its pixels (4 bytes each;
// bgr: B, G, R, A, else R, G, B, A; premultiplied or not), transparent
// ones over white: w×h bytes of malloc's.
uint8_t *vision_gray(const uint8_t *px, int w, int h, bool bgr, bool premultiplied);

// vision_box is a rectangle of pixels as fractions of a w×h image.
vision_Box vision_box(double x0, double y0, double x1, double y1, int w, int h);
// vision_union is the smallest box holding both (an empty one adds
// nothing).
vision_Box vision_union(vision_Box a, vision_Box b);
// vision_place finds word in line from byte *at on: its start and end in
// the line's characters (false: not there), *at past it.
bool vision_place(const char *line, const char *word, size_t *at, int64_t *start, int64_t *end);

// Lists grown a item at a time.
void vision_lines_push(vision_list_Line *l, vision_Line v);
void vision_words_push(vision_list_Word *l, vision_Word v);

#endif
