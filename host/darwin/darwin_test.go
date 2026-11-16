//go:build darwin && !ios

package darwin

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/egoist/asuka/colors"
	. "github.com/egoist/asuka/internal/darwin"
	"github.com/egoist/asuka/ops"
	"github.com/egoist/asuka/signal"
	"github.com/egoist/asuka/ui"
)

// AppKit runs on the main thread: TestMain keeps it, and the tests send
// their work there.
var mainQ = make(chan func())

func TestMain(m *testing.M) {
	done := make(chan int)
	go func() { done <- m.Run() }()
	for {
		select {
		case f := <-mainQ:
			f()
		case code := <-done:
			os.Exit(code)
		}
	}
}

// onMain runs f on the main thread. A failure in f (fatal) ends f,
// not the main thread's loop, which t.Fatal's Goexit would.
func onMain(f func()) {
	ok := make(chan struct{})
	mainQ <- func() {
		defer close(ok)
		defer func() {
			if r := recover(); r != nil && r != errFatal {
				panic(r)
			}
		}()
		WithPool(f)
	}
	<-ok
}

var errFatal = new(int)

// fatal fails the test and ends the onMain func it's called in.
func fatal(t *testing.T, args ...any) {
	t.Helper()
	t.Error(args...)
	panic(errFatal)
}

func fatalf(t *testing.T, format string, args ...any) {
	t.Helper()
	t.Errorf(format, args...)
	panic(errFatal)
}

// mount mounts root on a new host (a window of its own), as Run does but
// without AppKit's event loop: work the host defers runs at once.
func mount(t *testing.T, root func() ui.Node) (*Host, *ui.Root) {
	var h *Host
	var r *ui.Root
	onMain(func() {
		h = newHost()
		h.enter(func() { r = ui.Mount(h, root) })
	})
	t.Cleanup(func() {
		onMain(func() {
			h.enter(r.Dispose)
			Send(h.window, "close")
		})
	})
	return h, r
}

// find returns the nodes of kind k.
func find(h *Host, k ops.Kind) []*node {
	var out []*node
	for _, n := range h.nodes {
		if n.kind == k {
			out = append(out, n)
		}
	}
	return out
}

func text(n *node) string {
	if n.tracking != 0 {
		return GoString(Send(n.tracking, "string"))
	}
	return GoString(Send(n.view, "stringValue"))
}

func TestStackPushAndBack(t *testing.T) {
	var nav *ui.Navigator
	disposed := 0
	h, _ := mount(t, func() ui.Node {
		return ui.Stack(func() ui.Node {
			nav = ui.UseNavigator()
			return ui.StaticText("Home")
		}, ui.ScreenOptions{Title: ui.Val("Home")})
	})
	onMain(func() {
		h.enter(func() {
			ui.Post(func() {
				nav.Push(func() ui.Node {
					signal.OnCleanup(func() { disposed++ })
					return ui.StaticText("Detail")
				}, ui.ScreenOptions{Title: ui.Val("Detail")})
			})
		})
		h.runPosted()
		if s := GoString(Send(h.window, "title")); s != "Detail" {
			fatalf(t, "the window's title is %q", s)
		}
		if !h.main.chrome.back || h.main.chrome.toolbar == 0 {
			fatal(t, "no back button")
		}
		// The toolbar's back button: Popped, and the core disposes the screen.
		h.back(h.main)
		if disposed != 1 {
			fatalf(t, "the popped screen's owner was disposed %d times", disposed)
		}
		if s := GoString(Send(h.window, "title")); s != "Home" {
			fatalf(t, "after back, the title is %q", s)
		}
		if nav.Depth() != 1 {
			fatalf(t, "depth %d", nav.Depth())
		}
	})
}

func firstText(n *node) *node {
	if n.kind == ops.KindText {
		return n
	}
	for _, c := range n.children {
		if t := firstText(c); t != nil {
			return t
		}
	}
	return nil
}

