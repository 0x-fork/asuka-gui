package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/cachedir"
	"github.com/egoist/asuka/internal/ccache"
	"github.com/egoist/asuka/internal/prebuilt"
	androidHost "github.com/egoist/asuka/platform/android"
	"github.com/egoist/asuka/sdk"
	"github.com/egoist/asuka/std"
)

// The Android host's Kotlin and the std and sdk packages' as AARs
// (DESIGN.md, "The build cache"): an app's build depends on them, from a
// local Maven repository, rather than compiling their modules. A release
// of the CLI carries them (internal/prebuilt: the release's
// asuka-prebuilt-android-aar archive, its maven/), versioned as the CLI; else the CLI publishes
// them once into the build cache's (maven/), each versioned by a hash of
// its sources. An app's own native packages' modules are compiled with
// the app, as before.

// androidHostModules are the host's modules, published as their names.
var androidHostModules = []string{"asuka", "asuka-video", "asuka-push", "asuka-periodic"}

// androidAARs are what an app's Gradle build depends on instead of the
// host's and the std and sdk packages' modules.
type androidAARs struct {
	repo string            // the local Maven repository
	host string            // dev.asuka:asuka:<version>
	deps []string          // the coordinates the app depends on: the host's, its optional modules', packages'
	pkgs map[string]bool   // the native modules (native-…) they stand in for
	more map[string]string // module → version, of the host's (publishing)
}

// androidPackageArtifact is a std or sdk package's AAR's artifact
// (std-sqlite, sdk-device), or "" for another package's.
func androidPackageArtifact(dir string) string {
	parent := filepath.Clean(filepath.Dir(dir))
	if d, err := std.Dir(); err == nil && parent == filepath.Clean(d) {
		return "std-" + filepath.Base(dir)
	}
	if d, err := sdk.Dir(); err == nil && parent == filepath.Clean(d) {
		return "sdk-" + filepath.Base(dir)
	}
	return ""
}

