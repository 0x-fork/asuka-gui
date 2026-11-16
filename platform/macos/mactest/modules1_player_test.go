//go:build darwin

package mactest

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"syscall"
	"testing"
	"time"
)

// The player the notify and push tests run on: built as Player's is, from
// an app that imports asuka/notify and asuka/push, so it links asuka/host's
// Swift (HostNative), the packages' way to the host's modules. Player's own
// app imports nothing native: on it, host.available() is false and the
// packages fail with "unsupported". Its bundle ID is
// dev.asuka.mactest.notify (its user defaults: the categories kept).

const hostPlayerID = "dev.asuka.mactest.notify"

var (
	hostOnce      sync.Once
	hostPlayerExe string
	hostPlayerErr error
)

// hostPlayer builds that player once per process (after Player's, whose
// CLI it uses) and returns this process's copy of its executable.
func hostPlayer(t testing.TB) string {
	t.Helper()
	Player(t)
	hostOnce.Do(func() { hostPlayerExe, hostPlayerErr = buildHostPlayer() })
	if hostPlayerErr != nil {
		t.Fatalf("building the notify player: %v", hostPlayerErr)
	}
	return hostPlayerExe
}

func buildHostPlayer() (string, error) {
	base, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}
	dir := filepath.Join(base, "asuka", "mactest")
	lock, err := os.OpenFile(filepath.Join(dir, ".lock-notify"), os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		return "", err
	}
	defer lock.Close()
	if err := syscall.Flock(int(lock.Fd()), syscall.LOCK_EX); err != nil {
		return "", err
	}
	app := filepath.Join(dir, "mactest-notify") // its work directory: <cache>/asuka/mactest-notify-<hash>/mac
	if err := os.MkdirAll(app, 0o755); err != nil {
		return "", err
	}
	os.WriteFile(filepath.Join(app, "asuka.toml"), []byte("name = \"Mac Test Notify\"\nid = \""+hostPlayerID+"\"\nversion = \"1.0.0\"\n"), 0o644)
	os.WriteFile(filepath.Join(app, "app.asuka"), []byte(`import asuka/notify
import asuka/push

app { Text("ready {push.Registration().service}{notify.Delivered().id}") }
`), 0o644)
	snap := filepath.Join(dir, "notify-ready.png")
	os.Remove(snap)
	started := time.Now()
	c := exec.Command(filepath.Join(dir, "asuka"), "dev", "--platform", "mac", "--once", app)
	c.Env = append(os.Environ(), "ASUKA_SNAPSHOT="+snap)
	var out bytes.Buffer
	c.Stdout, c.Stderr = &out, &out
	if err := c.Start(); err != nil {
		return "", err
	}
	done := make(chan error, 1)
	go func() { done <- c.Wait() }()
	exe := ""
	deadline := time.Now().Add(15 * time.Minute)
	for time.Now().Before(deadline) {
		if exe == "" {
			ms, _ := filepath.Glob(filepath.Join(base, "asuka", "mactest-notify-*", "mac", "player", "Mac Test Notify.app", "Contents", "MacOS", "Mac Test Notify"))
			for _, m := range ms {
				if st, err := os.Stat(m); err == nil && !st.ModTime().Before(started) {
					exe = m
				}
			}
		}
		select {
		case err := <-done:
			if exe == "" {
				return "", fmt.Errorf("asuka dev: %v\n%s", err, out.String())
			}
			return ownCopy(exe)
		case <-time.After(200 * time.Millisecond):
		}
		if exe != "" {
			if _, err := os.Stat(snap); err == nil {
				c.Process.Signal(os.Interrupt)
				<-done
				return ownCopy(exe)
			}
		}
	}
	c.Process.Kill()
	return "", fmt.Errorf("asuka dev didn't start the notify player:\n%s", out.String())
}

// ownCopy copies exe's app bundle for this process, which runs its copy
// while another process's build may replace the built one.
func ownCopy(exe string) (string, error) {
	app := filepath.Dir(filepath.Dir(filepath.Dir(exe)))
	own, err := os.MkdirTemp("", "asuka-mactest-notify-")
	if err != nil {
		return "", err
	}
	cp := filepath.Join(own, filepath.Base(app))
	if b, err := exec.Command("ditto", app, cp).CombinedOutput(); err != nil {
		return "", fmt.Errorf("copying the player: %v\n%s", err, b)
	}
	return filepath.Join(cp, "Contents", "MacOS", filepath.Base(exe)), nil
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
