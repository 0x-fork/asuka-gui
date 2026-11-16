package appbuild

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/cbuild"
)

// Version is the CLI's release ("dev" for one built from source), which
// names the prebuilt desktop hosts it fetches. The CLI sets it.
var Version = "dev"

// hostSums are the SHA-256 of the desktop hosts the CLI's release ships,
// "<os>-<arch>=<hex>" separated by commas: set when the release is built
// (scripts/npm, -ldflags -X), and what a fetched host is checked against.
var hostSums string

// HostRelease is where a release's hosts are fetched from: the GitHub
// release of its tag, %s the version and the file.
var HostRelease = "https://github.com/egoist/asuka/releases/download/v%s/%s"

// HostFile is the name of the prebuilt host for t in a release.
func HostFile(t cbuild.Target) string {
	n := "asuka-host-" + t.OS + "-" + t.Arch
	if t.OS == "windows" {
		n += ".exe"
	}
	return n
}

// Checkout is the Asuka repository this CLI runs from (go run, go test,
// go install from a checkout), "" for an installed release.
func Checkout() string {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return ""
	}
	root := filepath.Dir(filepath.Dir(filepath.Dir(file)))
	if b, err := os.ReadFile(filepath.Join(root, "go.mod")); err == nil && bytes.Contains(b, []byte("module github.com/egoist/asuka\n")) {
		if _, err := os.Stat(filepath.Join(root, "cmd", "asuka-host")); err == nil {
			return root
		}
	}
	return ""
}

// HostOptions say how the host is built from source.
type HostOptions struct {
	// Release strips the executable of its symbols.
	Release bool
	// Syso, for Windows, are the executable's resources (its icon, its
	// manifest, its version: a COFF object, as go build links a main
	// package's .syso files).
	Syso []byte
	// Verbose prints the commands.
	Verbose bool
	// Natives are the directories of the app's own native packages' Go
	// for the desktop (a package's desktop/): the host is built with them
	// (the standard library's are in every host already). It needs go.
	Natives []string
}

// Host writes the desktop host's executable for t (cmd/asuka-host: the
// same for every app) at out:
//
//   - ASUKA_HOST, a host built elsewhere: the file, or the directory that
//     has HostFile(t);
//   - from source (go build, CGO_ENABLED=0) when the CLI runs from a
//     checkout;
//   - else the CLI's release's, fetched once into the user's cache and
//     checked against the hash the CLI was built with.
func Host(t cbuild.Target, out string, o HostOptions) error {
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	if len(o.Natives) > 0 {
		// A host of the app's own: its packages' Go linked in.
		return buildHost(Checkout(), t, out, o)
	}
	if p := os.Getenv("ASUKA_HOST"); p != "" {
		if st, err := os.Stat(p); err == nil && st.IsDir() {
			p = filepath.Join(p, HostFile(t))
		}
		return copyExe(p, out)
	}
	if root := Checkout(); root != "" {
		return buildHost(root, t, out, o)
	}
	p, err := fetchHost(t)
	if err != nil {
		return err
	}
	return copyExe(p, out)
}

// buildHost builds the host with go: the checkout's (root), or, with
// root "", the release's module (Version) from the module proxy, which
// only an app with native packages' Go needs.
func buildHost(root string, t cbuild.Target, out string, o HostOptions) error {
	if root == "" && Version == "dev" {
		return fmt.Errorf("the app's native packages have Go for the desktop, and this asuka is a build from source outside its checkout: run it from the checkout")
	}
	args := []string{"build", "-trimpath"}
	if o.Release {
		ld := "-s -w -buildid="
		if t.OS == "windows" {
			ld += " -H windowsgui" // no console window
		}
		args = append(args, "-ldflags="+ld)
	}
	args = append(args, "-o", out)
	dir := root
	pkg := "./cmd/asuka-host"
	if len(o.Syso) > 0 || len(o.Natives) > 0 || root == "" {
		// A module of its own, which uses this checkout (or the release's
		// module): go build links only the main package's own .syso files
		// (an overlay doesn't reach them), and the app's packages' Go are
		// packages of it.
		tmp, err := os.MkdirTemp("", "asuka-host-")
		if err != nil {
			return err
		}
		defer os.RemoveAll(tmp)
		main := "// The desktop host (cmd/asuka-host), with the app's native packages' Go.\npackage main\n\nimport (\n\t\"github.com/egoist/asuka/host/apphost\"\n"
		for i, d := range o.Natives {
			sub := fmt.Sprintf("natives/p%d", i)
			files, _ := filepath.Glob(filepath.Join(d, "*.go"))
			for _, f := range files {
				if strings.HasSuffix(f, "_test.go") {
					continue
				}
				if err := copyFile(f, filepath.Join(tmp, filepath.FromSlash(sub), filepath.Base(f))); err != nil {
					return err
				}
			}
			main += fmt.Sprintf("\t_ \"asukahost/%s\" // %s\n", sub, filepath.ToSlash(d))
		}
		main += ")\n\nfunc main() { apphost.Main() }\n"
		if err := os.WriteFile(filepath.Join(tmp, "main.go"), []byte(main), 0o644); err != nil {
			return err
		}
		if len(o.Syso) > 0 {
			if err := os.WriteFile(filepath.Join(tmp, "rsrc_windows_"+t.Arch+".syso"), o.Syso, 0o644); err != nil {
				return err
			}
		}
		goVersion := "1.25"
		var mod string
		if root != "" {
			if b, err := os.ReadFile(filepath.Join(root, "go.mod")); err == nil {
				for _, l := range strings.Split(string(b), "\n") {
					if v, ok := strings.CutPrefix(l, "go "); ok {
						goVersion = strings.TrimSpace(v)
					}
				}
			}
			mod = fmt.Sprintf("module asukahost\n\ngo %s\n\nrequire github.com/egoist/asuka v0.0.0\n\nreplace github.com/egoist/asuka => %s\n", goVersion, filepath.ToSlash(root))
			if err := copyFile(filepath.Join(root, "go.sum"), filepath.Join(tmp, "go.sum")); err != nil {
				return err
			}
		} else {
			mod = fmt.Sprintf("module asukahost\n\ngo %s\n\nrequire github.com/egoist/asuka v%s\n", goVersion, Version)
		}
		if err := os.WriteFile(filepath.Join(tmp, "go.mod"), []byte(mod), 0o644); err != nil {
			return err
		}
		args = append([]string{args[0], "-mod=mod"}, args[1:]...)
		dir, pkg = tmp, "."
	}
	args = append(args, pkg)
	cmd := exec.Command("go", args...)
	cmd.Dir = dir
	cmd.Env = append(os.Environ(), "GOOS="+t.OS, "GOARCH="+t.Arch, "CGO_ENABLED=0", "GOFLAGS=", "GOWORK=off")
	if o.Verbose {
		fmt.Printf("+ GOOS=%s GOARCH=%s CGO_ENABLED=0 go %s\n", t.OS, t.Arch, strings.Join(args, " "))
	}
	if b, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("building the desktop host (cmd/asuka-host) for %s: %v\n%s", t, err, b)
	}
	return nil
}

