//go:build darwin

package mactest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// The player the notify and push tests run on: built as Player's is, from
// an app that imports asuka/notify and asuka/push, so it links asuka/host's
// Swift (HostNative), the packages' way to the host's modules. Player's own
// app imports nothing native: on it, host.available() is false and the
// packages fail with "unsupported". Its bundle ID is
// dev.asuka.mactest.notify (its user defaults: the categories kept).

const hostPlayerID = "dev.asuka.mactest.notify"

// hostPlayer builds that player once per process and returns this
// process's copy of its executable.
func hostPlayer(t testing.TB) string {
	t.Helper()
	return NativePlayer(t, "notify", `import asuka/notify
import asuka/push

app { Text("ready {push.Registration().service}{notify.Delivered().id}") }
`)
}

// unbundled is a copy of the notify player's executable outside its
// bundle: a process with no bundle identifier, as an executable run on
// its own is.
func unbundled(t testing.TB) string {
	t.Helper()
	exe := hostPlayer(t)
	dst := filepath.Join(t.TempDir(), "notify-player")
	if b, err := exec.Command("cp", exe, dst).CombinedOutput(); err != nil {
		t.Fatalf("copying the executable: %v\n%s", err, b)
	}
	return dst
}
