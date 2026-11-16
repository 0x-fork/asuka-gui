//go:build darwin

package mactest

import (
	"fmt"
	"math"
	"strings"
	"testing"
	"time"
)

// The visual part's tests' helpers: the apps log what they're told into a
// Text that starts with "log:", and the tests read it back.

// viewSized is the View node of a size.
func viewSized(t *testing.T, tr *Tree, w, h float64) *Node {
	t.Helper()
	for _, n := range tr.Kind("View") {
		if len(n.Frame) == 4 && n.Frame[2] == w && n.Frame[3] == h {
			return n
		}
	}
	t.Fatalf("no %vx%v view:\n%s", w, h, tr.Raw())
	return nil
}

// kindSized is the node of a kind and a size.
func kindSized(t *testing.T, tr *Tree, kind string, w, h float64) *Node {
	t.Helper()
	for _, n := range tr.Kind(kind) {
		if len(n.Frame) == 4 && n.Frame[2] == w && n.Frame[3] == h {
			return n
		}
	}
	t.Fatalf("no %vx%v %s:\n%s", w, h, kind, tr.Raw())
	return nil
}

// logOf is what the app's log Text shows, without its "log:".
func vlogOf(tr *Tree) string {
	for _, n := range tr.Kind("Text") {
		if strings.HasPrefix(n.Text, "log:") {
			return strings.TrimPrefix(n.Text, "log:")
		}
	}
	return ""
}

// vlogger reads an app's log, each read what was logged since the last.
type vlogger struct {
	a    *App
	seen string
}

// next is what the app logged since the last read, as its entries.
func (l *vlogger) next() []string {
	s := vlogOf(l.a.Tree())
	if !strings.HasPrefix(s, l.seen) {
		l.a.t.Fatalf("the log went back: %q, was %q", s, l.seen)
	}
	d := strings.TrimPrefix(s, l.seen)
	l.seen = s
	d = strings.TrimSuffix(d, ";")
	if d == "" {
		return nil
	}
	return strings.Split(d, ";")
}

// expect checks the entries logged since the last read.
func (l *vlogger) expect(t *testing.T, what string, want ...string) {
	t.Helper()
	if got := l.next(); strings.Join(got, ";") != strings.Join(want, ";") {
		t.Errorf("%s: %q, want %q", what, got, want)
	}
}

// until waits for the log to hold f, then takes what it holds as read.
func (l *vlogger) until(t *testing.T, f func(string) bool) string {
	t.Helper()
	var s string
	l.a.Until(func(tr *Tree) bool { s = vlogOf(tr); return f(s) })
	l.seen = s
	return s
}

// obj is a JSON object field of n (nil if none).
func vobj(n *Node, key string) map[string]any {
	m, _ := n.Props[key].(map[string]any)
	return m
}

// num is a number field of a JSON object.
func vnum(m map[string]any, key string) float64 {
	f, _ := m[key].(float64)
	return f
}

// nums is a number array field of a JSON object.
func vnums(m map[string]any, key string) []float64 {
	a, _ := m[key].([]any)
	out := make([]float64, 0, len(a))
	for _, x := range a {
		f, _ := x.(float64)
		out = append(out, f)
	}
	return out
}

func vbool(m map[string]any, key string) bool {
	b, _ := m[key].(bool)
	return b
}

func vstr(m map[string]any, key string) string {
	s, _ := m[key].(string)
	return s
}

func vstrs(m map[string]any, key string) []string {
	a, _ := m[key].([]any)
	var out []string
	for _, x := range a {
		s, _ := x.(string)
		out = append(out, s)
	}
	return out
}

// visualState is the visual part's state, in the root's JSON.
func visualState(tr *Tree) map[string]any {
	if len(tr.Roots) == 0 {
		return nil
	}
	return vobj(tr.Roots[0], "visual")
}

func vnear(a, b, eps float64) bool { return math.Abs(a-b) <= eps }

// id is a node's id, as the steps take it.
func vid(n *Node) string { return fmt.Sprint(n.ID) }

// eventually polls f (reading the tree) for up to d.
func veventually(a *App, d time.Duration, f func(*Tree) bool) bool {
	deadline := time.Now().Add(d)
	for time.Now().Before(deadline) {
		if f(a.Tree()) {
			return true
		}
		time.Sleep(50 * time.Millisecond)
	}
	return false
}

// waitShort is how long a check waits for what follows a step.
const waitShort = 5 * time.Second
