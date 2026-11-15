package pkgs

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// A dependency's spec, as asuka.toml writes it:
//
//	"^1.2.0"                     a range of the package of the same name
//	"npm:@acme/charts@^2.0.0"    another package (an alias: a second major version)
//	"github:acme/charts#v1.2.0"  a tag, branch or commit of a GitHub repository
//	"file:../charts"             a directory beside the project
type Spec struct {
	Kind  string // "registry", "github", "file"
	Name  string // the registry package
	Range Range
	Repo  string // github: owner/repo
	Ref   string // github: the tag, branch or commit
	Path  string // file:
}

// ParseSpec reads the spec of the dependency imported as key.
func ParseSpec(key, s string) (Spec, error) {
	switch {
	case strings.HasPrefix(s, "npm:"):
		rest := strings.TrimPrefix(s, "npm:")
		at := strings.LastIndexByte(rest, '@')
		if at <= 0 {
			return Spec{}, fmt.Errorf("%s = %q: npm:<package>@<range>", key, s)
		}
		r, err := ParseRange(rest[at+1:])
		if err != nil {
			return Spec{}, fmt.Errorf("%s: %v", key, err)
		}
		return Spec{Kind: "registry", Name: rest[:at], Range: r}, nil
	case strings.HasPrefix(s, "github:"):
		repo, ref, _ := strings.Cut(strings.TrimPrefix(s, "github:"), "#")
		if strings.Count(repo, "/") != 1 {
			return Spec{}, fmt.Errorf("%s = %q: github:<owner>/<repo>#<ref>", key, s)
		}
		if ref == "" {
			ref = "HEAD"
		}
		return Spec{Kind: "github", Repo: repo, Ref: ref}, nil
	case strings.HasPrefix(s, "file:"):
		return Spec{Kind: "file", Path: strings.TrimPrefix(s, "file:")}, nil
	}
	r, err := ParseRange(s)
	if err != nil {
		return Spec{}, fmt.Errorf("%s: %v", key, err)
	}
	return Spec{Kind: "registry", Name: key, Range: r}, nil
}

// Locked is a package as resolved: what asuka.lock records.
type Locked struct {
	ID        string `json:"id"`        // name, or name@major for a second major (an alias's)
	Name      string `json:"name"`      // the registry's name; "" for github and file
	Version   string `json:"version"`   // the version; github: the ref; file: ""
	Source    string `json:"source"`    // the tarball's URL, github:owner/repo#ref, or file:path
	Integrity string `json:"integrity"` // the tarball's sha512; "" for file
	// Deps maps the names the package imports to IDs of other locked
	// packages.
	Deps map[string]string `json:"deps,omitempty"`
}

// Lock is asuka.lock: the app's dependencies (import name → ID) and
// every package they bring.
type Lock struct {
	Version  int               `json:"version"`
	Root     map[string]string `json:"root"`
	Packages []Locked          `json:"packages"`
}

// Find returns the locked package of an ID.
func (l *Lock) Find(id string) *Locked {
	for i := range l.Packages {
		if l.Packages[i].ID == id {
			return &l.Packages[i]
		}
	}
	return nil
}

type req struct {
	id    string // the package's identity in the build
	name  string // its registry name
	rng   Range
	by    string // who requires it, for messages
	alias bool
}

