// std/shader on Linux: a GtkGLArea draws the program's GLSL (compiled
// with the app: internal/shadercode's GL) on the GPU, a triangle over its
// whole framebuffer, its uniforms in a std140 block. GTK renders the area
// again when its size or scale changes; the package, when its uniforms
// or program do, and on each frame its frame clock draws when it's
// animated (Time moving on). GL's functions are libepoxy's, as GTK's own
// are. Where GDK has no GL (no driver, GDK_DISABLE=gl), it's empty.
#include "asuka_linux.h"
#include "shader.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef long GLsizeiptr;

enum {
	GL_FRAGMENT_SHADER = 0x8B30,
	GL_VERTEX_SHADER = 0x8B31,
	GL_COMPILE_STATUS = 0x8B81,
	GL_LINK_STATUS = 0x8B82,
	GL_INFO_LOG_LENGTH = 0x8B84,
	GL_UNIFORM_BUFFER = 0x8A11,
	GL_DYNAMIC_DRAW = 0x88E8,
	GL_TRIANGLES = 0x0004,
	GL_BLEND = 0x0BE2,
	GL_SCISSOR_TEST = 0x0C11,
	GL_DEPTH_TEST = 0x0B71,
	GL_VIEWPORT = 0x0BA2,
	GL_COLOR_BUFFER_BIT = 0x4000,
	GL_INVALID_INDEX = 0xFFFFFFFFu,
};

