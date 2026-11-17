package cbuild

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/cachedir"
	"github.com/egoist/asuka/internal/ccache"
)

// The object cache (DESIGN.md, "The build cache"): every C file compiled
// locally is kept in the build cache by what it's compiled from, as
// ccache's direct mode keeps it, so a build compiles only what changed,
// and objects are shared by every app, checkout and configuration.
//
// A source's first key is the compiler's identity (its path and what
// --version says), its flags, its directory and its bytes; under it the
// cache keeps a manifest (c/m) of the objects compiled from it: each with
// the headers that compile included (the compiler's depfile, -MMD) and
// their contents' hashes. A lookup takes the object whose headers all
// still hash the same; else the source is compiled, and its object (c/o)
// keyed by the first key and its headers'.
//
// Paths under the repository (a checkout's) or the cache are written
// relative to them in keys and manifests, so another checkout's build
// finds what this one compiled.

// manifestEntries is how many objects a manifest keeps (each a version
// of the headers the source includes), newest first.
const manifestEntries = 16

var (
	identityMu   sync.Mutex
	identityMemo = map[string]string{}
)

// Identity is the compiler's identity in keys: its program and leading
// arguments and what its --version says (Zig's version too, which its
// libc headers and runtime follow).
func (c Compiler) Identity() string {
	k := c.Path + "\x00" + strings.Join(c.Args, "\x00")
	identityMu.Lock()
	v, ok := identityMemo[k]
	identityMu.Unlock()
	if ok {
		return v
	}
	args := append(append([]string{}, c.Args...), "--version")
	cmd := exec.Command(c.Path, args...)
	// zig cc --version leaves an empty a.o where it runs.
	if tmp, err := os.MkdirTemp("", "asuka-cc-"); err == nil {
		cmd.Dir = tmp
		defer os.RemoveAll(tmp)
	}
	out, _ := cmd.CombinedOutput()
	v = k + "\n" + string(out)
	if c.IsZig() {
		zv, _ := exec.Command(c.Path, "version").Output()
		v += "\nzig " + strings.TrimSpace(string(zv))
	}
	if st, err := os.Stat(c.Path); err == nil {
		// A compiler replaced in place under the same name and version
		// string (a nightly's) is another.
		v += fmt.Sprintf("\n%d", st.Size())
	}
	identityMu.Lock()
	identityMemo[k] = v
	identityMu.Unlock()
	return v
}

// ---- Paths in keys ----

type pathRoot struct{ dir, label string }

var (
	rootsOnce sync.Once
	roots     []pathRoot
)

// pathRoots are the directories whose paths keys write relative to them:
// the repository's (when the CLI runs from a checkout, or a test from
// one) and the cache's, longest first.
func pathRoots() []pathRoot {
	rootsOnce.Do(func() {
		if r := RepoRoot(); r != "" {
			roots = append(roots, pathRoot{r, "$REPO"})
		}
		roots = append(roots, pathRoot{cachedir.Root(), "$CACHE"})
		sort.Slice(roots, func(i, j int) bool { return len(roots[i].dir) > len(roots[j].dir) })
	})
	return roots
}

// RepoRoot is the repository's directory when the CLI runs from a
// checkout (the core's sources are a checkout's core/), else "".
func RepoRoot() string {
	core, err := CoreDir()
	if err != nil || CoreEmbedded() {
		return ""
	}
	r := filepath.Dir(core)
	if _, err := os.Stat(filepath.Join(r, "go.mod")); err != nil {
		return ""
	}
	return r
}

// keyPath writes paths in s relative to the roots.
func keyPath(s string) string {
	for _, r := range pathRoots() {
		if s == r.dir {
			return r.label
		}
		s = strings.ReplaceAll(s, r.dir+string(filepath.Separator), r.label+"/")
	}
	return filepath.ToSlash(s)
}

// realPath is keyPath's path back.
func realPath(s string) string {
	for _, r := range pathRoots() {
		if s == r.label {
			return r.dir
		}
		if rest, ok := strings.CutPrefix(s, r.label+"/"); ok {
			return filepath.Join(r.dir, filepath.FromSlash(rest))
		}
	}
	return filepath.FromSlash(s)
}

// ---- Files' hashes ----

type fileSum struct {
	size, mtime int64
	sum         string
}

var (
	sumMu   sync.Mutex
	sumMemo = map[string]fileSum{}
)

