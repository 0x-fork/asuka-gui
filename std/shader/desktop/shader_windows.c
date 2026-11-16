// std/shader on Windows: a child window of the package's; the program's
// HLSL (compiled with the app: internal/shadercode's HLSL) drawn on the
// GPU with Direct3D 11, off the main thread. One device (the GPU's, or
// WARP where there's none) draws every shader's frames, one at a time
// (its immediate context serves one thread at a time): a triangle over a
// texture of the window's size in pixels, the pixel shader compiled by
// Windows's compiler (D3DCompile, once per program), copied to a staging
// texture and read back on a thread of its own. The frame crosses back to
// the main thread, which swaps it in and paints it, and never waits for
// one. Inputs that change while a frame renders render again once it's
// done. Animated, a timer starts a frame whenever none is rendering, so
// the frame rate is what the GPU and the copy afford, up to the timer's.
#include "asuka_windows.h"
#include "shader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uxtheme.h>

enum { TIMER = 7 };

// ---- The device ----

typedef struct pipeline {
	char *source;  // the Kage source: the program's identity
	ID3D11PixelShader *ps;
	char *err;
	struct pipeline *next;
} pipeline;

static struct {
	SRWLOCK mu;
	ID3D11Device *dev;
	ID3D11DeviceContext *ctx;
	D3D_FEATURE_LEVEL level;
	char *err;  // why there's no device
	ID3D11VertexShader *vs;
	pipeline *pipelines;
	pD3DCompile compile;
} d3d = {.mu = SRWLOCK_INIT};

static const char *vertex =
    "float4 asuka_vs(uint id : SV_VertexID) : SV_Position {\n"
    "\tfloat2 p = float2((id << 1) & 2, id & 2);\n"
    "\treturn float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n";

static const char *target(const char *stage) {
	bool five = d3d.level >= D3D_FEATURE_LEVEL_11_0;
	if (stage[0] == 'v') return five ? "vs_5_0" : "vs_4_0";
	return five ? "ps_5_0" : "ps_4_0";
}

