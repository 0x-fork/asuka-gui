//go:build darwin

package mactest

import (
	"image"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// The drawn kinds: Shape (a CAShapeLayer), Canvas (its display list
// replayed with Core Graphics), Shader (Metal, from the MSL the compiler
// writes) and Video (AVKit's player).

// snapPixel snaps the window's content and reads the color at x, y in
// node n's view (in points).
func snapPixel(t *testing.T, a *App, tr *Tree, n *Node, x, y float64) (r, g, b uint32) {
	t.Helper()
	p := filepath.Join(t.TempDir(), "snap.content.png")
	a.Snap(p)
	f, err := os.Open(p)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	k := float64(img.Bounds().Dx()) / vnum(tr.Environment, "width")
	at := image.Pt(int((n.InWindow[0]+x)*k), int((n.InWindow[1]+y)*k))
	cr, cg, cb, _ := img.At(at.X, at.Y).RGBA()
	return cr >> 8, cg >> 8, cb >> 8
}

// A Shape is its path in a shape layer filling its view, at its size, in
// its colors, line width, caps, joins and dashes; its trim animates in
// animate { }; what the canvas draws shows, and is drawn again when what
// it read changes.
func TestVisualShapeAndCanvas(t *testing.T) {
	a := Run(t, `
		component G {
		  state end = 0.5
		  state wide = false
		  Column {
		    padding: 20; gap: 10; align: .start
		    Shape((w, h) => Path().rect(0, 0, w, h)) { fill: #ff0000; width: 40; height: 30 }
		    Shape((w, h) => Path().move(0, h / 2).line(w, h / 2)) {
		      stroke: #0000ff; lineWidth: 3; lineCap: .round; lineJoin: .bevel; dash: [4, 2]; dashPhase: 1
		      trimEnd: end; width: 41; height: 30
		    }
		    Canvas(g => {
		      g.fill(Path().rect(0, 0, g.width, g.height), #00ff00)
		      if wide { g.fill(Path().rect(0, 0, g.width / 2, g.height), #ff0000) }
		    }) { width: 42; height: 30 }
		    Button("Trim") { on press { animate { end = 1 } } }
		    Button("Wide") { on press { wide = true } }
		  }
		}
		app { G() }
	`)
	tr := a.Until(func(tr *Tree) bool {
		ss := tr.Kind("Shape")
		return len(ss) == 2 && vobj(ss[0], "shape") != nil && len(tr.Kind("Canvas")) == 1 && tr.Kind("Canvas")[0].Num("canvasBytes") > 0
	})
	fill, line := kindSized(t, tr, "Shape", 40, 30), kindSized(t, tr, "Shape", 41, 30)
	if fill.Class != "AsukaShapeView" {
		t.Errorf("a %s", fill.Class)
	}
	s := vobj(fill, "shape")
	if b := vnums(s, "pathBox"); !slices.Equal(b, []float64{0, 0, 40, 30}) || !slices.Equal(vnums(s, "frame"), []float64{0, 0, 40, 30}) {
		t.Errorf("the filled shape: path %v, layer %v", b, s["frame"])
	}
	if vstr(s, "fill") == "" || s["stroke"] != nil {
		t.Errorf("colors: fill %v, stroke %v", s["fill"], s["stroke"])
	}
	s = vobj(line, "shape")
	if vnum(s, "lineWidth") != 3 || vstr(s, "lineCap") != "round" || vstr(s, "lineJoin") != "bevel" || vstr(s, "stroke") == "" {
		t.Errorf("the stroke: %v", s)
	}
	if !slices.Equal(vnums(s, "dash"), []float64{4, 2}) || vnum(s, "dashPhase") != 1 || vnum(s, "strokeEnd") != 0.5 {
		t.Errorf("dashes %v phase %v, trimmed to %v", s["dash"], s["dashPhase"], s["strokeEnd"])
	}
	if r, g, b := snapPixel(t, a, tr, fill, 20, 15); r < 200 || g > 60 || b > 60 {
		t.Errorf("the shape shows %d,%d,%d, not red", r, g, b)
	}
	canvas := tr.Kind("Canvas")[0]
	if canvas.Class != "AsukaCanvasView" || canvas.Frame[2] != 42 {
		t.Errorf("the canvas: %s %v", canvas.Class, canvas.Frame)
	}
	if r, g, b := snapPixel(t, a, tr, canvas, 10, 15); g < 200 || r > 60 || b > 60 {
		t.Errorf("the canvas shows %d,%d,%d, not green", r, g, b)
	}

	a.Do("tap Trim")
	tr = a.Until(func(tr *Tree) bool { return vnum(vobj(kindSized(t, tr, "Shape", 41, 30), "shape"), "strokeEnd") == 1 })
	if d := vnum(vobj(kindSized(t, tr, "Shape", 41, 30), "shape"), "strokeEndAnimation"); d == 0 {
		t.Errorf("the trim doesn't animate")
	}

	before := tr.Kind("Canvas")[0].Num("canvasBytes")
	a.Do("tap Wide")
	tr = a.Until(func(tr *Tree) bool { return tr.Kind("Canvas")[0].Num("canvasBytes") > before })
	if r, g, b := snapPixel(t, a, tr, tr.Kind("Canvas")[0], 10, 15); r < 200 || g > 60 || b > 60 {
		t.Errorf("drawn again, the canvas shows %d,%d,%d, not red", r, g, b)
	}
}

// A Shader is a CAMetalLayer under its view's subviews, its drawable the
// view's size in pixels, drawn by the program's Metal library; an
// animated one draws every frame while it's shown, on a clock of its own
// that stops once none is.
func TestVisualShader(t *testing.T) {
	a := Run(t, `
		shader Solid(blue: float) {
		  fn fragment(pos: vec2) -> vec4 = vec4(0, 0, blue, 1)
		}

		shader Flow {
		  fn fragment(pos: vec2) -> vec4 = vec4(0.5 + 0.5 * sin(time), 0, 0, 1)
		}

		component S {
		  state shown = true
		  state blue = 0.0
		  Column {
		    padding: 20; gap: 10; align: .start
		    Solid(blue: blue) { width: 50; height: 30 }
		    if shown { Flow { animated: true; width: 51; height: 30 } }
		    Button("Blue") { on press { blue = 1 } }
		    Button("Stop") { on press { shown = false } }
		  }
		}
		app { S() }
	`)
	tr := a.Until(func(tr *Tree) bool {
		ss := tr.Kind("Shader")
		return len(ss) == 2 && vnum(vobj(ss[0], "shader"), "drawn") > 0 && vnum(vobj(ss[1], "shader"), "drawn") > 0
	})
	solid, flow := kindSized(t, tr, "Shader", 50, 30), kindSized(t, tr, "Shader", 51, 30)
	s := vobj(solid, "shader")
	scale := vnum(tr.Environment, "scale")
	if !vbool(s, "pipeline") || vbool(s, "failed") || vnum(s, "layerIndex") != 0 {
		t.Fatalf("the solid shader: %v", s)
	}
	if d := vnums(s, "drawableSize"); len(d) != 2 || d[0] != 50*scale || d[1] != 30*scale {
		t.Errorf("drawable %v at scale %v", d, scale)
	}
	if !vbool(vobj(flow, "shader"), "animated") || !vbool(s, "clock") {
		t.Errorf("no clock for the animated shader")
	}
	// The animated one draws on; the other only as its uniforms change.
	drawn, drawnSolid := vnum(vobj(flow, "shader"), "drawn"), vnum(s, "drawn")
	tr = a.Until(func(tr *Tree) bool {
		return vnum(vobj(kindSized(t, tr, "Shader", 51, 30), "shader"), "drawn") > drawn+5
	})
	if d := vnum(vobj(kindSized(t, tr, "Shader", 50, 30), "shader"), "drawn"); d != drawnSolid {
		t.Errorf("the still shader drew %v times, was %v", d, drawnSolid)
	}
	a.Do("tap Blue")
	a.Until(func(tr *Tree) bool {
		return vnum(vobj(kindSized(t, tr, "Shader", 50, 30), "shader"), "drawn") > drawnSolid
	})
	a.Do("tap Stop")
	tr = a.Until(func(tr *Tree) bool { return len(tr.Kind("Shader")) == 1 && !vbool(visualState(tr), "shaderClock") })
}

// A Video is AVKit's player view (floating controls, or none), playing
// its source as it's told: autoPlay, muted, looped (Ended each time),
// paused, sought; its mode is the video's gravity; without a size it
// takes the video's; a source that can't play is its error.
func TestVisualVideo(t *testing.T) {
	ffmpeg, err := exec.LookPath("ffmpeg")
	if err != nil {
		t.Skip("no ffmpeg to make a video")
	}
	path := filepath.Join(t.TempDir(), "red.mp4")
	if b, err := exec.Command(ffmpeg, "-loglevel", "error", "-f", "lavfi", "-i", "color=c=red:s=64x48:d=0.4:r=10",
		"-pix_fmt", "yuv420p", path).CombinedOutput(); err != nil {
		t.Fatalf("ffmpeg: %v\n%s", err, b)
	}
	a := Run(t, `
		component V {
		  state log = ""
		  let player = Ref()
		  Column {
		    padding: 20; gap: 10; align: .start
		    Video("`+path+`") {
		      ref: player; autoPlay: true; loop: true; muted: true; mode: .fill; hideControls: true
		      on end { log = log + "end;" }
		    }
		    Video("/nonexistent/asuka.mp4") { width: 40; height: 30; on error(m) { log = log + "error;" } }
		    Button("Pause") { on press { player.pause() } }
		    Button("Play") { on press { player.seek(0); player.play() } }
		    Text("log:{log}")
		  }
		}
		app { V() }
	`)
	l := &vlogger{a: a}
	l.until(t, func(s string) bool { return strings.Count(s, "end;") >= 2 && strings.Contains(s, "error;") })
	tr := a.Tree()
	v := tr.Kind("Video")[0]
	if v.Frame[2] != 64 || v.Frame[3] != 48 {
		t.Errorf("the video's natural size: %v", v.Frame)
	}
	j := vobj(v, "video")
	if !vbool(j, "muted") || !vbool(j, "loop") || vnum(j, "controls") != 0 || vstr(j, "gravity") != "AVLayerVideoGravityResizeAspectFill" || !vbool(j, "sized") {
		t.Errorf("the player: %v", j)
	}
	if !strings.Contains(v.Class, "AVPlayerView") {
		t.Errorf("a %s", v.Class)
	}
	a.Do("tap Pause")
	a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("Video")[0], "video"), "rate") == 0 })
	a.Do("tap Play")
	a.Until(func(tr *Tree) bool { return vnum(vobj(tr.Kind("Video")[0], "video"), "rate") == 1 })
	if other := vobj(tr.Kind("Video")[1], "video"); vnum(other, "controls") == 0 {
		t.Errorf("a video's controls show by default: %v", other)
	}
}