// fileHash is the SHA-256 of a file's contents, hashed once per process
// for each version of it (its size and modification time).
func fileHash(p string) (string, error) {
	st, err := os.Stat(p)
	if err != nil {
		return "", err
	}
	sumMu.Lock()
	m, ok := sumMemo[p]
	sumMu.Unlock()
	if ok && m.size == st.Size() && m.mtime == st.ModTime().UnixNano() {
		return m.sum, nil
	}
	b, err := os.ReadFile(p)
	if err != nil {
		return "", err
	}
	s := sha256.Sum256(b)
	sum := hex.EncodeToString(s[:])
	sumMu.Lock()
	sumMemo[p] = fileSum{st.Size(), st.ModTime().UnixNano(), sum}
	sumMu.Unlock()
	return sum, nil
}

// ---- Objects ----

// Object is a compiled source in the cache.
type Object struct {
	Path string // its file in the cache
	Key  string // its key: what it was compiled from
}

// CompileCached compiles each source with flags (the whole of them: the
// core's are the caller's to add) into the object cache, in parallel,
// and returns the objects, in the sources' order. Headers (.h, .def, .inc)
// among srcs are skipped: what a source includes is its depfile's.
func (c Compiler) CompileCached(srcs, flags []string) ([]Object, error) {
	var cs []string
	for _, s := range srcs {
		if strings.HasSuffix(s, ".c") {
			cs = append(cs, s)
		}
	}
	objs := make([]Object, len(cs))
	errs := make([]error, len(cs))
	sem := make(chan struct{}, runtime.NumCPU())
	var wg sync.WaitGroup
	id := c.Identity()
	for i, src := range cs {
		wg.Add(1)
		go func(i int, src string) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			objs[i], errs[i] = c.object(id, src, flags)
		}(i, src)
	}
	wg.Wait()
	var msgs []string
	for _, e := range errs {
		if e != nil {
			msgs = append(msgs, e.Error())
		}
	}
	if msgs != nil {
		return nil, fmt.Errorf("%s", strings.Join(msgs, "\n"))
	}
	return objs, nil
}

// Paths are the objects' files.
func Paths(objs []Object) []string {
	out := make([]string, len(objs))
	for i, o := range objs {
		out[i] = o.Path
	}
	return out
}

// Keys are the objects' keys.
func Keys(objs []Object) []string {
	out := make([]string, len(objs))
	for i, o := range objs {
		out[i] = o.Key
	}
	return out
}

func (c Compiler) object(id, src string, flags []string) (Object, error) {
	sum, err := fileHash(src)
	if err != nil {
		return Object{}, err
	}
	src, _ = filepath.Abs(src)
	var nf []string
	for _, f := range flags {
		nf = append(nf, keyPath(f))
	}
	first := ccache.Key("c-object-1", id, strings.Join(nf, "\x00"), keyPath(filepath.Dir(src)), sum)
	manifest := ccache.Sharded("c", "m", first, "")
	entries := readManifest(manifest)
	for _, e := range entries {
		if !e.current() {
			continue
		}
		p := ccache.Sharded("c", "o", e.obj, ".o")
		if _, err := os.Stat(p); err != nil {
			continue
		}
		ccache.Touch(p)
		ccache.Touch(manifest)
		ccache.Hit("c")
		return Object{Path: p, Key: e.obj}, nil
	}
	ccache.Miss("c")
	tmpDir := filepath.Join(ccache.Dir("c"), "tmp")
	if err := os.MkdirAll(tmpDir, 0o755); err != nil {
		return Object{}, err
	}
	tmp := ccache.TmpName(filepath.Join(tmpDir, first[:16]+".o"))
	dep := tmp + ".d"
	defer os.Remove(dep)
	args := append(append([]string{}, c.Args...), flags...)
	args = append(args, "-MMD", "-MF", dep, "-c", src, "-o", tmp)
	if err := c.run(args); err != nil {
		os.Remove(tmp)
		return Object{}, err
	}
	data, err := os.ReadFile(dep)
	if err != nil {
		os.Remove(tmp)
		return Object{}, fmt.Errorf("cbuild: %s's depfile: %v", src, err)
	}
	var e manifestEntry
	for _, d := range parseDepfile(data) {
		d, _ = filepath.Abs(d) // the compiler ran in our directory
		if d == src {
			continue
		}
		h, err := fileHash(d)
		if err != nil {
			os.Remove(tmp)
			return Object{}, err
		}
		e.deps = append(e.deps, [2]string{keyPath(d), h})
	}
	sort.Slice(e.deps, func(i, j int) bool { return e.deps[i][0] < e.deps[j][0] })
	parts := []string{first}
	for _, d := range e.deps {
		parts = append(parts, d[0], d[1])
	}
	e.obj = ccache.Key(parts...)
	p := ccache.Sharded("c", "o", e.obj, ".o")
	if err := ccache.Commit(tmp, p); err != nil {
		return Object{}, err
	}
	writeManifest(manifest, append([]manifestEntry{e}, entries...))
	return Object{Path: p, Key: e.obj}, nil
}

