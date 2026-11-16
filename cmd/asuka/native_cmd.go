package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"

	"github.com/egoist/asuka/internal/nativegen"
)

// cmdNative is asuka native: generate, check, add. A library with native
// code is made by asuka new --lib --native.
func cmdNative(args []string) error {
	const use = `usage:
  asuka native generate [dir]          write the generated code of the module's native packages
  asuka native check [dir]             fail if a native package's generated code is stale (CI)
  asuka native add <project> [dir]     add a platform's project to a library: swift, android, web, desktop
A library with native code is made by asuka new --lib --native <dir>.`
	if len(args) == 0 {
		return errors.New(use)
	}
	switch args[0] {
	case "generate":
		return nativeGenerateCmd(args[1:], false)
	case "check":
		return nativeGenerateCmd(args[1:], true)
	case "new":
		return errors.New("asuka native new is asuka new --lib --native <dir> now: a native package is a library")
	case "add":
		return nativeAddCmd(args[1:])
	case "-h", "--help", "help":
		fmt.Println(use)
		return errHelp
	}
	return errors.New(use)
}

// moduleOf is the root of the module the directory is in.
func moduleOf(dir string) (string, error) {
	out, err := exec.Command("go", "-C", dir, "env", "GOMOD").Output()
	if err != nil {
		return "", err
	}
	mod := strings.TrimSpace(string(out))
	if mod == "" || mod == os.DevNull {
		return "", fmt.Errorf("%s isn't in a Go module", dir)
	}
	return filepath.Dir(mod), nil
}

func nativeGenerateCmd(args []string, check bool) error {
	name := "native generate"
	if check {
		name = "native check"
	}
	fs := newFlagSet(name, "[dir]")
	if check {
		fs.about = "Checks that the native package in dir, or else the native packages of dir's\nmodule (default: the working directory's), have their generated code current:\nfor CI."
	} else {
		fs.about = "Writes the generated code of the native package in dir, or else of the native\npackages of dir's module (default: the working directory's): their\nprojects' (the Swift's, the Kotlin's, the JavaScript's, the desktop's C). asuka dev and asuka build do it too; commit it."
	}
	if err := fs.parse(args); err != nil {
		return err
	}
	dir := fs.Arg(0)
	if dir == "" {
		dir = "."
	}
	// A native package's directory: it; else its module's.
	dirs := []string{mustAbs(dir)}
	if !hasDirectives(dir) {
		mod, err := moduleOf(dir)
		if err != nil {
			return err
		}
		if dirs, err = nativeDirs(mod); err != nil {
			return err
		}
	}
	var stale []string
	for _, d := range dirs {
		p, err := nativegen.LoadAsuka(d)
		if err != nil {
			return err
		}
		if p == nil {
			continue
		}
		if check {
			s, err := nativegen.Stale(p)
			if err != nil {
				return err
			}
			for _, f := range s {
				stale = append(stale, filepath.Join(rel(d), f))
			}
			continue
		}
		wrote, err := nativegen.Write(p)
		if err != nil {
			return err
		}
		for _, w := range wrote {
			fmt.Printf("asuka: wrote %s\n", filepath.Join(rel(d), w))
		}
	}
	if len(stale) > 0 {
		return fmt.Errorf("stale (asuka native generate):\n  %s", strings.Join(stale, "\n  "))
	}
	return nil
}

// nativeProjects are the platforms' projects asuka new --lib --native and
// asuka native add make.
var nativeProjects = []string{"swift", "android", "web", "desktop"}

// newLib makes a library in dir: its asuka.toml and its Asuka, a function
// and its test block; with native, a native module instead, and the
// platforms' projects (projects) that implement it.
func newLib(dir string, native bool, projects []string) error {
	if native {
		for _, p := range projects {
			if !slices.Contains(nativeProjects, p) {
				return fmt.Errorf("--projects: no project %q (%s)", p, strings.Join(nativeProjects, ", "))
			}
		}
	}
	name := goIdent(filepath.Base(mustAbs(dir)))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	toml := fmt.Sprintf("name = %q\nversion = \"0.1.0\"\n", name)
	if err := os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte(toml), 0o644); err != nil {
		return err
	}
	if !native {
		src := fmt.Sprintf(`// The %[1]s library: what its apps import (import "%[1]s" once it's a
// dependency: asuka add).

/// Greets name.
pub fn hello(name: string) -> string = "Hello, {name}"

test "greets" {
  expect(hello("Ada") == "Hello, Ada")
}
`, name)
		if err := os.WriteFile(filepath.Join(dir, name+".asuka"), []byte(src), 0o644); err != nil {
			return err
		}
		fmt.Printf("asuka: made %s\n\n  cd %s\n  asuka test    # its test blocks\n", dir, dir)
		return nil
	}
	t := upperCamelName(name)
	src := fmt.Sprintf(`// The %[1]s library: a native module, %[2]s, which each platform's own
// code implements (docs/native.md).

/// Greets in each platform's own code.
pub native interface %[2]s {
  /// Greets name.
  fn hello(name: string) -> string
}
`, name, t)
	if err := os.WriteFile(filepath.Join(dir, name+".asuka"), []byte(src), 0o644); err != nil {
		return err
	}
	for _, p := range projects {
		if err := addNativeProject(dir, p); err != nil {
			return err
		}
	}
	return generateNative(mustAbs(dir))
}

