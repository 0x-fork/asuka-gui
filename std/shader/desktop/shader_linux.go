//go:build linux && !android

package shader

import (
	"encoding/binary"
	"log"
	"math"
	"slices"
	"sync"
	"time"

	"github.com/ebitengine/purego"
	. "github.com/egoist/asuka/internal/gtk"
	ishader "github.com/egoist/asuka/internal/shader"
	"github.com/egoist/asuka/internal/shadercode"
	"github.com/egoist/asuka/internal/shaderir/interp"
	"github.com/egoist/asuka/native"
)

// Linux: a widget of the package's (AsukaShaderView), the program's Kage
// compiled to GLSL and drawn on the GPU with OpenGL, in a context of
// GDK's, into a texture its snapshot draws over it (shader_gl_linux.go).
// A frame renders as the widget snapshots, when its inputs changed since
// the last one: its uniforms, its size in pixels, the scale, and Time,
// which an animated shader's frame clock moves on each frame the display
// draws. Where GDK has no GL (no driver, GDK_DISABLE=gl), the program
// runs on the CPU instead, off the main thread (shader_cpu_linux.go),
// at a fraction of the pixels when they're many.

var le = binary.LittleEndian

const (
	kageFramePixels  = 64 << 10  // an animated shader's pixels a frame on the CPU, at most
	kageStaticPixels = 512 << 10 // a still one's
)

var (
	shaderOnce  sync.Once
	shaderType  GType
	shaderViews = map[Ptr]*glShader{}
)

type glShader struct {
	widget   Ptr
	code     *shadercode.Code
	uniforms []Uniform
	words    []uint32 // the uniforms' lanes
	animated bool
	start    time.Time
	tick     uint32 // the frame clock's callback, animated

	texture Ptr // the frame on show

	lastInputs   []uint32 // the inputs of the last frame rendered (or started)
	lastW, lastH int      // and the widget's size in pixels for it

	cpu *cpuShader // without GL
}

// cpuShader is a program run on the CPU (package interp).
type cpuShader struct {
	prog   *interp.Program
	failed bool
	busy   bool      // a frame is rendering
	dirty  bool      // and the inputs changed since it started
	spare  []byte    // the buffer the next frame renders into
	gen    *struct{} // a frame started for an older program is dropped
}

func shaderClass() {
	shaderOnce.Do(func() {
		snapshot := purego.NewCallback(func(widget, snap uintptr) uintptr {
			if s := shaderViews[widget]; s != nil {
				s.snapshot(snap)
			}
			return 0
		})
		measure := purego.NewCallback(func(widget uintptr, orientation, forSize int32, mn, nat, minB, natB uintptr) uintptr {
			*At[int32](mn), *At[int32](nat) = 0, 0
			if minB != 0 {
				*At[int32](minB) = -1
			}
			if natB != 0 {
				*At[int32](natB) = -1
			}
			return 0
		})
		shaderType = Subclass(Call(Sym("gtk_widget_get_type")), "AsukaShaderView", func(class, parent Ptr) {
			SetVfunc(class, OffWidgetSnapshot, snapshot)
			SetVfunc(class, OffWidgetMeasure, measure)
		})
	})
}

func newShader(events ShaderEvents) ShaderNative {
	shaderClass()
	s := &glShader{widget: NewObject(shaderType), start: time.Now()}
	shaderViews[s.widget] = s
	return s
}

func (s *glShader) Handle() uintptr { return s.widget }

func (s *glShader) SetProgram(p Program) {
	s.code, s.lastInputs, s.cpu = nil, nil, nil
	c, err := shadercode.Decode(p.Code)
	if err != nil {
		log.Printf("asuka: a shader doesn't run: %v", err)
	} else {
		s.code = c
		s.words = words(c, s.uniforms)
	}
	s.clock()
	Call(FnWidgetQueueDraw, s.widget)
}

func (s *glShader) SetUniforms(us []Uniform) {
	s.uniforms = us
	if s.code != nil {
		s.words = words(s.code, us)
	}
	Call(FnWidgetQueueDraw, s.widget)
}

func (s *glShader) SetAnimated(on bool) {
	s.animated = on
	s.clock()
	Call(FnWidgetQueueDraw, s.widget)
}

func (s *glShader) Destroy() {
	if s.tick != 0 {
		Draw.WidgetRemoveTickCallback(s.widget, s.tick)
		s.tick = 0
	}
	if s.texture != 0 {
		ObjectUnref(s.texture)
		s.texture = 0
	}
	if s.cpu != nil {
		s.cpu.gen = new(struct{}) // a frame rendering is dropped
	}
	delete(shaderViews, s.widget)
}

// clock runs an animated shader's frames on its widget's frame clock:
// each frame queues a draw, which renders one with the new Time.
func (s *glShader) clock() {
	on := s.animated && s.code != nil && s.code.Time >= 0
	if on == (s.tick != 0) {
		return
	}
	if !on {
		Draw.WidgetRemoveTickCallback(s.widget, s.tick)
		s.tick = 0
		return
	}
	s.tick = AddTickCallback(s.widget, func(Ptr) bool {
		if shaderViews[s.widget] != s || s.tick == 0 {
			return false
		}
		Call(FnWidgetQueueDraw, s.widget)
		return true
	})
}