static struct {
	bool tried, ok;
	// GTK
	GtkWidget *(*area_new)(void);
	void (*set_allowed_apis)(GtkWidget *, int);
	void (*queue_render)(GtkWidget *);
	void (*make_current)(GtkWidget *);
	gpointer (*get_context)(GtkWidget *);
	GError *(*get_error)(GtkWidget *);
	gboolean (*use_es)(gpointer);
	int (*get_width)(GtkWidget *);
	int (*get_height)(GtkWidget *);
	guint (*add_tick)(GtkWidget *, gboolean (*)(GtkWidget *, gpointer, gpointer), gpointer, GDestroyNotify);
	void (*remove_tick)(GtkWidget *, guint);
	guint (*disconnect_matched)(gpointer, int, guint, GQuark, gpointer, gpointer, gpointer);
	// GL
	GLuint (*CreateShader)(GLenum);
	void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
	void (*CompileShader)(GLuint);
	void (*GetShaderiv)(GLuint, GLenum, GLint *);
	void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
	void (*DeleteShader)(GLuint);
	GLuint (*CreateProgram)(void);
	void (*AttachShader)(GLuint, GLuint);
	void (*LinkProgram)(GLuint);
	void (*GetProgramiv)(GLuint, GLenum, GLint *);
	void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
	void (*DeleteProgram)(GLuint);
	void (*UseProgram)(GLuint);
	GLuint (*GetUniformBlockIndex)(GLuint, const GLchar *);
	void (*UniformBlockBinding)(GLuint, GLuint, GLuint);
	GLint (*GetUniformLocation)(GLuint, const GLchar *);
	void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
	void (*GenBuffers)(GLsizei, GLuint *);
	void (*BindBuffer)(GLenum, GLuint);
	void (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
	void (*BindBufferBase)(GLenum, GLuint, GLuint);
	void (*DeleteBuffers)(GLsizei, const GLuint *);
	void (*GenVertexArrays)(GLsizei, GLuint *);
	void (*BindVertexArray)(GLuint);
	void (*DeleteVertexArrays)(GLsizei, const GLuint *);
	void (*Disable)(GLenum);
	void (*DrawArrays)(GLenum, GLint, GLsizei);
	void (*GetIntegerv)(GLenum, GLint *);
	void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	void (*Clear)(GLenum);
} gk;

static bool load(void) {
	if (gk.tried) return gk.ok;
	gk.tried = true;
	if (!asuka_linux()) return false;
	bool ok = true;
#define GTK(f, name) ok = (*(void **)&gk.f = asuka_linux_sym(name)) && ok
	GTK(area_new, "gtk_gl_area_new");
	GTK(set_allowed_apis, "gtk_gl_area_set_allowed_apis");
	GTK(queue_render, "gtk_gl_area_queue_render");
	GTK(make_current, "gtk_gl_area_make_current");
	GTK(get_context, "gtk_gl_area_get_context");
	GTK(get_error, "gtk_gl_area_get_error");
	GTK(use_es, "gdk_gl_context_get_use_es");
	GTK(get_width, "gtk_widget_get_width");
	GTK(get_height, "gtk_widget_get_height");
	GTK(add_tick, "gtk_widget_add_tick_callback");
	GTK(remove_tick, "gtk_widget_remove_tick_callback");
	GTK(disconnect_matched, "g_signal_handlers_disconnect_matched");
#undef GTK
	// libepoxy exports a pointer for each GL function, to a stub that
	// finds the function of the context current when it's called.
	void *epoxy = dlopen("libepoxy.so.0", RTLD_NOW | RTLD_GLOBAL);
	if (!epoxy) ok = false;
#define GL(f)                                                     \
	do {                                                          \
		void **p = epoxy ? dlsym(epoxy, "epoxy_gl" #f) : NULL;    \
		if (p && *p) *(void **)&gk.f = *p;                        \
		else ok = false;                                          \
	} while (0)
	GL(CreateShader);
	GL(ShaderSource);
	GL(CompileShader);
	GL(GetShaderiv);
	GL(GetShaderInfoLog);
	GL(DeleteShader);
	GL(CreateProgram);
	GL(AttachShader);
	GL(LinkProgram);
	GL(GetProgramiv);
	GL(GetProgramInfoLog);
	GL(DeleteProgram);
	GL(UseProgram);
	GL(GetUniformBlockIndex);
	GL(UniformBlockBinding);
	GL(GetUniformLocation);
	GL(Uniform4f);
	GL(GenBuffers);
	GL(BindBuffer);
	GL(BufferData);
	GL(BindBufferBase);
	GL(DeleteBuffers);
	GL(GenVertexArrays);
	GL(BindVertexArray);
	GL(DeleteVertexArrays);
	GL(Disable);
	GL(DrawArrays);
	GL(GetIntegerv);
	GL(ClearColor);
	GL(Clear);
#undef GL
	if (!ok) sh_log("this GTK has no GL areas, or there's no libepoxy");
	gk.ok = ok;
	return ok;
}

static const char *vertex =
    "void main() {\n"
    "\tvec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "\tgl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

typedef struct {
	shader_Shader base;
	GtkWidget *area;
	sh_code code;
	bool has_code;
	shader_Uniform *uniforms;  // the app's, kept for a program to come
	size_t nuniforms;
	uint32_t *words;  // their lanes
	uint32_t *in;     // a frame's
	bool animated;
	guint tick;
	struct timespec start;
	// The GL side, in the area's context (made as it draws, gone as it's
	// unrealized).
	GLuint program, vao, ubo;
	GLuint block;
	GLint frame;
	bool built, failed, logged;
	uint8_t *buf;
} view;

static double since(const struct timespec *t) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - t->tv_sec) + (double)(now.tv_nsec - t->tv_nsec) / 1e9;
}

static void fail(view *v, const char *why) {
	v->failed = true;
	if (!v->logged) sh_log(why);
	v->logged = true;
}

static GLuint compile(view *v, GLenum kind, const char *head, const char *src) {
	GLuint s = gk.CreateShader(kind);
	const char *parts[] = {head, src};
	gk.ShaderSource(s, 2, parts, NULL);
	gk.CompileShader(s);
	GLint ok = 0, n = 0;
	gk.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		gk.GetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
		char *log = calloc((size_t)(n > 1 ? n : 1) + 1, 1);
		gk.GetShaderInfoLog(s, n, NULL, log);
		char *why = asuka_sprintf("GL doesn't compile it: %s", log);
		fail(v, why);
		free(why);
		free(log);
		gk.DeleteShader(s);
		return 0;
	}
	return s;
}

// free_gl lets go of what the program made in the area's context, which
// is current.
static void free_gl(view *v) {
	if (v->program) gk.DeleteProgram(v->program);
	if (v->vao) gk.DeleteVertexArrays(1, &v->vao);
	if (v->ubo) gk.DeleteBuffers(1, &v->ubo);
	v->program = v->vao = v->ubo = 0;
	v->built = false;
}