// hostSum is the hash the release says t's host has, "" if none.
func hostSum(t cbuild.Target) string {
	for _, kv := range strings.Split(hostSums, ",") {
		if k, v, ok := strings.Cut(kv, "="); ok && k == t.OS+"-"+t.Arch {
			return v
		}
	}
	return ""
}

// fetchHost returns the path of the release's host for t in the user's
// cache, fetching it first if it isn't there.
func fetchHost(t cbuild.Target) (string, error) {
	sum := hostSum(t)
	if Version == "dev" || sum == "" {
		return "", fmt.Errorf("this asuka has no prebuilt desktop host for %s: run it from a checkout of the Asuka repository (go run ./cmd/asuka), or set ASUKA_HOST to a host built with go build ./cmd/asuka-host", t)
	}
	p := filepath.Join(cacheDir(), "host-"+Version, HostFile(t))
	if fileSum(p) == sum {
		return p, nil
	}
	url := fmt.Sprintf(HostRelease, Version, HostFile(t))
	fmt.Printf("asuka: fetching the desktop host for %s (once)\n", t)
	c := &http.Client{Timeout: 5 * time.Minute}
	resp, err := c.Get(url)
	if err != nil {
		return "", fmt.Errorf("fetching %s: %v", url, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("fetching %s: %s", url, resp.Status)
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return "", err
	}
	tmp := fmt.Sprintf("%s.%d.tmp", p, os.Getpid())
	f, err := os.Create(tmp)
	if err != nil {
		return "", err
	}
	h := sha256.New()
	_, err = io.Copy(io.MultiWriter(f, h), resp.Body)
	f.Close()
	if err != nil {
		os.Remove(tmp)
		return "", fmt.Errorf("fetching %s: %v", url, err)
	}
	if got := hex.EncodeToString(h.Sum(nil)); got != sum {
		os.Remove(tmp)
		return "", fmt.Errorf("%s: its SHA-256 is %s, but this asuka's release says %s", url, got, sum)
	}
	os.Chmod(tmp, 0o755)
	return p, os.Rename(tmp, p)
}

func fileSum(p string) string {
	f, err := os.Open(p)
	if err != nil {
		return ""
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return ""
	}
	return hex.EncodeToString(h.Sum(nil))
}

func copyFile(src, dst string) error {
	b, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	return os.WriteFile(dst, b, 0o644)
}

func copyExe(src, dst string) error {
	b, err := os.ReadFile(src)
	if err != nil {
		return fmt.Errorf("the desktop host: %v", err)
	}
	os.Remove(dst)
	return os.WriteFile(dst, b, 0o755)
}

// MacHost writes the host for every Mac (MacArchs) at out, one universal
// binary.
func MacHost(out string, o HostOptions) error {
	work, err := os.MkdirTemp("", "asuka-mac-host-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(work)
	var parts []string
	for _, arch := range MacArchs {
		p := filepath.Join(work, arch, "asuka-host")
		if err := Host(cbuild.Target{OS: "darwin", Arch: arch}, p, o); err != nil {
			return err
		}
		parts = append(parts, p)
	}
	if err := lipo(parts, out); err != nil {
		return err
	}
	return os.Chmod(out, 0o755)
}
