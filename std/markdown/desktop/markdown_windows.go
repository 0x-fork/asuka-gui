//go:build windows

package markdown

import (
	"math"
	"strings"
	"unicode/utf16"
	"unsafe"

	"github.com/egoist/asuka/internal/rtf"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
	"github.com/egoist/asuka/ops"
)

// Windows: the paragraphs as RTF (internal/rtf) in a read-only rich edit
// (msftedit's RICHEDIT50W: its typesetter, selection, copying and
// accessibility), in a window of the package's that it fills, which gets
// the rich edit's EN_REQUESTRESIZE to measure it. Links are the theme's
// accent, underlined; a click on one opens it.

const (
	emSetEventMask   = WM_USER + 69
	emRequestResize  = WM_USER + 65
	emSetBkgndColor  = WM_USER + 67
	emSetTextEx      = WM_USER + 97
	emCharFromPos    = 0x00D7
	enRequestResize  = 0x0701
	enmRequestResize = 0x00040000
)

var richLoaded bool

type richDocument struct {
	hwnd, ctl uintptr
	source    string
	set       bool
	links     []link
	requested int32 // EN_REQUESTRESIZE's height
	pressed   int
}

// link is a link's characters, [start, end), and where it goes.
type link struct {
	start, end int32
	url        string
}

func newDocument(events DocumentEvents) DocumentNative {
	if !richLoaded {
		richLoaded = true
		LoadSystemLibrary("msftedit.dll")
	}
	v := &richDocument{pressed: -1}
	v.hwnd = NewProcWindow(0, "", WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, events.Context.Parent(), func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case WM_NOTIFY:
			// NMHDR, then REQRESIZE's rect.
			hdr := Native[NMHdr](lp)
			if hdr.HwndFrom == v.ctl && hdr.Code == enRequestResize {
				v.requested = Native[struct {
					hdr  NMHdr
					rect Rect
				}](lp).rect.H()
				return 0, true
			}
		case WM_SIZE:
			r := ClientRect(hwnd)
			SetWindowPos(v.ctl, 0, 0, 0, r.W(), r.H(), SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE)
		}
		return 0, false
	})
	v.ctl = CreateWindow(0, "RICHEDIT50W", "", WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_NOHIDESEL, 0, 0, 0, 0, v.hwnd, 0)
	SendMessage(v.ctl, emSetEventMask, 0, enmRequestResize)
	SendMessage(v.ctl, EM_SETMARGINS, EC_LEFTMARGIN|EC_RIGHTMARGIN, 0)
	Subclass(v.ctl, v.message)
	return v
}

func (v *richDocument) Handle() uintptr { return v.hwnd }

// color is a semantic color as the theme has it now.
func color(s ops.SemanticColor, def rtf.Color) rtf.Color {
	c, ok := native.ResolveColor(int(ops.SemanticColorValue(s)))
	if !ok {
		return def
	}
	return rtf.Color{R: uint8(c[0]*255 + 0.5), G: uint8(c[1]*255 + 0.5), B: uint8(c[2]*255 + 0.5)}
}

func (v *richDocument) SetText(s string) {
	if v.set && s == v.source {
		return
	}
	v.source, v.set = s, true
	v.build()
}