// manifestEntry is an object compiled from a source, and the headers
// that compile included, with their hashes.
type manifestEntry struct {
	obj  string
	deps [][2]string // keyPath, SHA-256
}

// current reports whether e's headers all still hash the same.
func (e manifestEntry) current() bool {
	for _, d := range e.deps {
		if h, err := fileHash(realPath(d[0])); err != nil || h != d[1] {
			return false
		}
	}
	return true
}

// A manifest is lines: "obj <key>", then "dep <sha256> <path>" for each
// of its headers.
func readManifest(p string) []manifestEntry {
	b, err := os.ReadFile(p)
	if err != nil {
		return nil
	}
	var out []manifestEntry
	for _, l := range strings.Split(string(b), "\n") {
		switch {
		case strings.HasPrefix(l, "obj "):
			out = append(out, manifestEntry{obj: l[4:]})
		case strings.HasPrefix(l, "dep ") && len(out) > 0 && len(l) > 4+64+1:
			e := &out[len(out)-1]
			e.deps = append(e.deps, [2]string{l[4+64+1:], l[4 : 4+64]})
		}
	}
	return out
}

func writeManifest(p string, es []manifestEntry) {
	var b bytes.Buffer
	seen := map[string]bool{}
	n := 0
	for _, e := range es {
		if seen[e.obj] || n == manifestEntries {
			continue
		}
		seen[e.obj] = true
		n++
		fmt.Fprintf(&b, "obj %s\n", e.obj)
		for _, d := range e.deps {
			fmt.Fprintf(&b, "dep %s %s\n", d[1], d[0])
		}
	}
	ccache.WriteFile(p, b.Bytes())
}

// parseDepfile reads a make rule (the compiler's -MMD): the files after
// its target's colon, with escaped spaces and line continuations.
func parseDepfile(b []byte) []string {
	s := strings.ReplaceAll(string(b), "\r\n", "\n")
	s = strings.ReplaceAll(s, "\\\n", " ")
	// The target ends at the first colon followed by a space or the
	// line's end (C:\ is a Windows drive).
	i := 0
	for ; i < len(s); i++ {
		if s[i] == ':' && (i+1 == len(s) || s[i+1] == ' ' || s[i+1] == '\n' || s[i+1] == '\t') {
			break
		}
	}
	if i >= len(s) {
		return nil
	}
	s = s[i+1:]
	var out []string
	var cur strings.Builder
	flush := func() {
		if cur.Len() > 0 {
			out = append(out, cur.String())
			cur.Reset()
		}
	}
	for j := 0; j < len(s); j++ {
		ch := s[j]
		switch {
		case ch == '\\' && j+1 < len(s) && s[j+1] == ' ':
			cur.WriteByte(' ')
			j++
		case ch == '$' && j+1 < len(s) && s[j+1] == '$':
			cur.WriteByte('$')
			j++
		case ch == ' ' || ch == '\t' || ch == '\n':
			flush()
			if ch == '\n' {
				// A second rule (-MP's phony targets): its prerequisites
				// are the first's.
				rest := s[j+1:]
				if strings.Contains(rest, ":") {
					j = len(s)
				}
			}
		default:
			cur.WriteByte(ch)
		}
	}
	flush()
	return out
}

// ---- Archives and executables ----

// ArchiveCached makes a static library of objs with the toolchain's
// archiver, kept in the cache by the objects' keys, and returns its path.
func (tc *Toolchain) ArchiveCached(objs []Object) (string, error) {
	key := ccache.Key(append([]string{"c-archive-1", tc.Target.String(), strings.Join(tc.AR, " ")}, Keys(objs)...)...)
	p := ccache.Sharded("c", "a", key, ".a")
	if _, err := os.Stat(p); err == nil {
		ccache.Touch(p)
		return p, nil
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return "", err
	}
	tmp := ccache.TmpName(p)
	if err := tc.archive(tmp, Paths(objs)); err != nil {
		os.Remove(tmp)
		return "", err
	}
	return p, ccache.Commit(tmp, p)
}

// archive runs the archiver: out made of members (objects or archives:
// libtool takes both; ar only objects).
func (tc *Toolchain) archive(out string, members []string) error {
	os.Remove(out)
	args := append(append([]string{}, tc.AR[1:]...), out)
	args = append(args, members...)
	cmd := exec.Command(tc.AR[0], args...)
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("%s: %v\n%s", strings.Join(tc.AR, " "), err, b)
	}
	return nil
}
