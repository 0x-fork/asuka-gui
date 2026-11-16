//go:build darwin || linux

package bridge_test

import (
	"math"
	"testing"

	"github.com/egoist/asuka/host"
	"github.com/egoist/asuka/host/hosttest"
	"github.com/egoist/asuka/layout"
	"github.com/egoist/asuka/ops"
)

// recHost is a host that keeps what the core sent it: each view's kind
// and last frame. It measures as a desktop host does (texts a line, 19
// points; buttons 32), and has no size until it says so, as a window
// that reports its size once it shows.
type recHost struct {
	core   host.Core
	root   ops.NodeID
	kinds  map[ops.NodeID]ops.Kind
	frames map[ops.NodeID][4]float32
	posted []func()
}

func (h *recHost) Connect(c host.Core) { h.core = c }

func (h *recHost) Apply(b *ops.Batch) {
	for _, op := range b.Ops {
		switch op.Code {
		case ops.OpCreate:
			h.kinds[op.ID] = op.Kind
		case ops.OpSetRoot:
			h.root = op.ID
		case ops.OpSetFrame:
			h.frames[op.ID] = op.Frame
		}
	}
}

func (h *recHost) Measure(id ops.NodeID, c layout.Constraints) layout.Size {
	button := h.kinds[id] == ops.KindButton
	hgt := float32(19)
	if button {
		hgt = 32
	}
	if c.Width == 0 {
		if button {
			return layout.Size{H: 32}
		}
		return layout.Size{}
	}
	w := c.Width
	if math.IsNaN(float64(w)) {
		w = 53
	}
	return layout.Size{W: w, H: hgt}
}

func (h *recHost) Post(f func())                 { h.posted = append(h.posted, f) }
func (h *recHost) Environment() host.Environment { return host.Environment{} }

func (h *recHost) run() {
	for len(h.posted) > 0 {
		fs := h.posted
		h.posted = nil
		for _, f := range fs {
			f()
		}
	}
}

func (h *recHost) of(k ops.Kind) [4]float32 {
	for id, kind := range h.kinds {
		if kind == k {
			return h.frames[id]
		}
	}
	return [4]float32{}
}

// An app the core lays out before its window has a size (a desktop
// host's, which reports it as the window shows) is laid out again, its
// views measured at their width, once it has one.
func TestLaysOutOnceTheWindowHasASize(t *testing.T) {
	h := &recHost{kinds: map[ops.NodeID]ops.Kind{}, frames: map[ops.NodeID][4]float32{}}
	hosttest.Mount(t, h, func(f func()) { f(); h.run() }, `
		component Counter {
		  state count = 0
		  Column {
		    gap: 8; padding: 16
		    Text("Count: {count}")
		    Button("Add") { on press { count += 1 } }
		  }
		}
		app { Counter() }
	`)
	h.core.Dispatch(ops.Event{ID: h.root, Kind: ops.EventEnvironment, Env: host.Environment{Width: 800, Height: 600, Scale: 1, FontScale: 1}})
	h.run()
	if f := h.of(ops.KindText); f != [4]float32{16, 16, 768, 19} {
		t.Errorf("the text's frame: %v", f)
	}
	if f := h.of(ops.KindButton); f != [4]float32{16, 43, 768, 32} {
		t.Errorf("the button's frame: %v", f)
	}
}
