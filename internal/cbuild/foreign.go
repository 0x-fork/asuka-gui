package cbuild

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"

	"github.com/egoist/asuka/internal/ccache"
)

// Foreign is the C an app calls through its extern declarations (RFC
// 0008), built into the core's build: the glue lang/cffi generated,
// compiled as the core is (its flags, -DAK_FFI, warnings as errors), and
// the app's and its packages' own sources (asuka.toml's [c]), each unit
// with its own flags and its own warnings.
type Foreign struct {
	Glue  string // the glue's .c ("": the app has no externs)
	Units []CUnit
	// Libs are the C of the std and sdk packages the app imports (StdC:
	// std/sqlite's SQLite), libraries (prebuilt, or built into the cache)
	// a link takes, which core/link names.
	Libs []Unit
	// Link are what a link of the build adds (-lsqlite3, -framework
	// Security, -L…). A static library has none of its own: the
	// executable it's linked into adds them (iOS's app).
	Link []string
}

// CUnit is a project's C sources and how they're compiled: its include
// directories and macros as flags (-I, -D), and any more of its own.
type CUnit struct {
	Name    string // what it's from, for messages (the project's directory)
	Sources []string
	Flags   []string
	// Headers are the directories of the headers the sources include
	// (their -I): what a compile includes of them is its depfile's.
	Headers []string
}

// glue reports whether f has externs' glue.
func (f *Foreign) glue() bool { return f != nil && f.Glue != "" }

// libs are f's Libs (none for a nil f).
func (f *Foreign) libs() []Unit {
	if f == nil {
		return nil
	}
	return f.Libs
}

// objects compiles f's units' sources into the object cache, each with
// base, then its own flags, and returns the objects.
func (f *Foreign) objects(cc Compiler, base []string) ([]string, error) {
	var objs []string
	for _, u := range f.Units {
		if len(u.Sources) == 0 {
			continue
		}
		flags := append(append([]string{}, base...), u.Flags...)
		o, err := cc.CompileCached(u.Sources, flags)
		if err != nil {
			return nil, fmt.Errorf("the C of %s: %v", u.Name, err)
		}
		objs = append(objs, Paths(o)...)
	}
	return objs, nil
}

// CacheFile writes src into the build cache at a path of its content's
// (c/s/<hash>/name), once, and returns the path: generated C (an app's,
// its glue) the object cache compiles from there. what names it in the
// hash (ffi, app).
func CacheFile(what, name string, src []byte) (string, error) {
	sum := sha256.Sum256(append([]byte(what+"\x00"), src...))
	p := filepath.Join(ccache.Sharded("c", "s", hex.EncodeToString(sum[:]), ""), name)
	if _, err := os.Stat(p); err == nil {
		ccache.Touch(filepath.Dir(p))
		return p, nil
	}
	return p, ccache.WriteFile(p, src)
}