// build sets the control's text from the source.
func (v *richDocument) build() {
	label := color(ops.SemanticColorLabel, rtf.Color{})
	secondary := color(ops.SemanticColorSecondaryLabel, rtf.Color{R: 96, G: 96, B: 96})
	accent := color(ops.SemanticColorAccent, rtf.Color{B: 200})
	code := color(ops.SemanticColorSecondaryBackground, rtf.Color{R: 240, G: 240, B: 240})
	bg := color(ops.SemanticColorBackground, rtf.Color{R: 255, G: 255, B: 255})
	const body = 10.5 // points: Segoe UI at 14 pixels
	sizes := [...]float32{20, 16, 13.5, 12, 10.5, 10.5}

	var runs []rtf.Run
	v.links = v.links[:0]
	at := int32(0) // the characters so far: a paragraph's end is one
	add := func(r rtf.Run) {
		if r.Link != "" {
			n := int32(len(utf16.Encode([]rune(r.Text))))
			v.links = append(v.links, link{start: at, end: at + n, url: r.Link})
			r.Link = ""
			r.Underline = true
			r.Color = &accent
		}
		at += int32(len(utf16.Encode([]rune(r.Text))))
		runs = append(runs, r)
	}
	ps := paragraphs(v.source)
	for i, p := range ps {
		if i > 0 {
			add(rtf.Run{Text: "\n"})
			if !ps[i-1].tight || !p.tight {
				add(rtf.Run{Text: "\n", Size: body / 2})
			}
		}
		lead := strings.Repeat("    ", max(p.indent-1, 0))
		if p.marker != "" {
			lead += p.marker + " "
		} else if p.indent > 0 {
			lead += "    "
		}
		if lead != "" {
			add(rtf.Run{Text: lead, Color: &secondary})
		}
		if p.rule {
			add(rtf.Run{Text: strings.Repeat("―", 24), Color: &secondary})
			continue
		}
		for _, r := range p.runs {
			x := rtf.Run{Text: r.text, Bold: r.bold, Italic: r.italic, Strike: r.strike, Mono: r.mono, Link: r.link}
			if p.code {
				x.Text = strings.ReplaceAll(x.Text, "\n", "\n"+strings.Repeat("    ", p.indent))
			}
			if r.mono {
				x.Background = &code
			}
			if r.quote {
				x.Color = &secondary
			}
			if p.heading > 0 {
				x.Bold = true
				x.Size = sizes[min(p.heading, 6)-1]
			}
			add(x)
		}
	}
	data := append(rtf.Write(runs, rtf.Doc{Face: "Segoe UI", MonoFace: "Cascadia Mono", Size: body * v.scale(), Color: label}), 0)
	SendMessage(v.ctl, emSetBkgndColor, 0, uintptr(bg.R)|uintptr(bg.G)<<8|uintptr(bg.B)<<16)
	settext := [2]uint32{0, 0} // SETTEXTEX: ST_DEFAULT, CP_ACP
	SendMessage(v.ctl, emSetTextEx, uintptr(unsafe.Pointer(&settext)), uintptr(unsafe.Pointer(&data[0])))
}

// scale is the rich edit's font sizes' factor: the window's DPI over
// the system's, which the control converts points at.
func (v *richDocument) scale() float32 {
	return float32(DPI(v.hwnd)) / float32(DPI(0))
}

// Measure is its height at the width it's given, in points.
func (v *richDocument) Measure(w, h float32) (float32, float32) {
	k := float32(DPI(v.hwnd)) / 96
	if w != w || w <= 0 {
		w = 10000
	}
	px := int32(math.Ceil(float64(w * k)))
	cur := ClientRect(v.ctl)
	SetWindowPos(v.ctl, 0, 0, 0, px, max(cur.H(), 1), SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOREDRAW)
	v.requested = 0
	SendMessage(v.ctl, emRequestResize, 0, 0)
	ht := v.requested
	r := ClientRect(v.hwnd)
	SetWindowPos(v.ctl, 0, 0, 0, r.W(), r.H(), SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOREDRAW)
	return w, float32(ht) / k
}

// linkAt is the link under p (the control's pixels), or -1.
func (v *richDocument) linkAt(p Point) int {
	pt := struct{ X, Y int32 }{p.X, p.Y}
	c := int32(SendMessage(v.ctl, emCharFromPos, 0, uintptr(unsafe.Pointer(&pt))))
	for i, l := range v.links {
		if c >= l.start && c < l.end {
			return i
		}
	}
	return -1
}

// message is the rich edit's subclass: links take clicks and show the
// hand; the wheel goes on to the scroll view it's in.
func (v *richDocument) message(hwnd uintptr, m uint32, w, l uintptr) uintptr {
	switch m {
	case WM_MOUSEWHEEL:
		return SendMessage(Parent(v.hwnd), m, w, l)
	case WM_SETCURSOR:
		if v.linkAt(ScreenToClient(hwnd, CursorPos())) >= 0 {
			SetCursor(Cursor(IDC_HAND))
			return 1
		}
	case WM_LBUTTONDOWN:
		v.pressed = v.linkAt(Point{X: XParam(l), Y: YParam(l)})
	case WM_LBUTTONUP:
		if k := v.linkAt(Point{X: XParam(l), Y: YParam(l)}); k >= 0 && k == v.pressed {
			r := DefSubclass(hwnd, m, w, l)
			OpenURL(v.links[k].url)
			return r
		}
	}
	return DefSubclass(hwnd, m, w, l)
}
