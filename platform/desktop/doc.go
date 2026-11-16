// Package desktop holds the C the Windows and Linux hosts share
// (RFC 0008): include/asuka_desktop.h, the contract between a host and
// the C its app links; src/native.c, the native packages' registry;
// asuka_linux.h and asuka_windows.h (src/linux.c, src/windows.c), what
// packages' Linux and Windows C share. Its
// tests build them with a test host (test/host.c) and run Asuka on the
// core, with no Go at run time.
package desktop

import (
	"embed"
	"os"
	"path/filepath"
	"sync"

	"github.com/egoist/asuka/internal/srcdir"
)

// Sources are the shared C, embedded in the asuka CLI so an installed CLI
// builds desktop apps without the repository.
//
//go:embed include src
var Sources embed.FS

var (
	once sync.Once
	dir  string
	err  error
)

// Dir is the directory of the shared C: ASUKA_DESKTOP if set; the
// repository's platform/desktop when the CLI runs from a checkout; else
// the embedded copy, written once into the user's cache.
func Dir() (string, error) {
	once.Do(func() {
		dir, err = srcdir.Tree{Env: "ASUKA_DESKTOP", Sub: filepath.Join("platform", "desktop"), Valid: valid, Files: Sources, Cache: "desktop-src"}.Dir()
	})
	return dir, err
}

func valid(d string) bool {
	_, err := os.Stat(filepath.Join(d, "include", "asuka_desktop.h"))
	return err == nil
}

// Registry is the registry's C source in dir (Dir's).
func Registry(dir string) string { return filepath.Join(dir, "src", "native.c") }

// Shared are the shared C sources in dir (Dir's): the registry, and what
// packages' C shares on Linux (asuka_linux.h: linux.c) and on Windows
// (asuka_windows.h: windows.c), each built for its platform only.
func Shared(dir string) []string {
	src := filepath.Join(dir, "src")
	return []string{filepath.Join(src, "native.c"), filepath.Join(src, "linux.c"), filepath.Join(src, "linux_syms.c"), filepath.Join(src, "windows.c"), filepath.Join(src, "fetch.c"), filepath.Join(src, "asuka_json.c"), filepath.Join(src, "webview2.c"),
		filepath.Join(dir, "include", "asuka_linux.h"), filepath.Join(dir, "include", "asuka_linux.def"), filepath.Join(dir, "include", "asuka_windows.h")}
}

// HostShared are the C the Windows and Linux hosts share themselves
// (desktop.h's dsk_: JSON, the app's settings and directories, files, the
// window and route state, flyouts' places, animated images, Asuka's own
// words), and its headers, in dir (Dir's).
func HostShared(dir string) []string {
	src := filepath.Join(dir, "src")
	var out []string
	for _, f := range []string{"app.c", "files.c", "json.c", "state.c", "flyout.c", "anim.c", "words.c", "words_gen.c", "words.h", "words_gen.h"} {
		out = append(out, filepath.Join(src, f))
	}
	return append(out, filepath.Join(dir, "include", "desktop.h"), filepath.Join(dir, "include", "anim.h"))
}

// Libs are the system libraries the shared C links with on goos.
func Libs(goos string) []string {
	if goos == "windows" {
		return []string{"-lwinhttp", "-lole32", "-loleaut32", "-luuid", "-lshell32", "-luser32", "-lgdi32", "-ladvapi32", "-lwindowscodecs"}
	}
	return nil
}

// Include is the include directory of asuka_desktop.h in dir.
func Include(dir string) string { return filepath.Join(dir, "include") }
