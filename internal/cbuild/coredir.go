package cbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"sync"

	"github.com/egoist/asuka/core"
)

var (
	coreOnce sync.Once
	coreDir  string
	coreErr  error
)

// CoreDir is the directory of the C core's sources to build from:
// ASUKA_CORE if set; the repository's core/ when the CLI runs from a
// checkout (go run, go test); else the sources embedded in the CLI,
// written once into the user's cache under their hash.
func CoreDir() (string, error) {
	coreOnce.Do(func() { coreDir, coreErr = findCore() })
	return coreDir, coreErr
}

func findCore() (string, error) {
	if d := os.Getenv("ASUKA_CORE"); d != "" {
		return filepath.Abs(d)
	}
	if d := repoCore(); d != "" {
		return d, nil
	}
	return extract()
}

// repoCore finds core/ above the working directory or the executable,
// when one is a checkout of the repository.
func repoCore() string {
	starts := []string{}
	if wd, err := os.Getwd(); err == nil {
		starts = append(starts, wd)
	}
	if exe, err := os.Executable(); err == nil {
		starts = append(starts, filepath.Dir(exe))
	}
	for _, s := range starts {
		for d := s; ; d = filepath.Dir(d) {
			if isCore(filepath.Join(d, "core")) {
				return filepath.Join(d, "core")
			}
			if filepath.Dir(d) == d {
				break
			}
		}
	}
	return ""
}

func isCore(d string) bool {
	_, err1 := os.Stat(filepath.Join(d, "include", "asuka.h"))
	_, err2 := os.Stat(filepath.Join(d, "src", "vm", "vm.h"))
	return err1 == nil && err2 == nil
}

func extract() (string, error) {
	var files []string
	fs.WalkDir(core.Sources, ".", func(p string, e fs.DirEntry, err error) error {
		if err == nil && !e.IsDir() {
			files = append(files, p)
		}
		return nil
	})
	sort.Strings(files)
	h := sha256.New()
	for _, f := range files {
		b, _ := core.Sources.ReadFile(f)
		h.Write([]byte(f))
		h.Write(b)
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	dir := filepath.Join(cache, "asuka", "core-src-"+hex.EncodeToString(h.Sum(nil))[:16])
	if isCore(dir) {
		return dir, nil
	}
	tmp := dir + ".tmp"
	os.RemoveAll(tmp)
	for _, f := range files {
		b, _ := core.Sources.ReadFile(f)
		p := filepath.Join(tmp, filepath.FromSlash(f))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return "", err
		}
		if err := os.WriteFile(p, b, 0o644); err != nil {
			return "", err
		}
	}
	if err := os.Rename(tmp, dir); err != nil && !isCore(dir) {
		return "", err
	}
	return dir, nil
}
