// Command release makes a release of the asuka CLI: everything it ships,
// as the assets of its GitHub release.
//
//   - With -prebuilt, a directory of what asuka prebuilt build made for
//     each of its targets (the release workflow's), each target's set
//     packed as one archive, asuka-prebuilt-<target>-<version>.tar.gz,
//     and their manifest (internal/prebuilt: each archive's SHA-256, each
//     library's path and SHA-256) embedded in the CLI.
//   - The CLI built for every platform it runs on, asuka-<os>-<cpu>
//     (.exe on Windows), with that manifest.
//   - The install scripts (scripts/install.sh, scripts/install.ps1).
//   - SHA256SUMS, of all of them.
//
// Everything goes into -out (dist/release). Without -upload that's all: a
// dry run. With -upload it's the release v<version>'s (gh, its tag
// already pushed): created as a draft if there's none, each asset
// uploaded that isn't there or differs from this one (its digest), then
// published, so a run again after a failed upload finishes the same
// release, and the latest release is never one with assets missing.
//
//	go run ./scripts/release -version 0.3.0 [-prebuilt dist/prebuilt] [-out dist/release] [-upload]
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/prebuilt"
)

// platforms are what the CLI is built for: os and cpu name its asset.
var platforms = []struct{ goos, goarch, os, cpu string }{
	{"darwin", "arm64", "darwin", "arm64"}, // Apple silicon: an Intel Mac isn't one Asuka develops on
	{"linux", "arm64", "linux", "arm64"},
	{"linux", "amd64", "linux", "x64"},
	{"windows", "arm64", "windows", "arm64"},
	{"windows", "amd64", "windows", "x64"},
}

// installScripts are attached to each release beside the CLI.
var installScripts = []string{"scripts/install.sh", "scripts/install.ps1"}

func main() {
	version := flag.String("version", "", "the release's version (semver, without the v)")
	out := flag.String("out", filepath.Join("dist", "release"), "where the assets go")
	pre := flag.String("prebuilt", "", "a directory of asuka prebuilt build's output, a directory per target")
	manifest := flag.String("manifest", filepath.Join("internal", "prebuilt", "manifest.json"), "the CLI's embedded manifest, which -prebuilt's is written to while the CLI builds")
	upload := flag.Bool("upload", false, "create (or finish) the GitHub release v<version> with the assets")
	repo := flag.String("repo", "egoist/asuka", "the GitHub repository")
	flag.Parse()
	if *version == "" {
		fail(errors.New("-version is required"))
	}
	*version = strings.TrimPrefix(*version, "v")
	if err := os.RemoveAll(*out); err != nil {
		fail(err)
	}
	if err := os.MkdirAll(*out, 0o755); err != nil {
		fail(err)
	}
	var m *prebuilt.Manifest
	if *pre != "" {
		var err error
		if m, err = packPrebuilt(*pre, *out, *version); err != nil {
			fail(err)
		}
		if *upload {
			if err := checkTargets(m); err != nil {
				fail(err)
			}
		}
	} else if *upload {
		fail(errors.New("-upload needs -prebuilt: a release carries every target's libraries"))
	}
	if err := buildCLI(*out, *version, *manifest, m); err != nil {
		fail(err)
	}
	for _, s := range installScripts {
		if err := copyFile(s, filepath.Join(*out, filepath.Base(s))); err != nil {
			fail(err)
		}
	}
	if err := writeSums(*out); err != nil {
		fail(err)
	}
	if !*upload {
		fmt.Println("the release's assets are in", *out, "(add -upload to publish them)")
		return
	}
	if err := publish(*repo, "v"+*version, *out); err != nil {
		fail(err)
	}
}

