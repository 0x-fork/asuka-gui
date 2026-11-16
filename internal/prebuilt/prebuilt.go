// Package prebuilt is what a release of the CLI carries prebuilt beside
// it (DESIGN.md, "The build cache"): the core's, the desktop hosts' and
// the std and sdk packages' C as static libraries for every target and
// configuration, and the Android host's and packages' Kotlin as AARs,
// published to npm with the release, an npm package per target
// (@egoist/asuka-prebuilt-<name>). The CLI embeds only their manifest
// (manifest.json: each package's exact version and integrity, each
// library's path and SHA-256), and fetches a target's package from the
// npm registry the first time a build needs it, into the build cache.
// A CLI built from a checkout has none (an empty manifest): its builds
// build the same libraries into the cache.
package prebuilt

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/ccache"
)

// Scope is the npm scope the packages are published under.
const Scope = "@egoist"

// Manifest is what the CLI knows of its prebuilt packages.
type Manifest struct {
	// Version is the CLI's, which its packages share.
	Version string `json:"version,omitempty"`
	// Packages by their short names (darwin-universal, android-aar).
	Packages map[string]Package `json:"packages"`
	// Archives by target/config/unit (darwin-arm64/release/core).
	Archives map[string]File `json:"archives"`
	// Maven is the Android AARs' local Maven repository: the package and
	// the directory in it.
	Maven *File `json:"maven,omitempty"`
}

// Package is one npm package.
type Package struct {
	Name      string `json:"name"`      // @egoist/asuka-prebuilt-darwin-universal
	Version   string `json:"version"`   // exact
	Integrity string `json:"integrity"` // its tarball's (npm's dist.integrity: sha512-<base64>)
	// Compiler is what built its C (zig, apple, ndk): a build links a
	// prebuilt library only with the same family's.
	Compiler string `json:"compiler,omitempty"`
}

// File is a file (or a directory) in a package.
type File struct {
	Package string `json:"package"`          // its short name
	Path    string `json:"path"`             // in the package (lib/release/libcore.a)
	SHA256  string `json:"sha256,omitempty"` // a file's
}

//go:embed manifest.json
var embedded []byte

var (
	manifestOnce sync.Once
	manifest     Manifest
	manifestErr  error
)

// Current is the manifest: ASUKA_PREBUILT_MANIFEST's (a file: tests, a
// release's checks), else the one embedded in the CLI.
func Current() (Manifest, error) {
	manifestOnce.Do(func() {
		b := embedded
		if p := os.Getenv("ASUKA_PREBUILT_MANIFEST"); p != "" {
			if b, manifestErr = os.ReadFile(p); manifestErr != nil {
				return
			}
		}
		manifestErr = json.Unmarshal(b, &manifest)
		if manifestErr != nil {
			manifestErr = fmt.Errorf("prebuilt: the manifest: %v", manifestErr)
		}
	})
	return manifest, manifestErr
}

// reset forgets the manifest read (tests).
func reset() {
	manifestOnce = sync.Once{}
	manifest = Manifest{}
	manifestErr = nil
}

// ArchiveKey is an archive's key in the manifest.
func ArchiveKey(target, config, unit string) string { return target + "/" + config + "/" + unit }

// Compiler is the compiler family target's libraries were built with,
// or "" when the CLI has none for it.
func Compiler(target string) string {
	m, err := Current()
	if err != nil {
		return ""
	}
	for k, f := range m.Archives {
		if len(k) > len(target) && k[:len(target)+1] == target+"/" {
			return m.Packages[f.Package].Compiler
		}
	}
	return ""
}

// Archive is the prebuilt library of unit for target and config, fetched
// into the cache first if it isn't there; ok is false when the CLI has
// none.
func Archive(target, config, unit string) (path string, ok bool, err error) {
	m, err := Current()
	if err != nil {
		return "", false, err
	}
	f, ok := m.Archives[ArchiveKey(target, config, unit)]
	if !ok {
		return "", false, nil
	}
	p, err := m.file(f)
	return p, err == nil, err
}

// MavenRepo is the local Maven repository of the Android host's and the
// std and sdk packages' AARs, fetched first if it isn't there; ok is
// false when the CLI has none.
func MavenRepo() (dir string, ok bool, err error) {
	m, err := Current()
	if err != nil {
		return "", false, err
	}
	if m.Maven == nil {
		return "", false, nil
	}
	p, err := m.file(*m.Maven)
	return p, err == nil, err
}

var (
	verifiedMu sync.Mutex
	verified   = map[string]bool{}
)

// file is f's path in its package, fetched; a file is checked against
// its SHA-256 once per process.
func (m Manifest) file(f File) (string, error) {
	pkg, ok := m.Packages[f.Package]
	if !ok {
		return "", fmt.Errorf("prebuilt: the manifest names no package %q", f.Package)
	}
	dir, err := Fetch(pkg)
	if err != nil {
		return "", err
	}
	p := filepath.Join(dir, filepath.FromSlash(f.Path))
	if f.SHA256 == "" {
		return p, nil
	}
	verifiedMu.Lock()
	done := verified[p]
	verifiedMu.Unlock()
	if done {
		return p, nil
	}
	sum, err := sha256File(p)
	if err != nil {
		return "", fmt.Errorf("prebuilt: %s: %v", p, err)
	}
	if sum != f.SHA256 {
		// Damaged in the cache: fetched again next time.
		os.RemoveAll(dir)
		return "", fmt.Errorf("prebuilt: %s isn't the library %s names (SHA-256); it was removed, build again", p, pkg.Name)
	}
	verifiedMu.Lock()
	verified[p] = true
	verifiedMu.Unlock()
	ccache.Touch(dir)
	return p, nil
}