func TestModules(t *testing.T) {
	h, _ := mount(t, func() ui.Node { return ui.StaticText("x") })
	onMain(func() {
		call := func(module, m string, args []byte) ([]byte, error) {
			var out []byte
			var err error
			h.module(module)[m](args, func(b []byte, e error) { out, err = b, e })
			return out, err
		}
		b, err := call("device", "info", nil)
		if err != nil || !strings.Contains(string(b), `"system":"macOS"`) {
			fatalf(t, "device.info: %s, %v", b, err)
		}
		key := fmt.Sprintf("asuka-test-%d", time.Now().UnixNano())
		if b, err := call("secureStore", "get", []byte(key)); err != nil || len(b) != 1 || b[0] != 0 {
			fatalf(t, "a missing key: %v, %v", b, err)
		}
		args := binary.LittleEndian.AppendUint32(nil, uint32(len(key)))
		args = append(append(args, key...), "secret"...)
		if _, err := call("secureStore", "set", args); err != nil {
			fatal(t, err)
		}
		if b, err := call("secureStore", "get", []byte(key)); err != nil || string(b) != "\x01secret" {
			fatalf(t, "get after set: %q, %v", b, err)
		}
		if _, err := call("secureStore", "delete", []byte(key)); err != nil {
			fatal(t, err)
		}
		if b, _ := call("secureStore", "get", []byte(key)); len(b) != 1 || b[0] != 0 {
			fatalf(t, "after delete: %q", b)
		}
		// ui.ShareFiles of a file that isn't there: an error, no picker.
		le := binary.LittleEndian
		share := le.AppendUint32(nil, 0)
		for range 3 {
			share = le.AppendUint32(share, 0)
		}
		share = le.AppendUint32(share, 1)
		share = append(le.AppendUint32(share, uint32(len("/nonexistent/a.pdf"))), "/nonexistent/a.pdf"...)
		if _, err := call("share", "files", share); err == nil || !strings.Contains(err.Error(), "/nonexistent/a.pdf") || len(sharePickers) != 0 {
			fatalf(t, "share.files of a missing file: %v", err)
		}
	})
}

func TestAccessibility(t *testing.T) {
	did, by := "", 0
	h, _ := mount(t, func() ui.Node {
		return ui.View(ui.ViewProps{
			Label:    ui.Val("Volume"),
			Actions:  []ui.A11yAction{{Name: ui.Val("Mute"), Do: func() { did = "Mute" }}},
			OnAdjust: func(n int) { by += n },
		}, ui.StaticText("x"))
	})
	onMain(func() {
		var v *node
		for _, n := range find(h, ops.KindView) {
			if n.a11yLabel == "Volume" {
				v = n
			}
		}
		if v == nil {
			fatal(t, "no labeled view")
		}
		if s := GoString(Send(v.view, "accessibilityLabel")); s != "Volume" {
			fatalf(t, "label %q", s)
		}
		actions := ArrayItems(Send(v.view, "accessibilityCustomActions"))
		if len(actions) != 1 || GoString(Send(actions[0], "name")) != "Mute" {
			fatalf(t, "%d actions", len(actions))
		}
		// VoiceOver takes the action: its target and selector.
		Send(Send(actions[0], "target"), "a11yAction:", uintptr(actions[0]))
		if did != "Mute" {
			fatal(t, "the action didn't run")
		}
		Send(v.view, "accessibilityPerformIncrement")
		Send(v.view, "accessibilityPerformIncrement")
		Send(v.view, "accessibilityPerformDecrement")
		if by != 1 {
			fatalf(t, "adjusted by %d", by)
		}
	})
}

func TestTransitions(t *testing.T) {
	shown := signal.New(false)
	h, _ := mount(t, func() ui.Node {
		return ui.Column(ui.Style{},
			ui.Show(shown, func() ui.Node {
				return ui.View(ui.ViewProps{Style: ui.Style{Height: ui.Pt(40), Background: colors.Red, Transition: ui.TransitionSlideUp}},
					ui.StaticText("toast"),
					ui.View(ui.ViewProps{Style: ui.Style{Height: ui.Pt(20), Material: ui.MaterialThin}}))
			}))
	})
	onMain(func() {
		toggle := func(v bool) {
			h.enter(func() { ui.Post(func() { ui.Animate(func() { shown.Set(v) }) }) })
			h.runPosted()
		}
		toggle(true)
		var toast *node
		for _, n := range find(h, ops.KindView) {
			if n.transition != nil {
				toast = n
			}
		}
		if toast == nil {
			fatal(t, "no toast")
		}
		v := Retain(toast.view)
		defer Release(v)
		if toast.entering || !toast.placed {
			fatal(t, "the toast didn't come in")
		}
		// Its text is at its first frame at once: the batch's animation
		// doesn't grow it out of the zero frame a new view has, which a
		// label would ellipsize its text in.
		for _, n := range find(h, ops.KindText) {
			if r, want := MsgRect(n.view, Sel("frame")), n.frame; r.Size != want.Size {
				fatalf(t, "new text at %vx%v, want %vx%v", r.Size.Width, r.Size.Height, want.Size.Width, want.Size.Height)
			}
		}
		// And so is what a new view lays out itself, its material's view.
		for _, n := range find(h, ops.KindView) {
			if n.effect == 0 {
				continue
			}
			if r := MsgRect(n.effect, Sel("frame")); r.Size != n.frame.Size {
				fatalf(t, "new material at %vx%v, want %vx%v", r.Size.Width, r.Size.Height, n.frame.Size.Width, n.frame.Size.Height)
			}
		}
		toggle(false)
		// It's gone from the core's tree, and stays on screen while it goes.
		if h.nodes[toast.id] != nil {
			fatal(t, "the toast's node is still there")
		}
		if !h.exiting[v] || Send(v, "superview") == 0 {
			fatal(t, "the toast left at once")
		}
	})
}

