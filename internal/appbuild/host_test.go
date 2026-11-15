package appbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// An installed CLI fetches its release's host once, checked against the
// hash it was built with; one that doesn't match isn't used; a CLI built
// from source has no release to fetch from.
func TestFetchHost(t *testing.T) {
	exe := []byte("a host for linux-arm64")
	sum := sha256.Sum256(exe)
	fetched := 0
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1.2.3/asuka-host-linux-arm64" {
			http.NotFound(w, r)
			return
		}
		fetched++
		w.Write(exe)
	}))
	defer srv.Close()
	t.Setenv("HOME", t.TempDir())
	t.Setenv("XDG_CACHE_HOME", t.TempDir())
	oldV, oldS, oldR := Version, hostSums, HostRelease
	defer func() { Version, hostSums, HostRelease = oldV, oldS, oldR }()
	HostRelease = srv.URL + "/v%s/%s"
	target := cbuild.Target{OS: "linux", Arch: "arm64"}

	Version, hostSums = "dev", ""
	if _, err := fetchHost(target); err == nil || !strings.Contains(err.Error(), "no prebuilt desktop host") {
		t.Fatalf("from source: %v", err)
	}
	Version, hostSums = "1.2.3", "darwin-arm64=00,linux-arm64="+hex.EncodeToString(sum[:])
	for range 2 {
		p, err := fetchHost(target)
		if err != nil {
			t.Fatal(err)
		}
		if b, _ := os.ReadFile(p); string(b) != string(exe) {
			t.Fatalf("%s: %q", p, b)
		}
	}
	if fetched != 1 {
		t.Errorf("fetched %d times", fetched)
	}
	// ASUKA_HOST names a host built elsewhere.
	out := filepath.Join(t.TempDir(), "app")
	t.Setenv("ASUKA_HOST", filepath.Dir(must(fetchHost(target))))
	if err := Host(target, out, HostOptions{}); err != nil {
		t.Fatal(err)
	}
	if st, err := os.Stat(out); err != nil || st.Mode()&0o100 == 0 {
		t.Errorf("the host: %v %v", st, err)
	}
	// A host that isn't what the release says.
	hostSums = "linux-arm64=" + strings.Repeat("0", 64)
	os.Remove(must(fetchHost2(target)))
	if _, err := fetchHost(target); err == nil || !strings.Contains(err.Error(), "SHA-256") {
		t.Errorf("a wrong hash: %v", err)
	}
}

func must(p string, err error) string {
	if err != nil {
		panic(err)
	}
	return p
}

// fetchHost2 is where fetchHost keeps target's host.
func fetchHost2(t cbuild.Target) (string, error) {
	return filepath.Join(cacheDir(), "host-"+Version, HostFile(t)), nil
}