// hlsl compiles src's entry point for stage: its bytecode, or NULL with
// *err set.
static ID3DBlob *hlsl(const char *src, const char *entry, const char *stage, char **err) {
	if (!d3d.compile) {
		HMODULE m = LoadLibraryExW(L"d3dcompiler_47.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m) *(FARPROC *)&d3d.compile = GetProcAddress(m, "D3DCompile");
		if (!d3d.compile) {
			*err = asuka_strdup("Windows has no shader compiler (d3dcompiler_47.dll)");
			return NULL;
		}
	}
	ID3DBlob *code = NULL, *log = NULL;
	HRESULT hr = d3d.compile(src, strlen(src), "asuka", NULL, NULL, entry, target(stage), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &log);
	if (FAILED(hr)) {
		*err = asuka_sprintf("Direct3D doesn't compile it: %.*s", log ? (int)ID3D10Blob_GetBufferSize(log) : 0,
		                     log ? (const char *)ID3D10Blob_GetBufferPointer(log) : "");
		if (code) ID3D10Blob_Release(code);
		code = NULL;
	}
	if (log) ID3D10Blob_Release(log);
	return code;
}

static void reset(void) {
	for (pipeline *p = d3d.pipelines, *next; p; p = next) {
		next = p->next;
		if (p->ps) ID3D11PixelShader_Release(p->ps);
		free(p->source);
		free(p->err);
		free(p);
	}
	d3d.pipelines = NULL;
	if (d3d.vs) ID3D11VertexShader_Release(d3d.vs);
	if (d3d.ctx) ID3D11DeviceContext_Release(d3d.ctx);
	if (d3d.dev) ID3D11Device_Release(d3d.dev);
	d3d.vs = NULL, d3d.ctx = NULL, d3d.dev = NULL;
}

// device makes the device and the vertex shader the first time: false
// (d3d.err why) when there's none.
static bool device(void) {
	if (d3d.dev) return true;
	if (d3d.err) return false;
	static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
	HRESULT hr = E_FAIL;
	for (int i = 0; i < 2 && FAILED(hr); i++) {
		D3D_DRIVER_TYPE kind = i == 0 ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
		hr = D3D11CreateDevice(NULL, kind, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4, D3D11_SDK_VERSION, &d3d.dev, &d3d.level, &d3d.ctx);
		if (hr == E_INVALIDARG)  // Direct3D 11.0 has no 11_1
			hr = D3D11CreateDevice(NULL, kind, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, 3, D3D11_SDK_VERSION, &d3d.dev, &d3d.level,
			                       &d3d.ctx);
	}
	if (FAILED(hr)) {
		d3d.err = asuka_sprintf("no Direct3D 11 device (0x%08lx)", (unsigned long)hr);
		return false;
	}
	char *err = NULL;
	ID3DBlob *code = hlsl(vertex, "asuka_vs", "vs", &err);
	if (code) {
		hr = ID3D11Device_CreateVertexShader(d3d.dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &d3d.vs);
		ID3D10Blob_Release(code);
		if (FAILED(hr)) err = asuka_sprintf("no vertex shader (0x%08lx)", (unsigned long)hr);
	}
	if (err) {
		reset();
		d3d.err = err;
		return false;
	}
	return true;
}

// pipeline_of is c's pixel shader, compiled the first time.
static pipeline *pipeline_of(const sh_code *c) {
	for (pipeline *p = d3d.pipelines; p; p = p->next)
		if (strcmp(p->source, c->source) == 0) return p;
	pipeline *p = calloc(1, sizeof *p);
	p->source = asuka_strdup(c->source);
	p->next = d3d.pipelines;
	d3d.pipelines = p;
	ID3DBlob *code = hlsl(c->hlsl, "asuka_ps", "ps", &p->err);
	if (code) {
		HRESULT hr = ID3D11Device_CreatePixelShader(d3d.dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &p->ps);
		ID3D10Blob_Release(code);
		if (FAILED(hr)) p->err = asuka_sprintf("no pixel shader (0x%08lx)", (unsigned long)hr);
	}
	return p;
}

// ---- A view's target: its own on the GPU ----

// What a shader draws into: the texture its frames draw into, the one
// they're copied to for the CPU, and its constant buffers. Only frames'
// threads touch it, holding d3d.mu.
typedef struct {
	ID3D11Device *dev;  // the device it was made on
	int w, h;
	ID3D11Texture2D *tex, *staging;
	ID3D11RenderTargetView *rtv;
	ID3D11Buffer *cb, *step;
	UINT cb_size;
	uint8_t *cbuf;
	bool released;
} gpu;

static void gpu_free(gpu *t) {
	if (t->rtv) ID3D11RenderTargetView_Release(t->rtv);
	if (t->tex) ID3D11Texture2D_Release(t->tex);
	if (t->staging) ID3D11Texture2D_Release(t->staging);
	if (t->cb) ID3D11Buffer_Release(t->cb);
	if (t->step) ID3D11Buffer_Release(t->step);
	free(t->cbuf);
	*t = (gpu){.released = t->released};
}

static HRESULT buffer(UINT size, ID3D11Buffer **out) {
	D3D11_BUFFER_DESC d = {.ByteWidth = size, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
	return ID3D11Device_CreateBuffer(d3d.dev, &d, NULL, out);
}

// prepare makes the target's textures for frames of w by h pixels, and
// its constant buffers, on the current device.
static HRESULT prepare(gpu *t, int w, int h, UINT cb_size) {
	if (t->dev != d3d.dev) {
		gpu_free(t);
		t->dev = d3d.dev;
	}
	HRESULT hr = S_OK;
	if (!t->step && FAILED(hr = buffer(16, &t->step))) return hr;
	cb_size = (cb_size + 15) & ~15u;
	if (t->cb_size != cb_size) {
		if (t->cb) ID3D11Buffer_Release(t->cb);
		free(t->cbuf);
		t->cb = NULL, t->cbuf = NULL, t->cb_size = 0;
		if (cb_size > 0) {
			if (FAILED(hr = buffer(cb_size, &t->cb))) return hr;
			t->cb_size = cb_size;
			t->cbuf = calloc(cb_size, 1);
		}
	}
	if (t->tex && t->w == w && t->h == h) return S_OK;
	if (t->rtv) ID3D11RenderTargetView_Release(t->rtv);
	if (t->tex) ID3D11Texture2D_Release(t->tex);
	if (t->staging) ID3D11Texture2D_Release(t->staging);
	t->rtv = NULL, t->tex = NULL, t->staging = NULL, t->w = t->h = 0;
	D3D11_TEXTURE2D_DESC d = {.Width = (UINT)w, .Height = (UINT)h, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
	                          .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
	if (FAILED(hr = ID3D11Device_CreateTexture2D(d3d.dev, &d, NULL, &t->tex))) return hr;
	if (FAILED(hr = ID3D11Device_CreateRenderTargetView(d3d.dev, (ID3D11Resource *)t->tex, NULL, &t->rtv))) return hr;
	d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	if (FAILED(hr = ID3D11Device_CreateTexture2D(d3d.dev, &d, NULL, &t->staging))) return hr;
	t->w = w, t->h = h;
	return S_OK;
}

// draw draws a frame of c with the lanes in, for a view of w by h pixels,
// then reads it back into *pix (premultiplied BGRA, *pw by *ph, malloc's).
static HRESULT draw(gpu *t, const sh_code *c, const uint32_t *in, int w, int h, uint8_t **pix, int *pw, int *ph, char **err) {
	pipeline *p = pipeline_of(c);
	if (p->err) {
		*err = asuka_strdup(p->err);
		return E_FAIL;
	}
	// A view larger than a texture can be renders at a fraction of its
	// pixels, which the program's positions are still.
	int m = d3d.level >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
	int fw = w, fh = h;
	if (fw > m || fh > m) {
		double f = (double)m / fw < (double)m / fh ? (double)m / fw : (double)m / fh;
		fw = (int)(fw * f), fh = (int)(fh * f);
		if (fw < 1) fw = 1;
		if (fh < 1) fh = 1;
	}
	HRESULT hr = prepare(t, fw, fh, c->hlsl_size);
	if (FAILED(hr)) {
		*err = asuka_sprintf("no textures of %dx%d (0x%08lx)", fw, fh, (unsigned long)hr);
		return hr;
	}
	ID3D11DeviceContext *ctx = d3d.ctx;
	if (t->cb) {
		sh_put(in, c->lanes, c->hlsl_offsets, t->cbuf, t->cb_size);
		ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)t->cb, 0, NULL, t->cbuf, 0, 0);
	}
	float step[4] = {(float)w / (float)fw, (float)h / (float)fh, 0, 0};
	ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)t->step, 0, NULL, step, 0, 0);
	ID3D11DeviceContext_ClearState(ctx);
	ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &t->rtv, NULL);
	D3D11_VIEWPORT vp = {0, 0, (float)fw, (float)fh, 0, 1};
	ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
	ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D11DeviceContext_VSSetShader(ctx, d3d.vs, NULL, 0);
	ID3D11DeviceContext_PSSetShader(ctx, p->ps, NULL, 0);
	ID3D11Buffer *cbs[2] = {t->cb, t->step};
	ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 2, cbs);
	ID3D11DeviceContext_Draw(ctx, 3, 0);
	ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)t->staging, (ID3D11Resource *)t->tex);
	D3D11_MAPPED_SUBRESOURCE mp;
	if (FAILED(hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)t->staging, 0, D3D11_MAP_READ, 0, &mp))) {
		*err = asuka_sprintf("the frame doesn't map (0x%08lx)", (unsigned long)hr);
		return hr;
	}
	uint8_t *out = malloc((size_t)fw * fh * 4);
	for (int y = 0; y < fh; y++) memcpy(out + (size_t)y * fw * 4, (const uint8_t *)mp.pData + (size_t)y * mp.RowPitch, (size_t)fw * 4);
	ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)t->staging, 0);
	*pix = out, *pw = fw, *ph = fh;
	return S_OK;
}

