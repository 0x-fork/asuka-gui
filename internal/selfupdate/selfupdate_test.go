package selfupdate

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// releases is a fake GitHub: releases/latest redirects to the latest's
// page, a download to the CDN, which serves the assets.
type releases struct {
	*httptest.Server
	latest  atomic.Value // string: the tag
	assets  map[string][]byte
	latests atomic.Int32  // requests for releases/latest
	block   chan struct{} // if set, releases/latest waits on it
}

func newReleases(t *testing.T, latest string, assets map[string][]byte) *releases {
	r := &releases{assets: assets}
	r.latest.Store(latest)
	r.Server = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		switch {
		case req.URL.Path == "/releases/latest":
			r.latests.Add(1)
			if r.block != nil {
				<-r.block
			}
			http.Redirect(w, req, "/releases/tag/"+r.latest.Load().(string), http.StatusFound)
		case strings.HasPrefix(req.URL.Path, "/releases/download/"):
			http.Redirect(w, req, "/cdn/"+strings.TrimPrefix(req.URL.Path, "/releases/download/"), http.StatusFound)
		case strings.HasPrefix(req.URL.Path, "/cdn/"):
			b, ok := r.assets[strings.TrimPrefix(req.URL.Path, "/cdn/")]
			if !ok {
				http.NotFound(w, req)
				return
			}
			w.Write(b)
		default:
			http.NotFound(w, req)
		}
	}))
	t.Cleanup(r.Close)
	old := Releases
	Releases = r.URL + "/releases"
	t.Cleanup(func() { Releases = old })
	return r
}

func TestLatest(t *testing.T) {
	r := newReleases(t, "v1.4.0", nil)
	if v, err := Latest(context.Background()); err != nil || v != "1.4.0" {
		t.Fatalf("Latest = %q, %v", v, err)
	}
	r.latest.Store("../nope")
	if v, err := Latest(context.Background()); err == nil {
		t.Fatalf("Latest of a redirect elsewhere = %q", v)
	}
}

func TestNewer(t *testing.T) {
	for _, c := range []struct {
		latest, current string
		newer           bool
	}{
		{"0.3.0", "0.2.9", true},
		{"0.10.0", "0.9.1", true}, // numbers, not text
		{"0.2.9", "0.3.0", false},
		{"0.3.0", "0.3.0", false},
		{"0.3.0", "0.3.0-beta.2", true}, // a release after its prereleases
		{"0.3.0-beta.2", "0.3.0", false},
		{"0.3.0-beta.10", "0.3.0-beta.9", true},
		{"0.3.0-rc.1", "0.3.0-beta.9", true},
		{"1.0.0", "1.0.0+build.5", false},
		{"", "0.3.0", false},
	} {
		if got := Newer(c.latest, c.current); got != c.newer {
			t.Errorf("Newer(%q, %q) = %v", c.latest, c.current, got)
		}
	}
}

func TestEnabled(t *testing.T) {
	env := map[string]string{}
	getenv := func(k string) string { return env[k] }
	if !Enabled("0.3.0", getenv, true) {
		t.Fatal("a release's CLI on a terminal doesn't check")
	}
	for _, v := range []string{"dev", ""} {
		if Enabled(v, getenv, true) {
			t.Errorf("a CLI built from a checkout (%q) checks", v)
		}
	}
	if Enabled("0.3.0", getenv, false) {
		t.Error("it checks when stderr isn't a terminal")
	}
	env["CI"] = "true"
	if Enabled("0.3.0", getenv, true) {
		t.Error("it checks in CI")
	}
	delete(env, "CI")
	env["ASUKA_NO_UPDATE_CHECK"] = "1"
	if Enabled("0.3.0", getenv, true) {
		t.Error("ASUKA_NO_UPDATE_CHECK doesn't stop it")
	}
}

