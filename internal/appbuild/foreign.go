package appbuild

import (
	"fmt"
	"path/filepath"
	"strings"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/lang/bytecode"
)

// CPlatform is the [c.<platform>] table a target reads (pkgs.CPlatforms).
func CPlatform(t cbuild.Target) string {
	switch t.OS {
	case "ios", "ios-simulator":
		return "ios"
	case "darwin":
		return "macos"
	case "wasm":
		return "web"
	}
	return t.OS // android, windows, linux
}

// Foreign is the C c (a module's: its externs') calls, for target t: its
// glue, and each project's asuka.toml [c] for t's platform, its sources
// a unit and its libraries and frameworks the link's. Nil for a nil c.
func Foreign(c *bytecode.C, t cbuild.Target) (*cbuild.Foreign, error) {
	if c == nil {
		return nil, nil
	}
	glue, err := cbuild.CacheFile("ffi", "ffi.c", c.Glue)
	if err != nil {
		return nil, err
	}
	f := &cbuild.Foreign{Glue: glue}
	platform := CPlatform(t)
	apple := t.OS == "ios" || t.OS == "ios-simulator" || t.OS == "darwin"
	seen := map[string]bool{}
	link := func(args ...string) {
		k := strings.Join(args, " ")
		if !seen[k] {
			seen[k] = true
			f.Link = append(f.Link, args...)
		}
	}
	for _, dir := range c.Dirs {
		cfg, err := pkgs.ReadC(dir)
		if err != nil {
			return nil, err
		}
		s := cfg.For(platform)
		srcs, err := s.CSources(dir)
		if err != nil {
			return nil, fmt.Errorf("%s: %v", filepath.Join(dir, pkgs.ManifestFile), err)
		}
		u := cbuild.CUnit{Name: dir, Sources: srcs}
		for _, inc := range s.Include {
			abs := filepath.Join(dir, filepath.FromSlash(inc))
			u.Flags = append(u.Flags, "-I"+abs)
			u.Headers = append(u.Headers, abs)
		}
		for _, d := range s.Defines {
			u.Flags = append(u.Flags, "-D"+d)
		}
		u.Flags = append(u.Flags, s.Flags...)
		f.Units = append(f.Units, u)
		for _, l := range s.Libraries {
			if strings.ContainsAny(l, `/\`) {
				link(filepath.Join(dir, filepath.FromSlash(l)))
			} else {
				link("-l" + l)
			}
		}
		if apple {
			for _, fw := range s.Frameworks {
				link("-framework", fw)
			}
		}
	}
	return f, nil
}