// ---- The view ----

typedef struct view view;

typedef struct {
	view *v;
	unsigned gen;
	uint32_t *in;
	int w, h;
	sh_code *code;  // the program it draws, which stays while it renders
	gpu *t;
	uint8_t *pix;
	int pw, ph;
	char *err;
} frame;

struct view {
	shader_Shader base;
	HWND hwnd;
	sh_code *code;  // NULL: none
	bool failed;
	shader_Uniform *uniforms;  // the app's, kept for a program to come
	size_t nuniforms;
	uint32_t *words;
	bool animated;
	LARGE_INTEGER start;
	bool busy, dirty;
	unsigned gen;  // a frame started for an older program is dropped
	// The frame on show, over its pixels, pw by ph.
	HBITMAP bmp;
	void *bits;
	int pw, ph;
	uint32_t *last;  // the inputs of the last frame started
	int last_w, last_h;
	gpu *t;  // the GPU's side, which only frames' threads touch
	bool dead, gone;  // the view went; its window went
	sh_code *old;     // a program replaced while a frame renders with it
};

static void code_free(sh_code *c) {
	if (!c) return;
	sh_code_free(c);
	free(c);
}

static void free_view(view *v) {
	code_free(v->code);
	for (size_t i = 0; i < v->nuniforms; i++) shader_Uniform_free(&v->uniforms[i]);
	free(v->uniforms);
	free(v->words);
	free(v->last);
	if (v->bmp) DeleteObject(v->bmp);
	free(v);
}