// scale is the pixels a point of the window the widget is in.
func (s *glShader) scale() float32 {
	if nat := WidgetGetNative(s.widget); nat != 0 {
		if sf := NativeGetSurface(nat); sf != 0 {
			if SurfaceGetScale != nil {
				return float32(SurfaceGetScale(sf))
			}
			return float32(SurfaceGetScaleFactor(sf))
		}
	}
	return 1
}

// inputs are the lanes the next frame renders with, and its size in
// pixels; changed is false when they're the last frame's.
func (s *glShader) inputs() (in []uint32, w, ht int, k float32, changed bool) {
	if s.code == nil {
		return nil, 0, 0, 0, false
	}
	k = s.scale()
	fw, fh := float32(Call(FnWidgetGetWidth, s.widget)), float32(Call(FnWidgetGetHeight, s.widget))
	w, ht = int(math.Ceil(float64(fw*k))), int(math.Ceil(float64(fh*k)))
	c := s.code
	if w <= 0 || ht <= 0 || len(s.words) != c.Lanes {
		return nil, 0, 0, 0, false
	}
	in = slices.Clone(s.words)
	in[c.Size] = math.Float32bits(float32(w) / k)
	in[c.Size+1] = math.Float32bits(float32(ht) / k)
	in[c.Scale] = math.Float32bits(k)
	if c.Time >= 0 {
		in[c.Time] = math.Float32bits(float32(time.Since(s.start).Seconds()))
	}
	if w == s.lastW && ht == s.lastH && slices.Equal(in, s.lastInputs) {
		return nil, 0, 0, 0, false
	}
	return in, w, ht, k, true
}

// render renders a frame if its inputs changed since the last one: on
// the GPU, or, without GL, by starting one on the CPU.
func (s *glShader) render() {
	if s.cpu == nil && s.code != nil && glReady() {
		in, w, ht, _, changed := s.inputs()
		if !changed {
			return
		}
		s.lastInputs, s.lastW, s.lastH = in, w, ht
		if t := renderGL(s.code, in, w, ht); t != 0 {
			if s.texture != 0 {
				ObjectUnref(s.texture)
			}
			s.texture = t
		}
		return
	}
	s.renderCPU()
}

// renderCPU starts a frame on the CPU if its inputs changed since the
// last one, or marks them changed if one is rendering.
func (s *glShader) renderCPU() {
	if s.code == nil {
		return
	}
	if s.cpu == nil {
		s.cpu = &cpuShader{gen: new(struct{})}
		p, err := ishader.CompileFragment([]byte(s.code.Source))
		if err == nil {
			s.cpu.prog, err = interp.New(p)
		}
		if err != nil {
			s.cpu.failed = true
			log.Printf("asuka: a shader doesn't run: %v", err)
		}
	}
	cs := s.cpu
	if cs.failed {
		return
	}
	in, w, ht, k, changed := s.inputs()
	if !changed {
		return
	}
	if cs.busy {
		cs.dirty = true
		return
	}
	s.lastInputs, s.lastW, s.lastH = in, w, ht
	budget := kageStaticPixels
	if s.animated && s.code.Time >= 0 {
		budget = kageFramePixels
	}
	step := max(1, math.Sqrt(float64(w*ht)/float64(budget)))
	pw, ph := max(1, int(math.Ceil(float64(w)/step))), max(1, int(math.Ceil(float64(ht)/step)))
	buf := cs.spare
	cs.spare = nil
	if cap(buf) < pw*ph*4 {
		buf = make([]byte, pw*ph*4)
	}
	buf = buf[:pw*ph*4]
	cs.busy, cs.dirty = true, false
	gen, prog := cs.gen, cs.prog
	sx, sy := float32(w)/float32(pw), float32(ht)/float32(ph)
	go func() {
		ok := kageShade(prog, in, buf, pw, ph, sx, sy, k)
		native.Post(func() {
			if shaderViews[s.widget] != s || s.cpu != cs {
				return
			}
			cs.busy = false
			if cs.gen == gen && ok {
				b := BytesNew(&buf[0], uintptr(len(buf)))
				t := MemoryTextureNew(int32(pw), int32(ph), MemoryB8G8R8A8Premultiplied, b, uintptr(pw*4))
				BytesUnref(b)
				if s.texture != 0 {
					ObjectUnref(s.texture)
				}
				s.texture = t
				cs.spare = buf // GBytes copied it
				Call(FnWidgetQueueDraw, s.widget)
			}
			if cs.dirty {
				cs.dirty = false
				Call(FnWidgetQueueDraw, s.widget) // renders again as it draws
			}
		})
	}()
}

// snapshot draws the frame on show over the widget, after rendering a
// new one if its inputs changed.
func (s *glShader) snapshot(snap Ptr) {
	s.render()
	if s.texture == 0 {
		return
	}
	bounds := Rect{W: float32(Call(FnWidgetGetWidth, s.widget)), H: float32(Call(FnWidgetGetHeight, s.widget))}
	Call(Draw.FnSnapshotAppendScaledTexure, snap, s.texture, 0, Addr(&bounds)) // GSK_SCALING_FILTER_LINEAR
}
