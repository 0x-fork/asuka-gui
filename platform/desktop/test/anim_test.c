// anim_test.c: decodes the animated GIF at argv[1] and plays it: each
// frame's delay and its canvas, as hex, a line each, for desktop_test.go
// to compare with internal/animimage's.
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "anim.h"
#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
	if (argc < 2) return 2;
	size_t n;
	char *data = dsk_read_file(argv[1], &n);
	if (!data) return 2;
	printf("animated %d\n", dsk_anim_is_animated((const uint8_t *)data, n));
	dsk_anim *a = dsk_anim_decode((const uint8_t *)data, n, NULL, NULL);
	if (!a) {
		printf("still\n");
		return 0;
	}
	printf("%dx%d loops %d frames %zu\n", a->width, a->height, a->loops, a->count);
	dsk_player p;
	dsk_player_init(&p, a);
	for (int i = 0; i < 8; i++) {
		int d = dsk_player_next(&p);
		printf("%d ", d);
		if (d < 0) break;
		for (int k = 0; k < a->width * a->height * 4; k++) printf("%02x", p.canvas[k]);
		printf("\n");
	}
	printf("\n");
	dsk_player_free(&p);
	dsk_anim_free(a);
	free(data);
	return 0;
}