static void release_gpu(void *p) {
	gpu *t = p;
	AcquireSRWLockExclusive(&d3d.mu);
	t->released = true;
	gpu_free(t);
	ReleaseSRWLockExclusive(&d3d.mu);
	free(t);
}

static double seconds(view *v) {
	LARGE_INTEGER now, f;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&f);
	return (double)(now.QuadPart - v->start.QuadPart) / (double)f.QuadPart;
}

static UINT dpi_of(HWND h) {
	static UINT(WINAPI * for_window)(HWND);
	static bool tried;
	if (!tried) {
		tried = true;
		*(FARPROC *)&for_window = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	}
	UINT d = for_window ? for_window(h) : 0;
	return d ? d : 96;
}

static void render(view *v);

static void rendered(void *p) {
	frame *f = p;
	view *v = f->v;
	v->busy = false;
	if (f->code == v->old) {  // the program it drew was replaced: its code goes
		code_free(v->old);
		v->old = NULL;
	}
	if (v->dead) {
		asuka_desktop_spawn(release_gpu, v->t);
		if (v->gone) free_view(v);
	} else if (v->gen == f->gen) {
		if (f->err) {
			v->failed = true;
			sh_log(f->err);
		} else if (f->pix) {
			BITMAPINFO bi = {.bmiHeader = {sizeof(BITMAPINFOHEADER), f->pw, -f->ph, 1, 32, BI_RGB}};
			void *bits = NULL;
			HBITMAP b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
			if (b) {
				memcpy(bits, f->pix, (size_t)f->pw * f->ph * 4);
				if (v->bmp) DeleteObject(v->bmp);
				v->bmp = b, v->bits = bits, v->pw = f->pw, v->ph = f->ph;
				InvalidateRect(v->hwnd, NULL, FALSE);
			}
		}
	}
	bool again = !v->dead && (v->dirty || v->gen != f->gen);
	v->dirty = false;
	free(f->in);
	free(f->pix);
	free(f->err);
	free(f);
	if (again) render(v);
}

static void run_frame(void *p) {
	frame *f = p;
	AcquireSRWLockExclusive(&d3d.mu);
	for (int i = 0; i < 2 && !f->t->released; i++) {  // again on a new device if the GPU was lost
		if (!device()) {
			f->err = asuka_strdup(d3d.err);
			break;
		}
		free(f->err);
		f->err = NULL;
		HRESULT hr = draw(f->t, f->code, f->in, f->w, f->h, &f->pix, &f->pw, &f->ph, &f->err);
		if (SUCCEEDED(hr) || ID3D11Device_GetDeviceRemovedReason(d3d.dev) == S_OK) break;
		reset();
	}
	ReleaseSRWLockExclusive(&d3d.mu);
	asuka_desktop_post(rendered, f);
}

// render starts a frame if its inputs changed since the last one, or
// marks them changed if one is rendering.
static void render(view *v) {
	if (v->dead || !v->code || v->failed) return;
	RECT r;
	GetClientRect(v->hwnd, &r);
	int w = r.right, h = r.bottom;
	if (w <= 0 || h <= 0) return;
	if (v->busy) {
		v->dirty = true;
		return;
	}
	float k = (float)dpi_of(v->hwnd) / 96;
	uint32_t *in = malloc(v->code->lanes * sizeof *in + 4);
	sh_inputs(v->code, v->words, in, (float)w / k, (float)h / k, k, seconds(v));
	if (v->last && w == v->last_w && h == v->last_h && memcmp(in, v->last, v->code->lanes * sizeof *in) == 0) {
		free(in);
		return;
	}
	free(v->last);
	v->last = malloc(v->code->lanes * sizeof *in + 4);
	memcpy(v->last, in, v->code->lanes * sizeof *in);
	v->last_w = w, v->last_h = h;
	frame *f = calloc(1, sizeof *f);
	*f = (frame){.v = v, .gen = v->gen, .in = in, .w = w, .h = h, .code = v->code, .t = v->t};
	v->busy = true;
	if (!asuka_desktop_spawn(run_frame, f)) {
		f->err = asuka_strdup("no thread to draw on");
		rendered(f);
	}
}