func TestOnceADay(t *testing.T) {
	r := newReleases(t, "v0.3.0", nil)
	file := filepath.Join(t.TempDir(), "update-check.json")
	t0 := time.Now()

	// Due once, then not again at once: a run at the same time starts none.
	if !Due(file, t0) {
		t.Fatal("the first run's check isn't due")
	}
	if Due(file, t0) {
		t.Fatal("a second check started at once")
	}
	// Its command ended before it was in: nothing to say yet.
	if n := Notice(file, "0.2.6", t0); n != "" {
		t.Fatalf("Notice before a check = %q", n)
	}
	if err := Check(file, t0); err != nil {
		t.Fatal(err)
	}
	if r.latests.Load() != 1 {
		t.Fatalf("%d checks", r.latests.Load())
	}
	// The next command's end says it, once.
	if n := Notice(file, "0.2.6", t0.Add(time.Minute)); !strings.Contains(n, "0.3.0") || !strings.Contains(n, "0.2.6") || !strings.Contains(n, "asuka upgrade") {
		t.Fatalf("Notice = %q", n)
	}
	if n := Notice(file, "0.2.6", t0.Add(2*time.Minute)); n != "" {
		t.Fatalf("said again at once: %q", n)
	}
	// Within the day: no check.
	if Due(file, t0.Add(5*time.Hour)) {
		t.Fatal("a check due again within the day")
	}
	// A day on: due, said again.
	t1 := t0.Add(Every + time.Hour)
	if !Due(file, t1) {
		t.Fatal("no check the next day")
	}
	r.latest.Store("v0.3.1")
	if err := Check(file, t1); err != nil {
		t.Fatal(err)
	}
	if n := Notice(file, "0.2.6", t1); !strings.Contains(n, "0.3.1") {
		t.Fatalf("Notice the next day = %q", n)
	}
	// A CLI as new as the latest says nothing.
	if n := Notice(file, "0.3.1", t1.Add(Every+time.Hour)); n != "" {
		t.Fatalf("Notice on the latest = %q", n)
	}
}

func TestCheckFailing(t *testing.T) {
	// Offline: a check that fails keeps what the last one found, and is
	// started again an hour on, not a day.
	r := newReleases(t, "v0.3.0", nil)
	file := filepath.Join(t.TempDir(), "update-check.json")
	t0 := time.Now().Add(-48 * time.Hour)
	if !Due(file, t0) {
		t.Fatal("not due")
	}
	if err := Check(file, t0); err != nil {
		t.Fatal(err)
	}
	r.Close()
	t1 := time.Now()
	if !Due(file, t1) {
		t.Fatal("not due two days on")
	}
	if err := Check(file, t1); err == nil {
		t.Fatal("a check with GitHub down passed")
	}
	if Due(file, t1.Add(30*time.Minute)) {
		t.Fatal("a failed check started again within the hour")
	}
	if !Due(file, t1.Add(61*time.Minute)) {
		t.Fatal("a failed check not started again after an hour")
	}
	if n := Notice(file, "0.2.6", t1); !strings.Contains(n, "0.3.0") {
		t.Fatalf("Notice from the last check's answer = %q", n)
	}
}

func TestAsset(t *testing.T) {
	for _, c := range []struct{ goos, goarch, want string }{
		{"darwin", "arm64", "asuka-darwin-arm64"},
		{"linux", "amd64", "asuka-linux-x64"},
		{"linux", "arm64", "asuka-linux-arm64"},
		{"windows", "amd64", "asuka-windows-x64.exe"},
		{"windows", "arm64", "asuka-windows-arm64.exe"},
	} {
		if got, err := Asset(c.goos, c.goarch); err != nil || got != c.want {
			t.Errorf("Asset(%s, %s) = %s, %v", c.goos, c.goarch, got, err)
		}
	}
	for _, c := range [][2]string{{"darwin", "amd64"}, {"freebsd", "amd64"}, {"linux", "386"}} {
		if _, err := Asset(c[0], c[1]); err == nil {
			t.Errorf("Asset(%s, %s) has a release", c[0], c[1])
		}
	}
}

func sums(files map[string][]byte) []byte {
	var b strings.Builder
	for name, data := range files {
		s := sha256.Sum256(data)
		fmt.Fprintf(&b, "%s  %s\n", hex.EncodeToString(s[:]), name)
	}
	return []byte(b.String())
}