func nativeAddCmd(args []string) error {
	fs := newFlagSet("native add", "<project> [dir]")
	fs.about = "Adds a project to the native package in dir (default: the working directory):\n" + strings.Join(nativeProjects, ", ") + "."
	if err := fs.parse(args); err != nil {
		return err
	}
	project, dir := fs.Arg(0), fs.Arg(1)
	if !slices.Contains(nativeProjects, project) {
		return fmt.Errorf("usage: asuka native add <%s> [dir]", strings.Join(nativeProjects, "|"))
	}
	if dir == "" {
		dir = "."
	}
	if err := addNativeProject(dir, project); err != nil {
		return err
	}
	return generateNative(mustAbs(dir))
}

// addNativeProject writes a project's files for the native package in
// dir, implementing its first interface's hello (asuka new --lib
// --native's).
func addNativeProject(dir, project string) error {
	abs := mustAbs(dir)
	p, err := nativegen.LoadAsuka(abs)
	if err != nil {
		return err
	}
	if p == nil || len(p.Interfaces) == 0 {
		return fmt.Errorf("%s: no native interface (an Asuka package's native interface)", dir)
	}
	path, name := p.Path, p.Name
	t := p.Interfaces[0].Name
	write := func(rel, content string) error {
		p := filepath.Join(abs, rel)
		if exists(p) {
			return fmt.Errorf("%s exists", filepath.Join(dir, rel))
		}
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return err
		}
		fmt.Printf("asuka: wrote %s\n", filepath.Join(dir, rel))
		return os.WriteFile(p, []byte(content), 0o644)
	}
	switch project {
	case "swift":
		if err := write("Package.swift", fmt.Sprintf(`// swift-tools-version:5.9
// The %[1]s package's Swift, for iOS and macOS: the Asuka CLI builds it
// with the apps that import the package. Its dependencies (Swift
// packages) go here, as any package's.
import PackageDescription

let package = Package(
    name: "%[2]sNative",
    platforms: [.iOS("16.0"), .macOS("11.0")],
    products: [.library(name: "%[2]sNative", targets: ["%[2]sNative"])],
    dependencies: [
        .package(url: "https://github.com/egoist/asuka", from: "0.1.0"),
    ],
    targets: [
        .target(name: "%[2]sNative", dependencies: [.product(name: "Asuka", package: "asuka")], path: "swift"),
    ]
)
`, name, upperCamelName(name))); err != nil {
			return err
		}
		return write(filepath.Join("swift", t+".swift"), fmt.Sprintf(`// The %[1]s package's Swift: what it declares, implemented on iOS and
// macOS (%[3]s.asuka.swift, beside it, is what asuka native generates).
import Asuka
import Foundation

final class Swift%[2]s: %[2]s {
    func hello(name: String) throws -> String { "Hello, \(name), from Swift" }
}

func make%[2]s() throws -> any %[2]s { Swift%[2]s() }
`, name, t, upperCamelName(name)))
	case "android":
		pkg := nativegen.KotlinPackage(path)
		if err := write(filepath.Join("android", "build.gradle.kts"), fmt.Sprintf(`// The %[1]s package's Kotlin for Android: a library module the Asuka CLI
// builds into the apps that import the package. Its Kotlin is in its
// namespace; its dependencies (Maven's) go here, its permissions and
// services in src/main/AndroidManifest.xml.
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "%[2]s"
}

dependencies {
}
`, name, pkg)); err != nil {
			return err
		}
		return write(filepath.Join("android", "src", "main", "kotlin", filepath.Join(strings.Split(pkg, ".")...), t+".kt"), fmt.Sprintf(`// The %[1]s package's Kotlin: what it declares, implemented on Android
// (%[4]s.asuka.kt, beside it, is what asuka native generates).
package %[2]s

private class Kotlin%[3]s : %[3]s {
    override fun hello(name: String) = "Hello, $name, from Kotlin"
}

fun make%[3]s(): %[3]s = Kotlin%[3]s()
`, name, pkg, t, upperCamelName(name)))
	case "web":
		return write(filepath.Join("web", name+".js"), fmt.Sprintf(`// The %[1]s package's JavaScript: what it declares, implemented on the
// web (%[1]s.asuka.js, beside it, is what asuka native generates).

export function make%[2]s() {
  return {
    hello: (name) => `+"`Hello, ${name}, from the web`"+`,
  };
}
`, name, t))
	case "desktop":
		return write(filepath.Join(nativegen.DesktopDir, name+".c"), fmt.Sprintf(`// The %[1]s package's C: what it declares, implemented on Windows and
// Linux (%[1]s.asuka.h and .c, beside it, are what asuka native
// generates). A file named _windows.c or _linux.c is built for that
// platform only; #ifdef _WIN32 where one file differs.
#include "%[1]s.asuka.h"

#include <stdlib.h>

static char *hello(%[1]s_%[2]s *self, const char *name, asuka_error *err) {
#ifdef _WIN32
	return asuka_sprintf("Hello, %%s, from Windows", name);
#else
	return asuka_sprintf("Hello, %%s, from Linux", name);
#endif
}

// No release: the object is freed as the app lets it go.
static const %[1]s_%[2]s_vtable vt = {.hello = hello};

%[1]s_%[2]s *%[1]s_make_%[2]s(asuka_error *err) {
	%[1]s_%[2]s *it = malloc(sizeof *it);
	it->vt = &vt;
	return it;
}
`, name, t))
	}
	return nil
}

// goIdent is a directory's name as a package name.
func goIdent(s string) string {
	var b strings.Builder
	for _, r := range strings.ToLower(s) {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' && b.Len() > 0 {
			b.WriteRune(r)
		}
	}
	if b.Len() == 0 {
		return "native"
	}
	return b.String()
}

func upperCamelName(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
}

func mustAbs(p string) string {
	a, err := filepath.Abs(p)
	if err != nil {
		return p
	}
	return a
}