// packPrebuilt packs each target's directory in dir (asuka prebuilt
// build's: its files and prebuilt.json) into out, and returns their
// manifest, which pins each archive's SHA-256; it's written to out too.
func packPrebuilt(dir, out, version string) (*prebuilt.Manifest, error) {
	es, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	m := &prebuilt.Manifest{Version: version, Packages: map[string]prebuilt.Package{}, Archives: map[string]prebuilt.File{}}
	for _, e := range es {
		if !e.IsDir() {
			continue
		}
		src := filepath.Join(dir, e.Name())
		b, err := os.ReadFile(filepath.Join(src, "prebuilt.json"))
		if err != nil {
			return nil, fmt.Errorf("%s: %v (asuka prebuilt build's output?)", src, err)
		}
		var c struct {
			Name     string                   `json:"name"`
			Compiler string                   `json:"compiler"`
			Swift    string                   `json:"swift"`
			Files    map[string]prebuilt.File `json:"files"`
			Maven    string                   `json:"maven"`
		}
		if err := json.Unmarshal(b, &c); err != nil {
			return nil, fmt.Errorf("%s: %v", src, err)
		}
		if c.Name == "" {
			return nil, fmt.Errorf("%s: prebuilt.json names no target", src)
		}
		name := prebuilt.AssetName(c.Name, version)
		dst := filepath.Join(out, name)
		sum, size, err := pack(src, dst, strings.TrimSuffix(name, ".tar.gz"))
		if err != nil {
			return nil, fmt.Errorf("packing %s: %v", src, err)
		}
		m.Packages[c.Name] = prebuilt.Package{Asset: name, Version: version, SHA256: sum, Size: size, Compiler: c.Compiler, Swift: c.Swift}
		for k, f := range c.Files {
			f.Package = c.Name
			m.Archives[k] = f
		}
		if c.Maven != "" {
			m.Maven = &prebuilt.File{Package: c.Name, Path: c.Maven}
		}
		fmt.Printf("packed %s (%d MB)\n", name, size>>20)
	}
	b, err := manifestJSON(m)
	if err != nil {
		return nil, err
	}
	if err := os.WriteFile(filepath.Join(out, "prebuilt-manifest.json"), b, 0o644); err != nil {
		return nil, err
	}
	fmt.Printf("the manifest: %d archives, %d libraries (%s)\n", len(m.Packages), len(m.Archives), strings.Join(sortedKeys(m.Packages), ", "))
	return m, nil
}

// pack writes src's archive to dst, returning its SHA-256 and size.
func pack(src, dst, top string) (string, int64, error) {
	f, err := os.Create(dst)
	if err != nil {
		return "", 0, err
	}
	h := sha256.New()
	cw := &countWriter{w: io.MultiWriter(f, h)}
	err = prebuilt.Pack(cw, src, top)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	return hex.EncodeToString(h.Sum(nil)), cw.n, err
}

type countWriter struct {
	w io.Writer
	n int64
}

func (c *countWriter) Write(b []byte) (int, error) {
	n, err := c.w.Write(b)
	c.n += int64(n)
	return n, err
}

// checkTargets makes sure m has every target asuka prebuilt targets
// names: a release without one would ship a CLI that compiles its
// libraries on every first build.
func checkTargets(m *prebuilt.Manifest) error {
	cmd := exec.Command("go", "run", "./cmd/asuka", "prebuilt", "targets")
	cmd.Stderr = os.Stderr
	b, err := cmd.Output()
	if err != nil {
		return fmt.Errorf("asuka prebuilt targets: %v", err)
	}
	var missing []string
	for _, t := range strings.Fields(string(b)) {
		if _, ok := m.Packages[t]; !ok {
			missing = append(missing, t)
		}
	}
	if len(missing) > 0 {
		return fmt.Errorf("-prebuilt has no build for %s", strings.Join(missing, ", "))
	}
	return nil
}

func manifestJSON(m *prebuilt.Manifest) ([]byte, error) {
	b, err := json.MarshalIndent(m, "", "  ")
	return append(b, '\n'), err
}

// buildCLI builds the CLI for every platform into out, with m embedded
// (written to manifest while it builds, then put back: a dry run leaves
// the checkout as it was).
func buildCLI(out, version, manifest string, m *prebuilt.Manifest) error {
	if m != nil {
		old, err := os.ReadFile(manifest)
		if err != nil {
			return err
		}
		b, err := manifestJSON(m)
		if err != nil {
			return err
		}
		if err := os.WriteFile(manifest, b, 0o644); err != nil {
			return err
		}
		defer os.WriteFile(manifest, old, 0o644)
	}
	for _, p := range platforms {
		name := cliAsset(p.os, p.cpu)
		fmt.Println("building", name)
		cmd := exec.Command("go", "build", "-trimpath", "-ldflags", "-s -w -X main.version="+version, "-o", filepath.Join(out, name), "./cmd/asuka")
		cmd.Env = append(os.Environ(), "GOOS="+p.goos, "GOARCH="+p.goarch, "CGO_ENABLED=0")
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("building %s: %v", name, err)
		}
	}
	return nil
}

// cliAsset is the CLI's asset for os and cpu.
func cliAsset(os, cpu string) string {
	if os == "windows" {
		return "asuka-" + os + "-" + cpu + ".exe"
	}
	return "asuka-" + os + "-" + cpu
}

// writeSums writes out/SHA256SUMS: every other file's, as sha256sum
// prints them.
func writeSums(out string) error {
	names, err := assets(out)
	if err != nil {
		return err
	}
	var b bytes.Buffer
	for _, n := range names {
		if n == "SHA256SUMS" {
			continue
		}
		s, err := fileSHA256(filepath.Join(out, n))
		if err != nil {
			return err
		}
		fmt.Fprintf(&b, "%s  %s\n", s, n)
	}
	return os.WriteFile(filepath.Join(out, "SHA256SUMS"), b.Bytes(), 0o644)
}

