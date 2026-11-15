package pkgs

import (
	"fmt"
	"path/filepath"
	"sync"
)

// Imports resolves what a project's packages import through its
// asuka.lock, for the compiler. The first dependency asked for makes the
// project's dependencies ready (Install: resolved again only when
// asuka.lock doesn't answer asuka.toml, fetched only when not in the
// cache), so a project without dependencies never reads the network.
type Imports struct {
	dir  string
	once sync.Once
	c    *Client
	lock *Lock
	err  error
}

// NewImports resolves the imports of the project in dir.
func NewImports(dir string) *Imports { return &Imports{dir: dir} }

func (im *Imports) load() {
	im.c = NewClient(LoadNPMRC(im.dir))
	m, err := ReadManifest(im.dir)
	if err != nil {
		im.err = err
		return
	}
	if len(m.Dependencies) == 0 {
		im.lock = &Lock{}
		return
	}
	im.lock, im.err = Install(im.c, im.dir, false)
}

// Resolve finds the package that from (a locked package's ID; "" for
// the project) imports as path (@acme/charts, charts2): its ID and its
// directory.
func (im *Imports) Resolve(from, path string) (id, dir string, err error) {
	im.once.Do(im.load)
	if im.err != nil {
		return "", "", im.err
	}
	var deps map[string]string
	if from == "" {
		deps = im.lock.Root
	} else if p := im.lock.Find(from); p != nil {
		deps = p.Deps
	}
	id, ok := deps[path]
	if !ok {
		if from == "" {
			return "", "", fmt.Errorf("no dependency %s: add it with asuka add %s", path, path)
		}
		return "", "", fmt.Errorf("%s has no dependency %s", from, path)
	}
	p := im.lock.Find(id)
	if p == nil {
		return "", "", fmt.Errorf("asuka.lock has no package %s: run asuka install -update", id)
	}
	dir, err = Fetch(im.c, p)
	if err == nil && !filepath.IsAbs(dir) {
		dir = filepath.Join(im.dir, dir) // a file: package, from the project
	}
	return id, dir, err
}