// build compiles and links the program for the area's context.
static void build(view *v, gpointer ctx) {
	free_gl(v);
	v->built = true;
	const char *head = gk.use_es(ctx) ? "#version 300 es\nprecision highp float;\nprecision highp int;\n" : "#version 330 core\n";
	GLuint vs = compile(v, GL_VERTEX_SHADER, head, vertex);
	GLuint fs = vs ? compile(v, GL_FRAGMENT_SHADER, head, v->code.gl) : 0;
	if (!vs || !fs) {
		if (vs) gk.DeleteShader(vs);
		return;
	}
	GLuint p = gk.CreateProgram();
	gk.AttachShader(p, vs);
	gk.AttachShader(p, fs);
	gk.LinkProgram(p);
	gk.DeleteShader(vs);
	gk.DeleteShader(fs);
	GLint ok = 0, n = 0;
	gk.GetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		gk.GetProgramiv(p, GL_INFO_LOG_LENGTH, &n);
		char *log = calloc((size_t)(n > 1 ? n : 1) + 1, 1);
		gk.GetProgramInfoLog(p, n, NULL, log);
		char *why = asuka_sprintf("GL doesn't link it: %s", log);
		fail(v, why);
		free(why);
		free(log);
		gk.DeleteProgram(p);
		return;
	}
	v->program = p;
	v->block = gk.GetUniformBlockIndex(p, "AsukaUniforms");
	if (v->block != GL_INVALID_INDEX) gk.UniformBlockBinding(p, v->block, 0);
	v->frame = gk.GetUniformLocation(p, "asuka_frame");
	gk.GenVertexArrays(1, &v->vao);
	gk.GenBuffers(1, &v->ubo);
}

static gboolean render(GtkWidget *area, gpointer ctx, gpointer p) {
	view *v = p;
	gk.ClearColor(0, 0, 0, 0);
	gk.Clear(GL_COLOR_BUFFER_BIT);
	if (!v->has_code || v->failed) return true;
	if (!v->built) build(v, ctx);
	if (!v->program) return true;
	GLint vp[4] = {0};
	gk.GetIntegerv(GL_VIEWPORT, vp);
	float w = (float)gk.get_width(area), h = (float)gk.get_height(area);
	if (vp[2] <= 0 || vp[3] <= 0 || w <= 0 || h <= 0) return true;
	float k = (float)vp[2] / w;  // the framebuffer's pixels a point
	sh_inputs(&v->code, v->words, v->in, w, h, k, since(&v->start));
	gk.Disable(GL_BLEND);
	gk.Disable(GL_SCISSOR_TEST);
	gk.Disable(GL_DEPTH_TEST);
	gk.UseProgram(v->program);
	// The view's rows count down from its top; the framebuffer's up.
	if (v->frame >= 0) gk.Uniform4f(v->frame, 1, -1, 0, (float)vp[3]);
	if (v->block != GL_INVALID_INDEX && v->code.gl_size > 0) {
		memset(v->buf, 0, v->code.gl_size);
		sh_put(v->in, v->code.lanes, v->code.gl_offsets, v->buf, v->code.gl_size);
		gk.BindBuffer(GL_UNIFORM_BUFFER, v->ubo);
		gk.BufferData(GL_UNIFORM_BUFFER, v->code.gl_size, v->buf, GL_DYNAMIC_DRAW);
		gk.BindBufferBase(GL_UNIFORM_BUFFER, 0, v->ubo);
	}
	gk.BindVertexArray(v->vao);
	gk.DrawArrays(GL_TRIANGLES, 0, 3);
	gk.BindVertexArray(0);
	gk.UseProgram(0);
	return true;
}

// realized: the area has a context; a program it had is the old one's.
static void realized(GtkWidget *area, gpointer p) {
	view *v = p;
	v->built = false;
	v->program = v->vao = v->ubo = 0;
	GError *e = gk.get_error(area);
	if (e) {
		char *why = asuka_sprintf("GDK has no GL: %s", e->message);
		fail(v, why);
		free(why);
	}
}

static void unrealized(GtkWidget *area, gpointer p) {
	view *v = p;
	gk.make_current(area);
	if (!gk.get_error(area)) free_gl(v);
}

static gboolean ticked(GtkWidget *w, gpointer clock, gpointer p) {
	gk.queue_render(w);
	return true;  // G_SOURCE_CONTINUE
}

