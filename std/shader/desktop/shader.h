// shader.h: what shader's Windows and Linux C share (shader.c): a
// program's code as the compiler encodes it (internal/shadercode), with
// the desktops' own programs in it (GL's and HLSL's, and where each
// lane goes in their uniform buffers), and the lanes a frame draws with.
#ifndef SHADER_H
#define SHADER_H

#include "shader.asuka.h"

enum { SH_FLOAT, SH_INT, SH_UINT, SH_BOOL };

typedef struct {
	char *name;
	uint32_t lane, lanes;
	uint8_t kind;
} sh_uniform;

typedef struct {
	char *source;  // the Kage source: the program's identity
	uint32_t lanes, size, scale;
	int32_t time;  // Time's lane, or -1
	sh_uniform *uniforms;
	size_t nuniforms;
	char *gl;  // GL's fragment shader, without its #version line
	uint32_t gl_size, *gl_offsets;
	char *hlsl;  // HLSL's pixel shader, asuka_ps
	uint32_t hlsl_size, *hlsl_offsets;
} sh_code;

// sh_decode reads a program's code: false (with a reason) when it's bad,
// or was compiled before the desktops had programs of their own.
bool sh_decode(const uint8_t *b, size_t n, sh_code *out, const char **why);
void sh_code_free(sh_code *c);

// sh_words are the lanes the app's uniforms set, in c's order (code's
// lanes of them, malloc's): a float's bits, an int's or a uint's, a
// bool's 0 or 1. The bridge's (rectSize, pixelScale, Time) are the
// view's to set.
uint32_t *sh_words(const sh_code *c, shader_list_Uniform us);

// sh_inputs sets a frame's lanes in in (code's lanes): words, then the
// box's size in points, the pixels a point, and Time in seconds.
void sh_inputs(const sh_code *c, const uint32_t *words, uint32_t *in, float width, float height, float scale, double time);

// sh_put writes in's lanes into a uniform buffer of size bytes at their
// offsets.
void sh_put(const uint32_t *in, uint32_t lanes, const uint32_t *offsets, uint8_t *buf, uint32_t size);

// sh_log says why a shader doesn't run, on stderr.
void sh_log(const char *why);

#endif
