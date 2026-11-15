// Package srcdir finds a tree of sources the asuka CLI carries (the C
// core's, the standard library's): a directory an environment variable
// names; the repository's own, when the CLI runs from a checkout (go run,
// go test); else the copy embedded in the CLI, written once into the
// user's cache under its hash.
package srcdir

import (
	"crypto/sha256"
	"encoding/hex"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
)

// Tree describes one.
type Tree struct {
	Env   string            // the variable that names its directory (ASUKA_CORE)
	Sub   string            // its directory in the repository (core)
	Valid func(string) bool // whether a directory is one
	Files fs.FS             // the embedded copy
	Cache string            // the cache directory's prefix (core-src)
}

// Dir finds t's directory.
func (t Tree) Dir() (string, error) {
	if d := os.Getenv(t.Env); d != "" {
		return filepath.Abs(d)
	}
	if d := t.repo(); d != "" {
		return d, nil
	}
	return t.Extract()
}

// repo finds Sub above the working directory or the executable.
func (t Tree) repo() string {
	var starts []string
	if wd, err := os.Getwd(); err == nil {
		starts = append(starts, wd)
	}
	if exe, err := os.Executable(); err == nil {
		starts = append(starts, filepath.Dir(exe))
	}
	for _, s := range starts {
		for d := s; ; d = filepath.Dir(d) {
			if c := filepath.Join(d, t.Sub); t.Valid(c) {
				return c
			}
			if filepath.Dir(d) == d {
				break
			}
		}
	}
	return ""
}

// Extract writes the embedded copy into the user's cache (once: it's kept
// under its hash) and returns its directory.
func (t Tree) Extract() (string, error) {
	var files []string
	fs.WalkDir(t.Files, ".", func(p string, e fs.DirEntry, err error) error {
		if err == nil && !e.IsDir() {
			files = append(files, p)
		}
		return nil
	})
	sort.Strings(files)
	h := sha256.New()
	for _, f := range files {
		b, _ := fs.ReadFile(t.Files, f)
		h.Write([]byte(f))
		h.Write(b)
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	dir := filepath.Join(cache, "asuka", t.Cache+"-"+hex.EncodeToString(h.Sum(nil))[:16])
	if t.Valid(dir) {
		return dir, nil
	}
	tmp := dir + ".tmp"
	os.RemoveAll(tmp)
	for _, f := range files {
		b, _ := fs.ReadFile(t.Files, f)
		p := filepath.Join(tmp, filepath.FromSlash(f))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return "", err
		}
		if err := os.WriteFile(p, b, 0o644); err != nil {
			return "", err
		}
	}
	// Another process may have written it first: theirs is as good.
	if err := os.Rename(tmp, dir); err != nil {
		os.RemoveAll(tmp)
		if !t.Valid(dir) {
			return "", err
		}
	}
	return dir, nil
}
