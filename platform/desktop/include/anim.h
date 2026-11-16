// anim.h: animated images where the platform decodes stills only
// (Windows' WIC, Linux's GDK): GIF, APNG and animated WebP, internal/
// animimage's port. Their frames are read here, a GIF's decoded here, an
// APNG's and a WebP's pictures made still PNG and WebP files for the
// host's decoder (dsk_still). A player composites frame after frame onto
// a canvas as the formats say (disposal, blending), so only the canvas
// and the frames' own rectangles are kept. Any thread; an animation and
// its player belong to one at a time.
#ifndef ASUKA_DESKTOP_ANIM_H
#define ASUKA_DESKTOP_ANIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DSK_DISPOSE_NONE, DSK_DISPOSE_BACKGROUND, DSK_DISPOSE_PREVIOUS };

// A frame: its RGBA pixels (not premultiplied), at x, y on the canvas,
// shown for delay_ms.
typedef struct {
	uint8_t *rgba;
	int x, y, w, h;
	int delay_ms;
	int disposal;  // DSK_DISPOSE_*
	bool blend;    // drawn over what's there (alpha); else replaces it
} dsk_frame;

typedef struct {
	int width, height;
	dsk_frame *frames;
	size_t count;
	int loops;  // how many times it plays (0: forever)
} dsk_anim;

// dsk_still decodes a still image file (a frame's PNG or WebP) into RGBA
// pixels, not premultiplied, malloc'd; NULL if it can't.
typedef uint8_t *(*dsk_still)(void *ctx, const uint8_t *data, size_t n, int *w, int *h);

// dsk_anim_is_animated reports whether data looks like an animated GIF,
// APNG or WebP, from its headers.
bool dsk_anim_is_animated(const uint8_t *data, size_t n);
// dsk_anim_decode reads an animated GIF, APNG or WebP: NULL for a still
// one, another format or a broken one. still decodes APNG's and WebP's
// frames.
dsk_anim *dsk_anim_decode(const uint8_t *data, size_t n, dsk_still still, void *ctx);
void dsk_anim_free(dsk_anim *a);

// A player composites an animation's frames in order onto its canvas
// (width * height RGBA, not premultiplied).
typedef struct {
	const dsk_anim *a;
	uint8_t *canvas;
	uint8_t *saved;  // DSK_DISPOSE_PREVIOUS's: the rectangle before the frame
	size_t next;     // the frame dsk_player_next draws
	int loop, last;
} dsk_player;

void dsk_player_init(dsk_player *p, const dsk_anim *a);
void dsk_player_free(dsk_player *p);
// dsk_player_next draws the next frame and returns how long it shows (ms);
// -1 once the animation has played its loops (the canvas keeps its last
// frame).
int dsk_player_next(dsk_player *p);

#endif
