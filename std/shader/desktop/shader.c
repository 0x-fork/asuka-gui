// shader.c: programs' code decoded, uniforms' lanes (shader.h), for
// shader_linux.c and shader_windows.c.
#include "shader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const uint8_t *b;
	size_t n, at;
	bool bad;
} rd;

static uint32_t u32(rd *r) {
	if (r->at + 4 > r->n) {
		r->bad = true;
		return 0;
	}
	const uint8_t *p = r->b + r->at;
	r->at += 4;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static char *str(rd *r) {
	uint32_t n = u32(r);
	if (r->bad || n > r->n - r->at) {
		r->bad = true;
		return asuka_strdup("");
	}
	char *s = asuka_strndup((const char *)r->b + r->at, n);
	r->at += n;
	return s;
}

static uint32_t *offsets(rd *r, uint32_t lanes) {
	uint32_t *o = calloc(lanes ? lanes : 1, sizeof *o);
	for (uint32_t i = 0; i < lanes && !r->bad; i++) o[i] = u32(r);
	return o;
}

bool sh_decode(const uint8_t *b, size_t n, sh_code *out, const char **why) {
	rd r = {b, n, 0, false};
	sh_code c = {0};
	c.source = str(&r);
	free(str(&r));  // Metal's
	free(str(&r));  // AGSL's
	c.lanes = u32(&r), c.size = u32(&r), c.scale = u32(&r), c.time = (int32_t)u32(&r) - 1;
	if (c.lanes > 1 << 16) r.bad = true;
	u32(&r);  // Metal's size
	for (uint32_t i = 0; i < c.lanes && !r.bad; i++) u32(&r);
	uint32_t nu = u32(&r);
	if (!r.bad && nu <= c.lanes + 3) {
		c.uniforms = calloc(nu ? nu : 1, sizeof *c.uniforms);
		for (uint32_t i = 0; i < nu && !r.bad; i++) {
			sh_uniform *u = &c.uniforms[c.nuniforms++];
			u->name = str(&r);
			u->lane = u32(&r), u->lanes = u32(&r);
			u->kind = r.at < r.n ? r.b[r.at++] : SH_FLOAT;
		}
	} else {
		r.bad = true;
	}
	if (!r.bad && r.at < r.n) free(str(&r));  // the web's GLSL
	bool desktop = !r.bad && r.at < r.n;
	if (desktop) {
		c.hlsl = str(&r);
		c.hlsl_size = u32(&r);
		c.hlsl_offsets = offsets(&r, c.lanes);
		desktop = !r.bad && r.at < r.n;
	}
	if (desktop) {
		c.gl = str(&r);
		c.gl_size = u32(&r);
		c.gl_offsets = offsets(&r, c.lanes);
	}
	if (!r.bad && (c.size + 1 >= c.lanes || c.scale >= c.lanes || c.time >= (int32_t)c.lanes)) r.bad = true;
	for (uint32_t i = 0; desktop && !r.bad && i < c.lanes; i++)
		if (c.gl_offsets[i] + 4 > c.gl_size || c.hlsl_offsets[i] + 4 > c.hlsl_size) r.bad = true;
	if (r.bad || !desktop) {
		*why = r.bad ? "its code is bad" : "it was compiled before the desktops had programs of their own: compile the app again";
		sh_code_free(&c);
		return false;
	}
	*out = c;
	return true;
}

void sh_code_free(sh_code *c) {
	for (size_t i = 0; i < c->nuniforms; i++) free(c->uniforms[i].name);
	free(c->uniforms);
	free(c->source);
	free(c->gl);
	free(c->gl_offsets);
	free(c->hlsl);
	free(c->hlsl_offsets);
	*c = (sh_code){0};
}

static uint32_t bits(float f) {
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

uint32_t *sh_words(const sh_code *c, shader_list_Uniform us) {
	uint32_t *w = calloc(c->lanes ? c->lanes : 1, sizeof *w);
	for (size_t i = 0; i < us.len; i++) {
		const shader_Uniform *u = &us.data[i];
		if (!u->name || strncmp(u->name, "__", 2) == 0) continue;
		for (size_t j = 0; j < c->nuniforms; j++) {
			const sh_uniform *v = &c->uniforms[j];
			if (strcmp(v->name, u->name) != 0) continue;
			for (uint32_t k = 0; k < v->lanes && k < u->value.len; k++) {
				uint32_t at = v->lane + k;
				if (at >= c->lanes) break;
				double x = u->value.data[k];
				switch (v->kind) {
				case SH_FLOAT: w[at] = bits((float)x); break;
				case SH_INT: w[at] = (uint32_t)(int32_t)round(x); break;
				case SH_UINT: w[at] = x > 0 ? (uint32_t)round(x) : 0; break;
				default: w[at] = x != 0;
				}
			}
		}
	}
	return w;
}

void sh_inputs(const sh_code *c, const uint32_t *words, uint32_t *in, float width, float height, float scale, double time) {
	memcpy(in, words, c->lanes * sizeof *in);
	in[c->size] = bits(width);
	in[c->size + 1] = bits(height);
	in[c->scale] = bits(scale);
	if (c->time >= 0) in[c->time] = bits((float)time);
}

void sh_put(const uint32_t *in, uint32_t lanes, const uint32_t *offsets, uint8_t *buf, uint32_t size) {
	for (uint32_t i = 0; i < lanes; i++)
		if (offsets[i] + 4 <= size) memcpy(buf + offsets[i], &in[i], 4);  // little-endian, as the GPU reads it
}

void sh_log(const char *why) { fprintf(stderr, "asuka: a shader doesn't run: %s\n", why); }