func TestUpgrade(t *testing.T) {
	for _, goos := range []string{"linux", "windows"} {
		t.Run(goos, func(t *testing.T) {
			asset := "asuka-linux-x64"
			if goos == "windows" {
				asset = "asuka-windows-x64.exe"
			}
			newExe := []byte("the new asuka")
			newReleases(t, "v0.3.0", map[string][]byte{
				"v0.3.0/" + asset:   newExe,
				"v0.3.0/SHA256SUMS": sums(map[string][]byte{asset: newExe, "asuka-other": []byte("x")}),
				"v0.3.1/" + asset:   []byte("not what SHA256SUMS says"),
				"v0.3.1/SHA256SUMS": sums(map[string][]byte{asset: newExe}),
				"v0.3.2/SHA256SUMS": sums(map[string][]byte{"asuka-other": newExe}),
				"v0.3.3/" + asset:   newExe,
				"v0.3.3/SHA256SUMS": sums(map[string][]byte{asset: newExe}),
			})
			dir := t.TempDir()
			exe := filepath.Join(dir, "asuka")
			os.WriteFile(exe, []byte("the old asuka"), 0o755)
			ctx := context.Background()
			checked := ""
			check := func(p string) error {
				checked = p
				return nil
			}
			left := func() []string {
				es, _ := os.ReadDir(dir)
				var names []string
				for _, e := range es {
					names = append(names, e.Name())
				}
				return names
			}
			unchanged := func() {
				t.Helper()
				if b, _ := os.ReadFile(exe); string(b) != "the old asuka" {
					t.Fatalf("the executable was replaced: %q", b)
				}
				if l := left(); len(l) != 1 {
					t.Fatalf("left beside it: %v", l)
				}
			}

			// A download that isn't what SHA256SUMS says isn't installed.
			if err := upgrade(ctx, exe, "0.3.1", asset, goos, check); err == nil || !strings.Contains(err.Error(), "SHA256SUMS") {
				t.Fatalf("a download with another checksum: %v", err)
			}
			unchanged()
			// Nor one SHA256SUMS doesn't name.
			if err := upgrade(ctx, exe, "0.3.2", asset, goos, check); err == nil || !strings.Contains(err.Error(), "no "+asset) {
				t.Fatalf("an asset SHA256SUMS hasn't: %v", err)
			}
			unchanged()
			// Nor one that doesn't run here.
			if err := upgrade(ctx, exe, "0.3.3", asset, goos, func(string) error { return errors.New("exec format error") }); err == nil || !strings.Contains(err.Error(), "doesn't run") {
				t.Fatalf("a download that doesn't run: %v", err)
			}
			unchanged()
			// Nor a release that isn't there.
			if err := upgrade(ctx, exe, "9.9.9", asset, goos, check); err == nil || !strings.Contains(err.Error(), "404") {
				t.Fatalf("a missing release: %v", err)
			}
			unchanged()

			if err := upgrade(ctx, exe, "0.3.0", asset, goos, check); err != nil {
				t.Fatal(err)
			}
			if b, _ := os.ReadFile(exe); string(b) != string(newExe) {
				t.Fatalf("the executable is %q", b)
			}
			if checked == "" || filepath.Dir(checked) != dir {
				t.Fatalf("checked %q, not the download beside the executable", checked)
			}
			if st, _ := os.Stat(exe); st.Mode()&0o100 == 0 && goos != "windows" {
				t.Fatalf("the executable's mode is %v", st.Mode())
			}
			// Nothing left beside it (on Windows the old one is removed when
			// it isn't running, as here).
			if l := left(); len(l) != 1 {
				t.Fatalf("left beside it: %v", l)
			}
		})
	}
}

func TestReplaceRunningOnWindows(t *testing.T) {
	// A running .exe can't be removed on Windows: replace moves it aside,
	// and RemoveOld removes it once it doesn't run. Here: an .old the
	// last upgrade left is replaced by this one's.
	dir := t.TempDir()
	exe := filepath.Join(dir, "asuka.exe")
	os.WriteFile(exe, []byte("v2"), 0o755)
	os.WriteFile(exe+".old", []byte("v1"), 0o755)
	tmp := filepath.Join(dir, ".asuka-upgrade-1")
	os.WriteFile(tmp, []byte("v3"), 0o755)
	if err := replace(tmp, exe, "windows"); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(exe); string(b) != "v3" {
		t.Fatalf("the executable is %q", b)
	}
	if _, err := os.Stat(tmp); err == nil {
		t.Fatal("the download is still beside it")
	}
}