static void on_paint(view *v, HDC dc) {
	RECT r;
	GetClientRect(v->hwnd, &r);
	DrawThemeParentBackground(v->hwnd, dc, &r);
	if (r.right != v->last_w || r.bottom != v->last_h) render(v);  // resized: a frame for the new size
	if (!v->bmp) return;
	HDC mem = CreateCompatibleDC(dc);
	HGDIOBJ was = SelectObject(mem, v->bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	SetStretchBltMode(dc, HALFTONE);
	AlphaBlend(dc, 0, 0, r.right, r.bottom, mem, 0, 0, v->pw, v->ph, blend);
	SelectObject(mem, was);
	DeleteDC(mem);
}

static bool window(void *ctx, HWND hwnd, UINT m, WPARAM wp, LPARAM lp, LRESULT *result) {
	view *v = ctx;
	switch (m) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		on_paint(v, dc);
		EndPaint(hwnd, &ps);
		*result = 0;
		return true;
	}
	case WM_ERASEBKGND:
		*result = 1;
		return true;
	case WM_SIZE:
		render(v);
		break;
	case WM_TIMER:
		if (wp == TIMER) {
			render(v);
			*result = 0;
			return true;
		}
		break;
	case WM_NCDESTROY:
		v->gone = true;
		if (!v->dead) {  // the window went before the view: the view goes with it
			v->dead = true;
			if (!v->busy) asuka_desktop_spawn(release_gpu, v->t);
		}
		if (!v->busy) free_view(v);
		break;
	}
	return false;
}

// clock runs the timer of an animated shader's frames.
static void clock_(view *v) {
	if (v->animated && v->code && v->code->time >= 0) SetTimer(v->hwnd, TIMER, 16, NULL);
	else KillTimer(v->hwnd, TIMER);
}

static void set_program(shader_Shader *self, const shader_Program *p) {
	view *v = (view *)self;
	sh_code *c = calloc(1, sizeof *c);
	const char *why = NULL;
	if (!sh_decode(p->code.data, p->code.len, c, &why)) {
		free(c);
		sh_log(why);
		return;
	}
	if (v->code && strcmp(c->source, v->code->source) == 0) {
		code_free(c);
		return;
	}
	if (v->busy && v->code && !v->old) {  // the frame rendering reads it: it goes once it's done
		v->old = v->code;
	} else {  // (with an old one kept, the frame reads that)
		code_free(v->code);
	}
	v->code = c;
	v->failed = false;
	v->gen++;
	free(v->last);
	v->last = NULL;
	free(v->words);
	v->words = sh_words(v->code, (shader_list_Uniform){v->uniforms, v->nuniforms});
	clock_(v);
	render(v);
}

static void set_uniforms(shader_Shader *self, shader_list_Uniform us) {
	view *v = (view *)self;
	for (size_t i = 0; i < v->nuniforms; i++) shader_Uniform_free(&v->uniforms[i]);
	free(v->uniforms);
	v->uniforms = calloc(us.len ? us.len : 1, sizeof *v->uniforms);
	v->nuniforms = us.len;
	for (size_t i = 0; i < us.len; i++) {
		v->uniforms[i].name = asuka_strdup(us.data[i].name);
		size_t n = us.data[i].value.len;
		v->uniforms[i].value.data = malloc((n ? n : 1) * sizeof(double));
		if (n) memcpy(v->uniforms[i].value.data, us.data[i].value.data, n * sizeof(double));
		v->uniforms[i].value.len = n;
	}
	if (!v->code) return;
	free(v->words);
	v->words = sh_words(v->code, us);
	render(v);
}

static void set_animated(shader_Shader *self, bool on) {
	view *v = (view *)self;
	v->animated = on;
	clock_(v);
	render(v);
}

// measure: a shader has no size of its own.
static bool measure(shader_Shader *self, float w, float h, float *ow, float *oh) {
	*ow = 0;
	*oh = 0;
	return true;
}

// destroy: no more frames; the GPU's side goes on a thread (it waits for
// a frame rendering), the view once its window has too.
static void destroy(shader_Shader *self) {
	view *v = (view *)self;
	v->dead = true;
	KillTimer(v->hwnd, TIMER);
	if (!v->busy) asuka_desktop_spawn(release_gpu, v->t);
}

static const shader_Shader_vtable vt = {
    .destroy = destroy, .set_program = set_program, .set_uniforms = set_uniforms, .set_animated = set_animated, .measure = measure};

shader_Shader *shader_make_Shader(shader_Shader_events events) {
	view *v = calloc(1, sizeof *v);
	v->base.vt = &vt;
	v->t = calloc(1, sizeof *v->t);
	QueryPerformanceCounter(&v->start);
	v->hwnd = asuka_win_window(0, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, (HWND)events.parent, window, v);
	if (!v->hwnd) {
		free(v->t);
		free(v);
		return NULL;
	}
	v->base.widget = v->hwnd;
	return &v->base;
}