func TestWindow(t *testing.T) {
	open := signal.New(false)
	closed := 0
	h, _ := mount(t, func() ui.Node {
		return ui.Window(ui.WindowProps{Open: open, Title: ui.Val("Inspector"), Width: 300, Height: 200,
			OnClose: func() { closed++ },
			Content: func() ui.Node {
				return ui.Text(signal.Func(func() string { return fmt.Sprint("width ", ui.UseEnvironment().Get().Width) }))
			}})
	})
	onMain(func() {
		h.enter(func() { ui.Post(func() { open.Set(true) }) })
		h.runPosted()
		if len(h.wins) != 1 {
			fatalf(t, "%d windows", len(h.wins))
		}
		var w *win
		for _, x := range h.wins {
			w = x
		}
		if s := GoString(Send(w.window, "title")); s != "Inspector" {
			fatalf(t, "title %q", s)
		}
		if b := MsgRect(w.content, Sel("bounds")); b.Size.Width != 300 || b.Size.Height != 200 {
			fatalf(t, "content %+v", b.Size)
		}
		// Its content lays out at the window's size.
		if s := text(firstText(w.root)); s != "width 300" {
			fatalf(t, "the content says %q", s)
		}
		// The user closes it: the core closes it.
		if SendBool(h.target, "windowShouldClose:", uintptr(w.window)) {
			fatal(t, "closed without the core")
		}
		if open.Peek() || closed != 1 || len(h.wins) != 0 {
			fatalf(t, "open %v closed %d windows %d", open.Peek(), closed, len(h.wins))
		}
	})
}

func TestMenuBar(t *testing.T) {
	saved := 0
	h, _ := mount(t, func() ui.Node {
		return ui.MenuBar(signal.Static([]ui.MenuBarMenu{
			{Title: "File", Items: []ui.MenuItem{{Title: "Save", Shortcut: ui.Cmd("s"), OnPress: func() { saved++ }}}},
			{Title: "Tools", Items: []ui.MenuItem{{Title: "Lint", Disabled: true}}},
		}))
	})
	onMain(func() {
		bar := Send(Send(Class("NSApplication"), "sharedApplication"), "mainMenu")
		find := func(menu, title string) ID {
			m := Send(Send(bar, "itemWithTitle:", uintptr(String(menu))), "submenu")
			if m == 0 {
				fatalf(t, "no %s menu", menu)
			}
			return Send(m, "itemWithTitle:", uintptr(String(title)))
		}
		save := find("File", "Save")
		if save == 0 || GoString(Send(save, "keyEquivalent")) != "s" || SendInt(save, "keyEquivalentModifierMask") != modCommand {
			fatal(t, "no Save with ⌘S in File")
		}
		if find("File", "Close Window") == 0 {
			fatal(t, "File lost Close Window")
		}
		// Clicked, as AppKit sends it: the item's OnPress runs.
		Send(Send(save, "target"), "appMenuItem:", uintptr(save))
		if saved != 1 {
			fatal(t, "Save didn't run")
		}
		lint := find("Tools", "Lint")
		if lint == 0 || h.validateMenuItem(lint) {
			fatal(t, "Lint isn't there and off")
		}
	})
}

