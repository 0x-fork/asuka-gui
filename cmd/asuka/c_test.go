package main

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
)

// testdata/capp calls its own C and the system's zlib (asuka.toml's
// [c]): asuka test builds them into asuka-run, and its UI test taps
// what calls C, which calls back, from another thread too.
func TestCAppTests(t *testing.T) {
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
	if err := cmdTest([]string{filepath.Join("testdata", "capp")}); err != nil {
		t.Fatal(err)
	}
}

// Each target reads its [c.<platform>]: zlib where the platform has it,
// the app's own CRC on the web and Windows; frameworks only on Apple's.
func TestCAppForeign(t *testing.T) {
	p, err := appbuild.Load(filepath.Join("testdata", "capp"))
	if err != nil {
		t.Fatal(err)
	}
	m, diags, err := p.Compile()
	if err != nil || len(diags) > 0 {
		t.Fatal(err, diags)
	}
	if m.C == nil || !strings.Contains(string(m.C.Glue), `"ffi.main.checksum_lines"`) {
		t.Fatalf("no glue: %+v", m.C)
	}
	for _, c := range []struct {
		t      cbuild.Target
		link   []string
		define string
	}{
		{cbuild.Target{OS: "ios-simulator", Arch: "arm64"}, []string{"-lz"}, "-DCHECKSUM_SEED=7"},
		{cbuild.Target{OS: "android", Arch: "arm64"}, []string{"-lz"}, "-DCHECKSUM_SEED=7"},
		{cbuild.Target{OS: "wasm", Arch: "wasm32"}, nil, "-DCHECKSUM_OWN_CRC"},
		{cbuild.Target{OS: "windows", Arch: "amd64"}, nil, "-DCHECKSUM_OWN_CRC"},
	} {
		f, err := appbuild.Foreign(m.C, c.t)
		if err != nil {
			t.Fatal(err)
		}
		if !slices.Equal(f.Link, c.link) {
			t.Errorf("%s: links %q, want %q", c.t, f.Link, c.link)
		}
		u := f.Units[0]
		if len(u.Sources) != 1 || filepath.Base(u.Sources[0]) != "checksum.c" || !slices.Contains(u.Flags, c.define) {
			t.Errorf("%s: unit %+v", c.t, u)
		}
		if _, err := os.Stat(f.Glue); err != nil {
			t.Error(err)
		}
	}
}
