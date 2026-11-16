package ccache

import (
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"time"

	"github.com/egoist/asuka/internal/cachedir"
)

// Recent is how long an entry is kept after its last use whatever the
// cap: another build (another terminal's, another checkout's) may be
// linking it this moment.
var Recent = time.Hour

// trimEvery is how often MaybeTrim measures the cache.
var trimEvery = time.Hour

// stores are the cache's content-addressed stores and the depth of their
// entries: c/<o|m|a|x>/<xx>/<entry>, swift/<m>/<xx>/<entry>, and a
// directory per package in prebuilt, pkg and plugins. Everything else at
// the cache's top (an app's work directory, a host's shared build, an
// extracted source tree, an older CLI's build directories) is one entry.
var stores = map[string]int{"c": 3, "swift": 3, "prebuilt": 1, "pkg": 1, "plugins": 1}

// Entry is what eviction takes or keeps whole.
type Entry struct {
	Path string
	Size int64
	Used time.Time
}

// Entries are the cache's entries.
func Entries() []Entry {
	root := cachedir.Root()
	tops, err := os.ReadDir(root)
	if err != nil {
		return nil
	}
	var out []Entry
	for _, t := range tops {
		p := filepath.Join(root, t.Name())
		if !t.IsDir() {
			continue // stats.json, the lock, the trim stamp: the cache's own
		}
		if depth, ok := stores[t.Name()]; ok {
			out = append(out, entriesAt(p, depth)...)
			continue
		}
		out = append(out, entry(p))
	}
	return out
}

func entriesAt(dir string, depth int) []Entry {
	es, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var out []Entry
	for _, e := range es {
		p := filepath.Join(dir, e.Name())
		if depth > 1 && e.IsDir() {
			out = append(out, entriesAt(p, depth-1)...)
			continue
		}
		out = append(out, entry(p))
	}
	return out
}

func entry(p string) Entry {
	e := Entry{Path: p}
	st, err := os.Lstat(p)
	if err != nil {
		return e
	}
	e.Used = st.ModTime()
	if !st.IsDir() {
		e.Size = st.Size()
		return e
	}
	filepath.WalkDir(p, func(q string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() {
			if info, err := d.Info(); err == nil {
				e.Size += info.Size()
			}
		}
		return nil
	})
	return e
}

// Size is the cache's size and its number of entries.
func Size() (bytes int64, entries int) {
	es := Entries()
	for _, e := range es {
		bytes += e.Size
	}
	return bytes, len(es)
}

// Trim evicts the entries used least lately until the cache is under
// max (to 90% of it, so the next builds don't trim again at once),
// keeping any used within Recent. It returns what it freed.
func Trim(max int64) (freed int64, removed int) {
	if max <= 0 {
		return 0, 0
	}
	withLock(func() {
		es := Entries()
		var total int64
		for _, e := range es {
			total += e.Size
		}
		if total <= max {
			return
		}
		sort.Slice(es, func(i, j int) bool { return es[i].Used.Before(es[j].Used) })
		goal := max / 10 * 9
		cutoff := time.Now().Add(-Recent)
		for _, e := range es {
			if total <= goal {
				break
			}
			if e.Used.After(cutoff) {
				continue
			}
			if os.RemoveAll(e.Path) == nil {
				total -= e.Size
				freed += e.Size
				removed++
			}
		}
	})
	return freed, removed
}

// MaybeTrim trims the cache to Max, at most once every hour.
func MaybeTrim() {
	stamp := cachedir.Path(".trimmed")
	if st, err := os.Stat(stamp); err == nil && time.Since(st.ModTime()) < trimEvery {
		return
	}
	if err := os.MkdirAll(filepath.Dir(stamp), 0o755); err != nil {
		return
	}
	os.WriteFile(stamp, nil, 0o644)
	Touch(stamp)
	Trim(Max())
}

// Clean removes everything in the cache.
func Clean() (freed int64, err error) {
	root := cachedir.Root()
	es, rerr := os.ReadDir(root)
	if rerr != nil {
		if os.IsNotExist(rerr) {
			return 0, nil
		}
		return 0, rerr
	}
	for _, e := range es {
		if e.Name() == ".lock" {
			continue
		}
		p := filepath.Join(root, e.Name())
		size := entry(p).Size
		if rerr := os.RemoveAll(p); rerr != nil {
			if err == nil {
				err = rerr
			}
			continue
		}
		freed += size
	}
	return freed, err
}
