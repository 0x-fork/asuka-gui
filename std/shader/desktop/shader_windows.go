//go:build windows

package shader

import (
	"log"
	"math"
	"slices"
	"time"

	"github.com/egoist/asuka/internal/shadercode"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: a child window of the package's, the program's Kage compiled
// to HLSL and drawn on the GPU with Direct3D 11 (shader_d3d_windows.go),
// off the main thread. A frame renders on a goroutine into a texture of
// the window's size in pixels, which the GPU copies back to memory, and
// crosses back with native.Post: the main thread swaps it in and paints
// it, and never waits for one. Inputs that change while a frame renders
// render again once it's done. Animated, a timer starts a frame whenever
// none is rendering, so the frame rate is what the GPU and the copy
// afford, up to the timer's.

const timerShader = 7

type d3dShader struct {
	hwnd     uintptr
	code     *shadercode.Code
	failed   bool
	uniforms []Uniform
	words    []uint32 // the uniforms' lanes
	animated bool
	start    time.Time

	busy  bool // a frame is rendering
	dirty bool // and the inputs changed since it started

	bmp    *Bitmap // the frame on show, over pix, pw by ph
	pix    []byte
	pw, ph int
	spare  []byte // the buffer the next frame renders into

	lastInputs   []uint32 // the inputs of the last frame started
	lastW, lastH int      // and the window's size in pixels for it

	target *d3dTarget // the GPU's side, which only the frames' goroutines touch
	gen    *struct{}  // a frame started for an older program is dropped
	dead   bool
}

func newShader(events ShaderEvents) ShaderNative {
	s := &d3dShader{start: time.Now(), gen: new(struct{}), target: new(d3dTarget)}
	s.hwnd = NewProcWindow(0, "", WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, events.Context.Parent(), func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case WM_PAINT:
			var ps PaintStruct
			dc := BeginPaint(hwnd, &ps)
			s.paint(dc)
			EndPaint(hwnd, &ps)
			return 0, true
		case WM_ERASEBKGND:
			return 1, true
		case WM_SIZE:
			s.render()
		case WM_TIMER:
			if wp == timerShader {
				s.render()
				return 0, true
			}
		}
		return 0, false
	})
	return s
}

func (s *d3dShader) Handle() uintptr { return s.hwnd }

func (s *d3dShader) SetProgram(p Program) {
	s.code, s.failed = nil, false
	s.gen = new(struct{})
	s.lastInputs = nil
	c, err := shadercode.Decode(p.Code)
	if err != nil {
		s.failed = true
		log.Printf("asuka: a shader doesn't run: %v", err)
		return
	}
	s.code = c
	s.words = words(c, s.uniforms)
	s.clock()
	s.render()
}

func (s *d3dShader) SetUniforms(us []Uniform) {
	s.uniforms = us
	if s.code != nil {
		s.words = words(s.code, us)
	}
	s.render()
}

func (s *d3dShader) SetAnimated(on bool) {
	s.animated = on
	s.clock()
	s.render()
}

// Measure: a shader has no size of its own.
func (s *d3dShader) Measure(w, h float32) (float32, float32) { return 0, 0 }

// Destroy lets go of the frame's bitmap and the GPU's side.
func (s *d3dShader) Destroy() {
	s.dead = true
	KillTimer(s.hwnd, timerShader)
	s.bmp.Close()
	s.bmp = nil
	s.gen = new(struct{}) // a frame rendering is dropped
	go s.target.release()
}

// clock runs the timer of an animated shader's frames.
func (s *d3dShader) clock() {
	if s.animated && s.code != nil && s.code.Time >= 0 {
		SetTimer(s.hwnd, timerShader, 16)
	} else {
		KillTimer(s.hwnd, timerShader)
	}
}

// render starts a frame if its inputs changed since the last one, or
// marks them changed if one is rendering.
func (s *d3dShader) render() {
	if s.dead || s.code == nil || s.failed {
		return
	}
	r := ClientRect(s.hwnd)
	w, ht := int(r.Right-r.Left), int(r.Bottom-r.Top)
	if w <= 0 || ht <= 0 || len(s.words) != s.code.Lanes {
		return
	}
	if s.busy {
		s.dirty = true
		return
	}
	k := float32(DPI(s.hwnd)) / 96
	c := s.code
	in := slices.Clone(s.words)
	in[c.Size] = math.Float32bits(float32(w) / k)
	in[c.Size+1] = math.Float32bits(float32(ht) / k)
	in[c.Scale] = math.Float32bits(k)
	if c.Time >= 0 {
		in[c.Time] = math.Float32bits(float32(time.Since(s.start).Seconds()))
	}
	if w == s.lastW && ht == s.lastH && slices.Equal(in, s.lastInputs) {
		return
	}
	s.lastInputs, s.lastW, s.lastH = in, w, ht
	buf := s.spare
	s.spare = nil
	s.busy, s.dirty = true, false
	gen, t := s.gen, s.target
	go func() {
		pix, pw, ph, err := t.render(c, in, w, ht, buf)
		native.Post(func() {
			s.busy = false
			if s.dead {
				return
			}
			if s.gen == gen {
				switch {
				case err != nil:
					s.failed = true
					log.Printf("asuka: a shader doesn't run: %v", err)
				case pix != nil:
					s.bmp.Close()
					s.spare = s.pix
					s.pix, s.pw, s.ph = pix, pw, ph
					s.bmp = NewBitmap(int32(pw), int32(ph), pix)
					Invalidate(s.hwnd)
				}
			}
			if s.dirty || s.gen != gen {
				s.dirty = false
				s.render()
			}
		})
	}()
}

// paint draws the frame on show over the window.
func (s *d3dShader) paint(dc uintptr) {
	r := ClientRect(s.hwnd)
	DrawParentBackground(s.hwnd, dc, &r)
	if int(r.W()) != s.lastW || int(r.H()) != s.lastH {
		s.render() // resized: a frame for the new size
	}
	if s.bmp == nil {
		return
	}
	g := NewGraphics(dc)
	defer g.Close()
	g.DrawImageClamped(s.bmp, 0, 0, float32(r.W()), float32(r.H()), 0, 0, float32(s.pw), float32(s.ph))
}