// androidPrebuilt finds (or, from a checkout, publishes into the cache)
// the AARs app's build in the host's project src depends on, and sets
// them on its configuration: gradleProps names them.
func (t *androidToolchain) androidPrebuilt(app *asukaApp, src string) error {
	cfg := app.cfg
	cfg.aars = nil
	byDir := map[string]string{} // a native module's directory → its package's
	for _, m := range cfg.nativeModules {
		byDir[m[1]] = filepath.Dir(m[1])
	}
	a := &androidAARs{pkgs: map[string]bool{}, more: map[string]string{}}
	// The CLI's own, when it has them and the packages are its own.
	if repo, ok, err := prebuilt.MavenRepo(); err != nil {
		return err
	} else if ok {
		m, _ := prebuilt.Current()
		a.repo = repo
		a.host = "dev.asuka:asuka:" + m.Version
		a.deps = append(a.deps, a.host)
		for _, mod := range cfg.androidModules {
			a.deps = append(a.deps, "dev.asuka:asuka-"+mod+":"+m.Version)
		}
		for _, nm := range cfg.nativeModules {
			art := androidPackageArtifact(byDir[nm[1]])
			embedded := strings.HasPrefix(art, "std-") && std.Embedded() || strings.HasPrefix(art, "sdk-") && sdk.Embedded()
			if art != "" && embedded && aarExists(repo, art, m.Version) {
				a.deps = append(a.deps, "dev.asuka:"+art+":"+m.Version)
				a.pkgs[nm[0]] = true
			}
		}
		cfg.aars = a
		return nil
	}
	// Else built into the cache's repository, once per version of each.
	repo := cachedir.Path("maven")
	a.repo = repo
	hostV := "0.0.0-" + androidHostKey()[:16]
	for _, mod := range androidHostModules {
		a.more[mod] = hostV
	}
	a.host = "dev.asuka:asuka:" + hostV
	a.deps = append(a.deps, a.host)
	for _, mod := range cfg.androidModules {
		a.deps = append(a.deps, "dev.asuka:asuka-"+mod+":"+hostV)
	}
	type pub struct{ module, artifact, version, dir string }
	var missing []pub
	for _, mod := range androidHostModules {
		if !aarExists(repo, mod, hostV) {
			missing = append(missing, pub{mod, mod, hostV, ""})
		}
	}
	for _, nm := range cfg.nativeModules {
		art := androidPackageArtifact(byDir[nm[1]])
		if art == "" {
			continue
		}
		v := "0.0.0-" + ccache.Key(hostV, treeKey(nm[1]))[:16]
		a.deps = append(a.deps, "dev.asuka:"+art+":"+v)
		a.pkgs[nm[0]] = true
		a.more[nm[0]] = v
		if !aarExists(repo, art, v) {
			missing = append(missing, pub{nm[0], art, v, nm[1]})
		} else {
			touchAAR(repo, art, v)
		}
	}
	for _, mod := range androidHostModules {
		touchAAR(repo, mod, hostV)
	}
	if len(missing) > 0 {
		var names []string
		var tasks, natives, versions, artifacts []string
		for _, p := range missing {
			names = append(names, p.artifact)
			tasks = append(tasks, ":"+p.module+":publishPrebuiltPublicationToPrebuiltRepository")
			if p.dir != "" {
				natives = append(natives, p.module+"="+p.dir)
				artifacts = append(artifacts, p.module+"="+p.artifact)
			}
		}
		for mod, v := range a.more {
			versions = append(versions, mod+"="+v)
		}
		sort.Strings(versions)
		fmt.Printf("asuka: building the Android libraries %s (once)\n", strings.Join(names, ", "))
		args := append(tasks, "-PasukaPublishRepo="+repo, "-PasukaPublishVersions="+strings.Join(versions, ";"),
			"-PasukaPublishArtifacts="+strings.Join(artifacts, ";"))
		if len(natives) > 0 {
			args = append(args, "-PasukaNativeModules="+strings.Join(natives, ";"))
		}
		if err := t.gradle(src, args...); err != nil {
			return err
		}
	}
	cfg.aars = a
	return nil
}

// aarExists reports whether repo has artifact's AAR at version.
func aarExists(repo, artifact, version string) bool {
	_, err := os.Stat(filepath.Join(repo, "dev", "asuka", artifact, version, artifact+"-"+version+".aar"))
	return err == nil
}

// touchAAR marks a version of an AAR used (the cache evicts what was
// used least lately).
func touchAAR(repo, artifact, version string) {
	ccache.Touch(filepath.Join(repo, "dev", "asuka", artifact, version))
}

// androidHostKey is a hash of the host's Gradle project but the shell
// app's: what its modules' AARs are built from.
func androidHostKey() string {
	h := sha256.New()
	fs.WalkDir(androidHost.Files, ".", func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() || strings.HasPrefix(p, "app/") {
			return nil
		}
		b, _ := androidHost.Files.ReadFile(p)
		fmt.Fprintf(h, "%s %d\n", p, len(b))
		h.Write(b)
		return nil
	})
	return hex.EncodeToString(h.Sum(nil))
}

// treeKey is a hash of a directory's files (their paths relative to it
// and contents), what a build makes there left out: the same in every
// checkout.
func treeKey(dir string) string {
	h := sha256.New()
	filepath.WalkDir(dir, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if p != dir && (d.Name() == "build" || d.Name() == ".gradle" || d.Name() == ".cxx") {
				return filepath.SkipDir
			}
			return nil
		}
		r, _ := filepath.Rel(dir, p)
		b, _ := os.ReadFile(p)
		fmt.Fprintf(h, "%s %d\n", filepath.ToSlash(r), len(b))
		h.Write(b)
		return nil
	})
	return hex.EncodeToString(h.Sum(nil))
}
