// Package ccache is Asuka's build cache (DESIGN.md, "The build cache"):
// a content-addressed store of what builds make (C objects, archives,
// Swift modules, prebuilt packages), shared by every app, checkout and
// configuration on the machine, its size capped by evicting what was used
// least lately (ASUKA_CACHE_MAX, 10 GB by default), with counts of its
// hits and misses (asuka cache stats).
package ccache

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/egoist/asuka/internal/cachedir"
)

// DefaultMax is the cache's size cap when ASUKA_CACHE_MAX doesn't set
// one.
const DefaultMax = 10 << 30

// Max is the cache's size cap: ASUKA_CACHE_MAX (bytes, or with a unit:
// 500M, 10G, 1T), else DefaultMax. 0 turns eviction off.
func Max() int64 {
	if v := os.Getenv("ASUKA_CACHE_MAX"); v != "" {
		if n, err := ParseSize(v); err == nil {
			return n
		}
	}
	return DefaultMax
}

// ParseSize reads a size: a number of bytes, or one with a unit (K, M, G,
// T; KB, MB, GB, TB, KiB... alike: powers of 1024).
func ParseSize(s string) (int64, error) {
	t := strings.ToUpper(strings.TrimSpace(s))
	t = strings.TrimSuffix(strings.TrimSuffix(t, "B"), "I")
	mul := int64(1)
	if n := len(t); n > 0 {
		switch t[n-1] {
		case 'K':
			mul = 1 << 10
		case 'M':
			mul = 1 << 20
		case 'G':
			mul = 1 << 30
		case 'T':
			mul = 1 << 40
		}
		if mul > 1 {
			t = t[:n-1]
		}
	}
	f, err := strconv.ParseFloat(strings.TrimSpace(t), 64)
	if err != nil || f < 0 {
		return 0, fmt.Errorf("ccache: %q isn't a size (10G, 500M, bytes)", s)
	}
	return int64(f * float64(mul)), nil
}

// Dir is a store's directory in the cache: c (C objects, archives,
// executables), swift (Swift modules), prebuilt (downloaded packages).
func Dir(store string) string { return cachedir.Path(store) }

// Key hashes parts into a key: hex SHA-256.
func Key(parts ...string) string {
	h := sha256.New()
	for _, p := range parts {
		fmt.Fprintf(h, "%d:%s\n", len(p), p)
	}
	return hex.EncodeToString(h.Sum(nil))
}

// Sharded is key's path in a store: <store>/<sub>/<key[:2]>/<key><ext>,
// so no directory holds them all.
func Sharded(store, sub, key, ext string) string {
	return filepath.Join(Dir(store), sub, key[:2], key+ext)
}

// Touch marks p used now: eviction takes what was used least lately.
func Touch(p string) {
	now := time.Now()
	os.Chtimes(p, now, now)
}

// touchEvery is how stale an entry's time of use gets before a hit marks
// it used again (as Go's build cache): eviction keeps what was used in
// the days before, so an hour is exact enough, and a build that hits
// hundreds of entries doesn't write each one's time.
const touchEvery = time.Hour

// TouchStale marks p used now (Touch) unless it was within touchEvery,
// and reports whether it did: used is its modification time, as a stat
// of it said.
func TouchStale(p string, used time.Time) bool {
	if time.Since(used) <= touchEvery {
		return false
	}
	Touch(p)
	return true
}

var tmpSeq atomic.Uint64

// TmpName is a name of its own beside p, to write then rename into place:
// builds running at once, in this process or others, share the cache.
func TmpName(p string) string {
	ext := filepath.Ext(p)
	return fmt.Sprintf("%s.%d.%d.tmp%s", strings.TrimSuffix(p, ext), os.Getpid(), tmpSeq.Add(1), ext)
}

// Commit renames tmp to p (the cache's file, written whole), keeping
// another process's if it got there first.
func Commit(tmp, p string) error {
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, p); err != nil {
		os.RemoveAll(tmp)
		if _, serr := os.Stat(p); serr == nil {
			return nil
		}
		return err
	}
	return nil
}

// WriteFile writes data to p whole (a temporary file, renamed).
func WriteFile(p string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	tmp := TmpName(p)
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		os.Remove(tmp)
		return err
	}
	return Commit(tmp, p)
}

// CopyFile copies src to p whole.
func CopyFile(src, p string, mode os.FileMode) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	tmp := TmpName(p)
	out, err := os.OpenFile(tmp, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	_, cerr := io.Copy(out, in)
	if err := out.Close(); cerr == nil {
		cerr = err
	}
	if cerr != nil {
		os.Remove(tmp)
		return cerr
	}
	return Commit(tmp, p)
}

// ---- Hits and misses ----

// Counts are the cache's hits and misses by what they're of (c: C
// objects; swift: Swift modules; prebuilt: packages downloaded, and found
// already there).
type Counts struct {
	Hits   map[string]int64 `json:"hits"`
	Misses map[string]int64 `json:"misses"`
	Since  time.Time        `json:"since"`
}

var (
	countMu sync.Mutex
	pending = Counts{Hits: map[string]int64{}, Misses: map[string]int64{}}
)

// Hit counts a hit of kind.
func Hit(kind string) { count(kind, true) }

// Miss counts a miss of kind.
func Miss(kind string) { count(kind, false) }

func count(kind string, hit bool) {
	countMu.Lock()
	if hit {
		pending.Hits[kind]++
	} else {
		pending.Misses[kind]++
	}
	countMu.Unlock()
}

func statsPath() string { return cachedir.Path("stats.json") }

// Flush adds the counts since the last flush to the cache's (stats.json,
// under its lock), and, at most once an hour, evicts what's over the cap.
// A build calls it when it's done.
func Flush() {
	countMu.Lock()
	add := pending
	pending = Counts{Hits: map[string]int64{}, Misses: map[string]int64{}}
	countMu.Unlock()
	if len(add.Hits)+len(add.Misses) > 0 {
		withLock(func() {
			c := ReadCounts()
			for k, v := range add.Hits {
				c.Hits[k] += v
			}
			for k, v := range add.Misses {
				c.Misses[k] += v
			}
			if b, err := json.MarshalIndent(c, "", "  "); err == nil {
				WriteFile(statsPath(), b)
			}
		})
	}
	MaybeTrim()
}

// ReadCounts are the cache's counts.
func ReadCounts() Counts {
	c := Counts{Hits: map[string]int64{}, Misses: map[string]int64{}}
	if b, err := os.ReadFile(statsPath()); err == nil {
		json.Unmarshal(b, &c)
	}
	if c.Hits == nil {
		c.Hits = map[string]int64{}
	}
	if c.Misses == nil {
		c.Misses = map[string]int64{}
	}
	if c.Since.IsZero() {
		c.Since = time.Now().UTC().Truncate(time.Second)
	}
	return c
}

// ResetCounts zeroes the counts.
func ResetCounts() { os.Remove(statsPath()) }

// withLock runs f holding the cache's lock (other processes' Flush and
// Trim wait).
func withLock(f func()) {
	root := cachedir.Root()
	if err := os.MkdirAll(root, 0o755); err != nil {
		f()
		return
	}
	lf, err := os.OpenFile(filepath.Join(root, ".lock"), os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		f()
		return
	}
	defer lf.Close()
	if lockFile(lf) == nil {
		defer unlockFile(lf)
	}
	f()
}