func TestWindowOptions(t *testing.T) {
	ui.SetWindowOptions(ui.WindowOptions{Title: ui.Val("Notes"), Width: 420, Height: 500, MinWidth: 400, MinHeight: 300})
	defer ui.SetWindowOptions(ui.WindowOptions{})
	h, _ := mount(t, func() ui.Node {
		return ui.Text(signal.Func(func() string { return fmt.Sprint("width ", ui.UseEnvironment().Get().Width) }))
	})
	onMain(func() {
		// The core lays out at the size the options gave the window (unless
		// the user's is remembered).
		if s := text(find(h, ops.KindText)[0]); s != "width 420" {
			fatalf(t, "the root laid out at %q", s)
		}
		if s := GoString(Send(h.window, "title")); s != "Notes" {
			fatalf(t, "title %q", s)
		}
		if m := MsgSize(h.window, Sel("contentMinSize")); m.Width != 400 || m.Height != 300 {
			fatalf(t, "min %+v", m)
		}
	})
}

// asuka/sdk/notify needs the app's bundle: a test binary has none, and is told
// so; what needs no center (the Dock's badge, Android's channels) works.
func TestNotifyWithoutBundle(t *testing.T) {
	h, _ := mount(t, func() ui.Node { return ui.StaticText("x") })
	onMain(func() {
		call := func(m string, args []byte) ([]byte, error) {
			var out []byte
			var err error
			h.module("notify")[m](args, func(b []byte, e error) { out, err = b, e })
			return out, err
		}
		for _, m := range []string{"request", "schedule", "showing", "removeAll"} {
			if _, err := call(m, []byte(`{"title":"x"}`)); err != errNoBundle {
				fatalf(t, "notify.%s: %v", m, err)
			}
		}
		if _, err := call("badge", []byte{3, 0, 0, 0}); err != nil {
			fatalf(t, "badge: %v", err)
		}
		tile := Send(Send(Class("NSApplication"), "sharedApplication"), "dockTile")
		if got := GoString(Send(tile, "badgeLabel")); got != "3" {
			fatalf(t, "badge label %q", got)
		}
		call("badge", []byte{0, 0, 0, 0})
		if got := GoString(Send(tile, "badgeLabel")); got != "" {
			fatalf(t, "badge label %q after 0", got)
		}
		if _, err := call("channels", []byte(`[]`)); err != nil {
			fatalf(t, "channels: %v", err)
		}
	})
}

// A view with a key destroyed during an animation waits in the window's
// content view, where it was, for the view inserted with its key, which
// starts at its place and grows to its own; the old one then goes.
func TestMatch(t *testing.T) {
	big := signal.New(false)
	h, _ := mount(t, func() ui.Node {
		return ui.Column(ui.Style{Padding: ui.All(10), Align: ui.AlignStart},
			ui.Show(big, func() ui.Node {
				return ui.View(ui.ViewProps{Style: ui.Style{Width: ui.Pt(200), Height: ui.Pt(150), Background: colors.Blue, Match: "card"}})
			}, func() ui.Node {
				return ui.View(ui.ViewProps{Style: ui.Style{Width: ui.Pt(40), Height: ui.Pt(30), Background: colors.Blue, Match: "card"}})
			}))
	})
	onMain(func() {
		var small *node
		for _, n := range find(h, ops.KindView) {
			if n.match == "card" {
				small = n
			}
		}
		if small == nil {
			fatal(t, "no keyed view")
		}
		old := Retain(small.view)
		defer Release(old)
		h.enter(func() { ui.Post(func() { ui.Animate(func() { big.Set(true) }) }) })
		h.runPosted()
		content := Send(h.window, "contentView")
		if Send(old, "superview") != content || !h.exiting[old] {
			fatal(t, "the old view didn't wait in the content view")
		}
		var large *node
		for _, n := range find(h, ops.KindView) {
			if n.match == "card" {
				large = n
			}
		}
		if large == nil || len(h.match.outs) != 0 {
			fatalf(t, "not matched: %v, %d waiting", large, len(h.match.outs))
		}
		if !runUntil(func() bool { return Send(old, "superview") == 0 }) {
			fatal(t, "the old view stayed")
		}
		if f := MsgRect(large.view, Sel("frame")); f.Size.Width != 200 || f.Size.Height != 150 || f.Origin.X != 10 {
			fatalf(t, "the new view ended at %+v", f)
		}
	})
}
