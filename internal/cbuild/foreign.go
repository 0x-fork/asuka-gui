package cbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"hash"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// Foreign is the C an app calls through its extern declarations (RFC
// 0008), built into the core's build: the glue lang/cffi generated,
// compiled as the core is (its flags, -DAK_FFI, warnings as errors), and
// the app's and its packages' own sources (asuka.toml's [c]), each unit
// with its own flags and its own warnings.
type Foreign struct {
	Glue  string // the glue's .c
	Units []CUnit
	// Link are what a link of the build adds (-lsqlite3, -framework
	// Security, -L…). A static library has none of its own: the
	// executable it's linked into adds them (iOS's app).
	Link []string
}

// CUnit is a project's C sources and how they're compiled: its include
// directories and macros as flags (-I, -D), and any more of its own.
type CUnit struct {
	Name    string // what it's from, for messages and the cache (the project's directory)
	Sources []string
	Flags   []string
	// Headers are the directories whose .h files the sources may
	// include: a change to one compiles them again.
	Headers []string
}

// hash writes what the units compile from: their flags, sources and
// headers.
func (f *Foreign) hash(h hash.Hash) error {
	fmt.Fprintln(h, "ffi", f.Link)
	for _, u := range f.Units {
		fmt.Fprintln(h, u.Name, u.Flags)
		files := append([]string{}, u.Sources...)
		files = append(files, unitHeaders(u)...)
		for _, p := range files {
			b, err := os.ReadFile(p)
			if err != nil {
				return err
			}
			fmt.Fprintln(h, p, len(b))
			h.Write(b)
		}
	}
	return nil
}

// unitHeaders are the .h files under u's header directories and its
// sources' directories.
func unitHeaders(u CUnit) []string {
	dirs := append([]string{}, u.Headers...)
	for _, s := range u.Sources {
		dirs = append(dirs, filepath.Dir(s))
	}
	sort.Strings(dirs)
	seen := map[string]bool{}
	var out []string
	for _, d := range dirs {
		if seen[d] {
			continue
		}
		seen[d] = true
		filepath.WalkDir(d, func(p string, e fs.DirEntry, err error) error {
			if err != nil {
				return nil
			}
			if e.IsDir() && p != d && (strings.HasPrefix(e.Name(), ".") || e.Name() == "node_modules" || e.Name() == "build" || e.Name() == "dist") {
				return filepath.SkipDir
			}
			if !e.IsDir() && (strings.HasSuffix(p, ".h") || strings.HasSuffix(p, ".inc")) && !seen[p] {
				seen[p] = true
				out = append(out, p)
			}
			return nil
		})
	}
	sort.Strings(out)
	return out
}

// compileUnits compiles the units' sources into dir (a cache: an
// object already there is kept), each with base, then its own flags.
func (f *Foreign) compileUnits(cc Compiler, dir string, base []string) ([]string, error) {
	var objs []string
	for i, u := range f.Units {
		if len(u.Sources) == 0 {
			continue
		}
		flags := append(append([]string{}, base...), u.Flags...)
		o, err := cc.CompileWith(u.Sources, filepath.Join(dir, fmt.Sprint(i)), flags)
		if err != nil {
			return nil, fmt.Errorf("the C of %s: %v", u.Name, err)
		}
		objs = append(objs, o...)
	}
	return objs, nil
}

// CacheFile writes src under the user's cache at a path of its content's
// (what+hash/name), once, and returns the path: generated C (the glue)
// whose objects the build caches by path and content.
func CacheFile(what, name string, src []byte) (string, error) {
	sum := sha256.Sum256(src)
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	dir := filepath.Join(cache, "asuka", what+"-"+hex.EncodeToString(sum[:])[:16])
	p := filepath.Join(dir, name)
	if _, err := os.Stat(p); err == nil {
		return p, nil
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	tmp := fmt.Sprintf("%s.%d.tmp", p, os.Getpid())
	if err := os.WriteFile(tmp, src, 0o644); err != nil {
		return "", err
	}
	return p, os.Rename(tmp, p)
}
