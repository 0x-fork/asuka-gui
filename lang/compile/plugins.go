package compile

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/plugin"
	"github.com/egoist/asuka/std"
)

// PluginEnv is the environment a runner needs for the plugins m uses: a
// standard library package with a plugin manifest (std/sqlite's
// asuka-plugin.toml) whose builtins m calls gets each of its libraries'
// files for this computer (the plugin's build, or its published file,
// downloaded once into the user's cache) as ASUKA_LIB_<NAME>, where the
// core looks first (core/src/sqlite). A library that can't be had is
// left out, its reason printed: the program then fails where it needs it.
func PluginEnv(m *bytecode.Module) []string {
	pkgs := map[string]bool{}
	for _, b := range m.Builtins {
		if pkg, _, ok := strings.Cut(b, "."); ok {
			pkgs[pkg] = true
		}
	}
	var env []string
	for pkg := range pkgs {
		env = append(env, LibraryEnv("asuka/"+pkg)...)
	}
	return env
}

// LibraryEnv is PluginEnv for the standard library package imported as
// path (asuka/sqlite): nothing when it isn't a plugin.
func LibraryEnv(path string) []string {
	dir, ok := std.Package(path)
	if !ok {
		return nil
	}
	data, err := os.ReadFile(filepath.Join(dir, plugin.ManifestFile))
	if err != nil {
		return nil
	}
	man, err := plugin.Parse(data)
	if err != nil {
		return nil
	}
	var env []string
	target := plugin.Target(runtime.GOOS, runtime.GOARCH)
	for _, l := range man.Libraries {
		name := "ASUKA_LIB_" + strings.ToUpper(l.Name)
		if os.Getenv(name) != "" {
			continue
		}
		p, err := plugin.FilePath(dir, l, target)
		if err != nil {
			// The program fails where it needs the library; this says why.
			fmt.Fprintf(os.Stderr, "asuka: %v\n", err)
			continue
		}
		env = append(env, name+"="+p)
	}
	return env
}
