// Command npm builds the asuka CLI for every platform and lays out its
// npm packages: @egoist/asuka (the launcher, npm/cli) and one
// @egoist/asuka-<os>-<cpu> per build, with only its binary. With -publish
// it publishes them (npm publish, with the user's npm credentials;
// --provenance on CI), platform packages first.
//
// It builds the desktop host (cmd/asuka-host, every desktop app's
// executable) for each desktop target first, into <out>/hosts, which the
// release uploads beside the CLI's binaries: each CLI is built with their
// SHA-256 (internal/appbuild's hostSums), and fetches and checks the one
// a build needs.
//
//	go run ./scripts/npm -version 0.1.0 [-out dist/npm] [-publish]
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

var platforms = []struct{ goos, goarch, os, cpu string }{
	{"darwin", "arm64", "darwin", "arm64"},
	{"darwin", "amd64", "darwin", "x64"},
	{"linux", "arm64", "linux", "arm64"},
	{"linux", "amd64", "linux", "x64"},
	{"windows", "arm64", "win32", "arm64"},
	{"windows", "amd64", "win32", "x64"},
}

func main() {
	version := flag.String("version", "", "the release's version (semver)")
	out := flag.String("out", filepath.Join("dist", "npm"), "where the packages go")
	publish := flag.Bool("publish", false, "npm publish them")
	tag := flag.String("tag", "latest", "the dist-tag to publish under")
	flag.Parse()
	if *version == "" {
		fail("npm: -version is required")
	}
	if err := os.RemoveAll(*out); err != nil {
		fail(err.Error())
	}
	// The Go desktop hosts (macOS's and Linux's: Windows's is C, built
	// with each app from the CLI's embedded sources), and their hashes
	// for the CLI.
	var sums []string
	hosts := filepath.Join(*out, "hosts")
	for _, p := range platforms {
		if p.goos == "windows" {
			continue
		}
		name := "asuka-host-" + p.goos + "-" + p.goarch
		ld := "-s -w -buildid="
		bin := filepath.Join(hosts, name)
		fmt.Println("building", name)
		cmd := exec.Command("go", "build", "-trimpath", "-ldflags", ld, "-o", bin, "./cmd/asuka-host")
		cmd.Env = append(os.Environ(), "GOOS="+p.goos, "GOARCH="+p.goarch, "CGO_ENABLED=0")
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			fail(fmt.Sprintf("npm: building %s: %v", name, err))
		}
		b, err := os.ReadFile(bin)
		if err != nil {
			fail(err.Error())
		}
		sum := sha256.Sum256(b)
		sums = append(sums, p.goos+"-"+p.goarch+"="+hex.EncodeToString(sum[:]))
	}
	cliFlags := "-s -w -X main.version=" + *version + " -X github.com/egoist/asuka/internal/appbuild.hostSums=" + strings.Join(sums, ",")
	var dirs []string
	deps := map[string]string{}
	for _, p := range platforms {
		name := "@egoist/asuka-" + p.os + "-" + p.cpu
		dir := filepath.Join(*out, "cli-"+p.os+"-"+p.cpu)
		exe := "asuka"
		if p.goos == "windows" {
			exe += ".exe"
		}
		bin := filepath.Join(dir, "bin", exe)
		fmt.Println("building", name)
		cmd := exec.Command("go", "build", "-trimpath", "-ldflags", cliFlags, "-o", bin, "./cmd/asuka")
		cmd.Env = append(os.Environ(), "GOOS="+p.goos, "GOARCH="+p.goarch, "CGO_ENABLED=0")
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			fail(fmt.Sprintf("npm: building %s: %v", name, err))
		}
		writeJSON(filepath.Join(dir, "package.json"), map[string]any{
			"name":        name,
			"version":     *version,
			"description": "The asuka CLI for " + p.os + " " + p.cpu + " (installed by @egoist/asuka)",
			"license":     "MIT",
			"repository":  "github:egoist/asuka",
			"os":          []string{p.os},
			"cpu":         []string{p.cpu},
			"files":       []string{"bin"},
		})
		deps[name] = *version
		dirs = append(dirs, dir)
	}
	// The launcher: npm/cli with the version and its dependencies set.
	main := filepath.Join(*out, "cli")
	if err := os.MkdirAll(filepath.Join(main, "bin"), 0o755); err != nil {
		fail(err.Error())
	}
	b, err := os.ReadFile(filepath.Join("npm", "cli", "bin", "asuka.js"))
	if err != nil {
		fail(err.Error())
	}
	if err := os.WriteFile(filepath.Join(main, "bin", "asuka.js"), b, 0o755); err != nil {
		fail(err.Error())
	}
	var pkg map[string]any
	b, err = os.ReadFile(filepath.Join("npm", "cli", "package.json"))
	if err != nil {
		fail(err.Error())
	}
	if err := json.Unmarshal(b, &pkg); err != nil {
		fail(err.Error())
	}
	pkg["version"] = *version
	pkg["optionalDependencies"] = deps
	writeJSON(filepath.Join(main, "package.json"), pkg)
	dirs = append(dirs, main)
	if !*publish {
		fmt.Println("packages in", *out, "(add -publish to publish them)")
		return
	}
	for _, d := range dirs {
		args := []string{"publish", "--access", "public", "--tag", *tag}
		if os.Getenv("CI") != "" {
			args = append(args, "--provenance")
		}
		cmd := exec.Command("npm", args...)
		cmd.Dir = d
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			fail(fmt.Sprintf("npm publish %s: %v", d, err))
		}
	}
}

func writeJSON(path string, v any) {
	b, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		fail(err.Error())
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		fail(err.Error())
	}
	if err := os.WriteFile(path, append(b, '\n'), 0o644); err != nil {
		fail(err.Error())
	}
}

func fail(msg string) {
	fmt.Fprintln(os.Stderr, msg)
	os.Exit(1)
}