// assets are the files in out, sorted.
func assets(out string) ([]string, error) {
	es, err := os.ReadDir(out)
	if err != nil {
		return nil, err
	}
	var names []string
	for _, e := range es {
		if e.Type().IsRegular() {
			names = append(names, e.Name())
		}
	}
	sort.Strings(names)
	return names, nil
}

// remoteAsset is a release's asset, as gh release view --json assets
// says.
type remoteAsset struct {
	Name   string `json:"name"`
	Digest string `json:"digest"` // sha256:<hex>
	State  string `json:"state"`
}

// toUpload is which of the local assets (name → SHA-256) the release
// needs: those it hasn't, or has other bytes of, or didn't finish
// uploading. SHA256SUMS, if it's one, comes last: an installer that
// finds it finds the rest.
func toUpload(local map[string]string, remote []remoteAsset) []string {
	have := map[string]remoteAsset{}
	for _, a := range remote {
		have[a.Name] = a
	}
	var out []string
	sums := false
	for name, sum := range local {
		r, ok := have[name]
		if ok && r.State == "uploaded" && r.Digest == "sha256:"+sum {
			continue
		}
		if name == "SHA256SUMS" {
			sums = true
			continue
		}
		out = append(out, name)
	}
	sort.Strings(out)
	if sums {
		out = append(out, "SHA256SUMS")
	}
	return out
}

// publish creates or finishes the release tag of repo with out's files.
func publish(repo, tag, out string) error {
	pre := strings.Contains(tag, "-")
	view := func() (draft bool, remote []remoteAsset, found bool, err error) {
		var stderr bytes.Buffer
		cmd := exec.Command("gh", "release", "view", tag, "-R", repo, "--json", "isDraft,assets")
		cmd.Stderr = &stderr
		b, err := cmd.Output()
		if err != nil {
			if strings.Contains(stderr.String(), "release not found") {
				return false, nil, false, nil
			}
			return false, nil, false, fmt.Errorf("gh release view %s: %v: %s", tag, err, stderr.String())
		}
		var r struct {
			IsDraft bool          `json:"isDraft"`
			Assets  []remoteAsset `json:"assets"`
		}
		if err := json.Unmarshal(b, &r); err != nil {
			return false, nil, false, err
		}
		return r.IsDraft, r.Assets, true, nil
	}
	draft, remote, found, err := view()
	if err != nil {
		return err
	}
	if !found {
		args := []string{"release", "create", tag, "-R", repo, "--draft", "--verify-tag", "--generate-notes", "--title", tag}
		if pre {
			args = append(args, "--prerelease")
		}
		if err := gh(args...); err != nil {
			return err
		}
		draft = true
	}
	names, err := assets(out)
	if err != nil {
		return err
	}
	local := map[string]string{}
	for _, n := range names {
		if local[n], err = fileSHA256(filepath.Join(out, n)); err != nil {
			return err
		}
	}
	up := toUpload(local, remote)
	if !draft && len(up) > 0 {
		fmt.Printf("%s is published already; replacing %s\n", tag, strings.Join(up, ", "))
	}
	// SHA256SUMS on its own, after the rest.
	if n := len(up); n > 0 && up[n-1] == "SHA256SUMS" {
		if err := uploadFiles(repo, tag, out, up[:n-1]); err != nil {
			return err
		}
		up = up[n-1:]
	}
	if err := uploadFiles(repo, tag, out, up); err != nil {
		return err
	}
	// Every asset there, as this run made it, before it's published.
	_, remote, _, err = view()
	if err != nil {
		return err
	}
	if left := toUpload(local, remote); len(left) > 0 {
		return fmt.Errorf("%s: after uploading, the release still lacks %s: run again", tag, strings.Join(left, ", "))
	}
	if draft {
		args := []string{"release", "edit", tag, "-R", repo, "--draft=false"}
		if !pre {
			args = append(args, "--latest")
		}
		if err := gh(args...); err != nil {
			return err
		}
	}
	fmt.Printf("released %s: https://github.com/%s/releases/tag/%s\n", tag, repo, tag)
	return nil
}

func uploadFiles(repo, tag, out string, names []string) error {
	if len(names) == 0 {
		return nil
	}
	args := []string{"release", "upload", tag, "-R", repo, "--clobber"}
	for _, n := range names {
		args = append(args, filepath.Join(out, n))
	}
	fmt.Println("uploading", strings.Join(names, ", "))
	return gh(args...)
}

func gh(args ...string) error {
	cmd := exec.Command("gh", args...)
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("gh %s: %v", strings.Join(args[:2], " "), err)
	}
	return nil
}

func fileSHA256(p string) (string, error) {
	f, err := os.Open(p)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func copyFile(src, dst string) error {
	b, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	return os.WriteFile(dst, b, 0o644)
}

func sortedKeys[V any](m map[string]V) []string {
	var out []string
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "release:", err)
	os.Exit(1)
}
