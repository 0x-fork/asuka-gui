//go:build windows

package windows

import (
	"testing"

	"github.com/egoist/asuka/host/hosttest"
	. "github.com/egoist/asuka/internal/windows"
)

// mountApp runs an Asuka app (src) on a new host, on the C core as an
// app runs (host/hosttest); its windows go when the test ends.
func mountApp(t *testing.T, src string) *Host {
	t.Helper()
	var h *Host
	onMain(t, func() { h = newHost() })
	t.Cleanup(func() {
		onMain(t, func() {
			// What the host deferred (WebView2 controllers it closes on a
			// later turn) runs before its windows go.
			h.runPosted()
			DestroyWindow(h.window)
			DestroyWindow(h.park)
			DestroyWindow(h.msgWin)
		})
	})
	hosttest.Mount(t, h, func(f func()) { onMain(t, func() { h.enter(f) }) }, src)
	return h
}