// Resolve picks one version of each package the dependencies bring, by
// minimal version selection (Go's): for each package, the greatest of
// the least versions each requirement accepts, so a build depends only
// on what its requirements say, never on what was published since. A
// package required in two majors without an alias is an error.
func Resolve(c *Client, root map[string]string, projectDir string) (*Lock, error) {
	lock := &Lock{Version: 1, Root: map[string]string{}}
	selected := map[string]Version{}
	names := map[string]string{}
	var reqs []req
	queue := []req{}
	idOf := func(key string, sp Spec) string {
		if sp.Name != key {
			// An alias: the second major is a package of its own.
			return sp.Name + "@" + majorOf(sp.Range)
		}
		return sp.Name
	}
	addDeps := func(by string, deps map[string]string, into map[string]string) error {
		keys := sortedKeys(deps)
		for _, k := range keys {
			sp, err := ParseSpec(k, deps[k])
			if err != nil {
				return fmt.Errorf("%s: %v", by, err)
			}
			switch sp.Kind {
			case "registry":
				id := idOf(k, sp)
				into[k] = id
				queue = append(queue, req{id: id, name: sp.Name, rng: sp.Range, by: by, alias: sp.Name != k})
			case "github":
				id := "github:" + sp.Repo + "#" + sp.Ref
				into[k] = id
				if lock.Find(id) == nil {
					lock.Packages = append(lock.Packages, Locked{ID: id, Version: sp.Ref, Source: id})
				}
			case "file":
				p := sp.Path
				if !filepath.IsAbs(p) {
					p = filepath.Join(projectDir, p)
				}
				if _, err := os.Stat(filepath.Join(p, ManifestFile)); err != nil {
					return fmt.Errorf("%s: %s = %q: no package there (no %s)", by, k, deps[k], ManifestFile)
				}
				// Recorded from the project, so asuka.lock can be committed.
				rel := p
				if r, err := filepath.Rel(projectDir, p); err == nil {
					rel = r
				}
				id := "file:" + filepath.ToSlash(rel)
				into[k] = id
				if lock.Find(id) == nil {
					lock.Packages = append(lock.Packages, Locked{ID: id, Source: id})
				}
			}
		}
		return nil
	}
	if err := addDeps("asuka.toml", root, lock.Root); err != nil {
		return nil, err
	}
	for len(queue) > 0 {
		r := queue[0]
		queue = queue[1:]
		reqs = append(reqs, r)
		doc, err := c.Packument(r.name)
		if err != nil {
			return nil, fmt.Errorf("%s needs %s: %v", r.by, r.name, err)
		}
		min, ok := r.rng.Min(doc.VersionList())
		if !ok {
			return nil, fmt.Errorf("%s needs %s %s, which has no such version", r.by, r.name, r.rng)
		}
		names[r.id] = r.name
		if cur, ok := selected[r.id]; ok && cur.Compare(min) >= 0 {
			continue
		}
		selected[r.id] = min
		meta := doc.Versions[min.String()]
		deps := map[string]string{}
		if err := addDeps(r.name+"@"+min.String(), meta.Dependencies, deps); err != nil {
			return nil, err
		}
	}
	// Every requirement must accept what was selected.
	for _, r := range reqs {
		v := selected[r.id]
		if !r.rng.Match(v) {
			return nil, fmt.Errorf("%s needs %s %s, but %s is needed elsewhere: two majors of a package are two packages, depend on one as an alias (\"%s2\" = \"npm:%s@%s\") and import it under that name", r.by, r.name, r.rng, v, lastSegment(r.name), r.name, r.rng)
		}
	}
	ids := make([]string, 0, len(selected))
	for id := range selected {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	for _, id := range ids {
		v := selected[id]
		doc, _ := c.Packument(names[id])
		meta := doc.Versions[v.String()]
		l := Locked{ID: id, Name: names[id], Version: v.String(), Source: meta.Dist.Tarball, Integrity: meta.Dist.Integrity, Deps: map[string]string{}}
		for _, k := range sortedKeys(meta.Dependencies) {
			sp, err := ParseSpec(k, meta.Dependencies[k])
			if err != nil {
				return nil, err
			}
			switch sp.Kind {
			case "registry":
				l.Deps[k] = idOf(k, sp)
			case "github":
				l.Deps[k] = "github:" + sp.Repo + "#" + sp.Ref
			}
		}
		if len(l.Deps) == 0 {
			l.Deps = nil
		}
		lock.Packages = append(lock.Packages, l)
	}
	sort.Slice(lock.Packages, func(i, j int) bool { return lock.Packages[i].ID < lock.Packages[j].ID })
	return lock, nil
}

func majorOf(r Range) string {
	for _, set := range r.sets {
		for _, c := range set {
			if c.op == ">=" || c.op == "=" {
				return fmt.Sprint(c.v.Major)
			}
		}
	}
	return "0"
}

func lastSegment(name string) string {
	if i := strings.LastIndexByte(name, '/'); i >= 0 {
		return name[i+1:]
	}
	return name
}

func sortedKeys(m map[string]string) []string {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

// ---- The cache ----

// CacheDir is where packages' files are kept, by their content's hash.
func CacheDir() string {
	if d := os.Getenv("ASUKA_PKG_CACHE"); d != "" {
		return d
	}
	d, err := os.UserCacheDir()
	if err != nil {
		d = os.TempDir()
	}
	return filepath.Join(d, "asuka", "pkg")
}

// Fetch makes sure a locked package's files are in the cache and
// returns their directory: a file: package's own directory (from the
// project's, when relative); else the cache's, keyed by the tarball's
// integrity, downloaded (and checked) once.
func Fetch(c *Client, l *Locked) (string, error) {
	if strings.HasPrefix(l.Source, "file:") {
		return filepath.FromSlash(strings.TrimPrefix(l.Source, "file:")), nil
	}
	key := l.Integrity
	if key == "" {
		key = l.Source
	}
	h := sha256.Sum256([]byte(key))
	dir := filepath.Join(CacheDir(), hex.EncodeToString(h[:16]))
	if _, err := os.Stat(filepath.Join(dir, ".complete")); err == nil {
		return dir, nil
	}
	var tgz []byte
	var err error
	if strings.HasPrefix(l.Source, "github:") {
		repo, ref, _ := strings.Cut(strings.TrimPrefix(l.Source, "github:"), "#")
		tgz, err = c.get("https://codeload.github.com/"+repo+"/tar.gz/"+ref, "")
		if err == nil {
			if l.Integrity == "" {
				l.Integrity = Integrity(tgz) // recorded in the lock from now on
			} else if Integrity(tgz) != l.Integrity {
				err = fmt.Errorf("%s changed since asuka.lock recorded it (a moved tag or branch): run asuka install -update %s", l.Source, l.ID)
			}
		}
	} else {
		tgz, err = c.Tarball(PackageMeta{Name: l.Name, Version: l.Version, Dist: struct {
			Tarball   string `json:"tarball"`
			Integrity string `json:"integrity"`
			Shasum    string `json:"shasum"`
		}{l.Source, l.Integrity, ""}})
	}
	if err != nil {
		return "", err
	}
	tmp := dir + ".tmp"
	os.RemoveAll(tmp)
	if err := Extract(tgz, tmp); err != nil {
		return "", fmt.Errorf("%s: %v", l.ID, err)
	}
	if err := os.WriteFile(filepath.Join(tmp, ".complete"), nil, 0o644); err != nil {
		return "", err
	}
	os.RemoveAll(dir)
	if err := os.Rename(tmp, dir); err != nil {
		return "", err
	}
	return dir, nil
}
