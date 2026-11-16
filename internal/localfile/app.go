package localfile

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/egoist/asuka/internal/boot"
)

// AppName is the running desktop app's name: asuka.toml's, else its
// executable's, as the desktop hosts name it (Credential Manager's
// targets, WebView2's data folder, the caches).
func AppName() string {
	if boot.Name != "" {
		return boot.Name
	}
	exe, err := os.Executable()
	if err != nil {
		return "asuka"
	}
	return strings.TrimSuffix(strings.TrimSuffix(filepath.Base(exe), ".exe"), ".EXE")
}

// AppID is its application ID: asuka.toml's, else one made of its name,
// as the Linux host names its GApplication (a libsecret schema, the
// caches there).
func AppID() string {
	if boot.ID != "" && validID(boot.ID) {
		return boot.ID
	}
	var b strings.Builder
	for _, r := range AppName() {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			b.WriteRune(r)
		case r == '-' || r == '_':
			b.WriteRune('_')
		}
	}
	id := "dev.asuka.app." + b.String()
	if b.Len() == 0 || !validID(id) {
		return "dev.asuka.App"
	}
	return id
}

// validID is g_application_id_is_valid's rule: two or more elements of
// letters, digits, _ and -, none starting with a digit, at most 255 bytes.
func validID(id string) bool {
	if len(id) == 0 || len(id) > 255 || !strings.Contains(id, ".") {
		return false
	}
	for _, e := range strings.Split(id, ".") {
		if e == "" || e[0] >= '0' && e[0] <= '9' {
			return false
		}
		for _, r := range e {
			if !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '_' || r == '-') {
				return false
			}
		}
	}
	return true
}

// AppCacheDir is the app's folder in the user's caches, as its desktop
// host has it: by its ID on Linux, its name elsewhere.
func AppCacheDir() string {
	if runtime.GOOS == "linux" {
		return CacheDir(AppID())
	}
	return CacheDir(AppName())
}
