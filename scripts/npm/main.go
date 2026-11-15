// Command npm builds the asuka CLI for every platform and lays out its
// npm packages: @asuka/cli (the launcher, npm/cli) and one
// @asuka/cli-<os>-<cpu> per build, with only its binary. With -publish
// it publishes them (npm publish, with the user's npm credentials;
// --provenance on CI), platform packages first.
//
//	go run ./scripts/npm -version 0.1.0 [-out dist/npm] [-publish]
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
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
	var dirs []string
	deps := map[string]string{}
	for _, p := range platforms {
		name := "@asuka/cli-" + p.os + "-" + p.cpu
		dir := filepath.Join(*out, "cli-"+p.os+"-"+p.cpu)
		exe := "asuka"
		if p.goos == "windows" {
			exe += ".exe"
		}
		bin := filepath.Join(dir, "bin", exe)
		fmt.Println("building", name)
		cmd := exec.Command("go", "build", "-trimpath", "-ldflags", "-s -w -X main.version="+*version, "-o", bin, "./cmd/asuka")
		cmd.Env = append(os.Environ(), "GOOS="+p.goos, "GOARCH="+p.goarch, "CGO_ENABLED=0")
		cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
		if err := cmd.Run(); err != nil {
			fail(fmt.Sprintf("npm: building %s: %v", name, err))
		}
		writeJSON(filepath.Join(dir, "package.json"), map[string]any{
			"name":        name,
			"version":     *version,
			"description": "The asuka CLI for " + p.os + " " + p.cpu + " (installed by @asuka/cli)",
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