// clock runs an animated shader's frames on its area's frame clock.
static void clock_(view *v) {
	bool on = v->animated && v->has_code && v->code.time >= 0;
	if (on == (v->tick != 0)) return;
	if (on) {
		v->tick = gk.add_tick(v->area, ticked, v, NULL);
	} else {
		gk.remove_tick(v->area, v->tick);
		v->tick = 0;
	}
}

static void free_uniforms(view *v) {
	for (size_t i = 0; i < v->nuniforms; i++) shader_Uniform_free(&v->uniforms[i]);
	free(v->uniforms);
	v->uniforms = NULL;
	v->nuniforms = 0;
}

static void set_program(shader_Shader *self, const shader_Program *p) {
	view *v = (view *)self;
	sh_code c;
	const char *why = NULL;
	if (v->has_code && sh_decode(p->code.data, p->code.len, &c, &why)) {
		bool same = strcmp(c.source, v->code.source) == 0;
		sh_code_free(&c);
		if (same) return;
	}
	if (v->has_code) sh_code_free(&v->code);
	v->has_code = false;
	v->failed = v->logged = false;
	if (v->built) {
		gk.make_current(v->area);
		free_gl(v);
	}
	if (!sh_decode(p->code.data, p->code.len, &v->code, &why)) {
		fail(v, why);
	} else {
		v->has_code = true;
		free(v->in);
		free(v->buf);
		v->in = calloc(v->code.lanes ? v->code.lanes : 1, sizeof *v->in);
		v->buf = calloc(v->code.gl_size ? v->code.gl_size : 1, 1);
		free(v->words);
		v->words = sh_words(&v->code, (shader_list_Uniform){v->uniforms, v->nuniforms});
	}
	clock_(v);
	gk.queue_render(v->area);
}

static void set_uniforms(shader_Shader *self, shader_list_Uniform us) {
	view *v = (view *)self;
	free_uniforms(v);
	v->uniforms = calloc(us.len ? us.len : 1, sizeof *v->uniforms);
	v->nuniforms = us.len;
	for (size_t i = 0; i < us.len; i++) {
		v->uniforms[i].name = asuka_strdup(us.data[i].name);
		size_t n = us.data[i].value.len;
		v->uniforms[i].value.data = malloc((n ? n : 1) * sizeof(double));
		if (n) memcpy(v->uniforms[i].value.data, us.data[i].value.data, n * sizeof(double));
		v->uniforms[i].value.len = n;
	}
	if (!v->has_code) return;
	free(v->words);
	v->words = sh_words(&v->code, us);
	gk.queue_render(v->area);
}

static void set_animated(shader_Shader *self, bool on) {
	view *v = (view *)self;
	v->animated = on;
	clock_(v);
	gk.queue_render(v->area);
}

// measure: a shader has no size of its own.
static bool measure(shader_Shader *self, float w, float h, float *ow, float *oh) {
	*ow = 0;
	*oh = 0;
	return true;
}

static void destroy(shader_Shader *self) {
	view *v = (view *)self;
	if (v->tick) gk.remove_tick(v->area, v->tick);
	gk.disconnect_matched(v->area, 16 /* G_SIGNAL_MATCH_DATA */, 0, 0, NULL, NULL, v);
	if (v->built) {
		gk.make_current(v->area);
		free_gl(v);
	}
	G(g_object_unref)(v->area);
	free_uniforms(v);
	if (v->has_code) sh_code_free(&v->code);
	free(v->words);
	free(v->in);
	free(v->buf);
	free(v);
}

static const shader_Shader_vtable vt = {
    .destroy = destroy, .set_program = set_program, .set_uniforms = set_uniforms, .set_animated = set_animated, .measure = measure};

shader_Shader *shader_make_Shader(shader_Shader_events events) {
	if (!load()) return NULL;
	view *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	clock_gettime(CLOCK_MONOTONIC, &v->start);
	v->area = gk.area_new();
	G(g_object_ref_sink)(v->area);
	gk.set_allowed_apis(v->area, 3);  // GDK_GL_API_GL | GDK_GL_API_GLES
	g_signal_connect(v->area, "render", render, v);
	G(g_signal_connect_data)(v->area, "realize", (GCallback)realized, v, NULL, 1);  // G_CONNECT_AFTER: its context made
	g_signal_connect(v->area, "unrealize", unrealized, v);
	v->base.widget = v->area;
	return &v->base;
}
