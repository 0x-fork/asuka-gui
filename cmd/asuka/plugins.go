package main

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/egoist/asuka/plugin"
)

// Plugins (package plugin): the packages among an app's dependencies with
// a manifest (asuka-plugin.toml) bring native libraries, which a build
// ships with the app for its platform, where the host finds them: in
// Contents/Frameworks on macOS, beside the .exe on Windows, in lib/<exe>
// of the prefix on Linux, as a framework each in an iOS app's Frameworks,
// beside libasuka.so in each of an APK's ABIs, and in lib/ of a web
// build's host files. The files are the plugins' checkout's builds, or
// the published ones, downloaded once into the user's cache.

// appPlugin is a plugin an app uses: its directory and manifest.
type appPlugin struct {
	dir string
	m   *plugin.Manifest
}

// pluginLib is a plugin's library's file for a target.
type pluginLib struct {
	appPlugin
	lib    plugin.Library
	file   plugin.File
	target string
}

// appPlugins are the plugins of the app: its own directory's manifest,
// if it has one, its dependencies' (and their packages'), and the
// standard library packages' it imports (asuka/sqlite's).
func appPlugins(app *asukaApp) ([]appPlugin, error) {
	dirs := []string{app.dir}
	for _, d := range app.packageDirs() {
		dirs = append(dirs, d)
	}
	slices.Sort(dirs)
	var ps []appPlugin
	for _, d := range slices.Compact(dirs) {
		data, err := os.ReadFile(filepath.Join(d, plugin.ManifestFile))
		if err != nil {
			continue
		}
		m, err := plugin.Parse(data)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", d, err)
		}
		ps = append(ps, appPlugin{dir: d, m: m})
	}
	return ps, nil
}

// appLibs are the libraries of the app's plugins for target
// (plugin.Targets): what its build ships.
func appLibs(app *asukaApp, target string) ([]pluginLib, error) {
	ps, err := appPlugins(app)
	if err != nil {
		return nil, err
	}
	return pluginLibs(ps, target)
}

// pluginLibs are the libraries of plugins ps for target. Every platform
// puts them in one place, by their files' names, so no two may share one.
func pluginLibs(ps []appPlugin, target string) ([]pluginLib, error) {
	var libs []pluginLib
	byName := map[string]pluginLib{}
	for _, p := range ps {
		for _, l := range p.m.Libraries {
			f, ok := l.Files[target]
			if !ok {
				return nil, fmt.Errorf("plugin %s: library %s has no file for %s", p.m.Name, l.Name, target)
			}
			if o, ok := byName[f.Name]; ok {
				return nil, fmt.Errorf("plugins %s and %s both have a file %s for %s", o.m.Name, p.m.Name, f.Name, target)
			}
			byName[f.Name] = pluginLib{appPlugin: p, lib: l, file: f, target: target}
			libs = append(libs, byName[f.Name])
		}
	}
	return libs, nil
}

// path is the library's file on this machine, downloaded first if need
// be (saying so).
func (l pluginLib) path() (string, error) {
	if !plugin.Cached(l.dir, l.lib, l.target) {
		fmt.Printf("asuka: downloading %s, plugin %s's, for %s\n", l.file.Name, l.m.Name, l.target)
	}
	return plugin.FilePath(l.dir, l.lib, l.target)
}

// read is the library's bytes.
func (l pluginLib) read() ([]byte, error) {
	p, err := l.path()
	if err != nil {
		return nil, err
	}
	return os.ReadFile(p)
}

// write writes the library to path.
func (l pluginLib) write(path string) error {
	data, err := l.read()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o755)
}

// writeLibs writes each library into dir under its file's name.
func writeLibs(libs []pluginLib, dir string) error {
	for _, l := range libs {
		if err := l.write(filepath.Join(dir, l.file.Name)); err != nil {
			return err
		}
	}
	return nil
}

// pluginEnv is the environment that points the core at the app's plugins'
// libraries for target, in a run that isn't a build (asuka dev on a
// desktop): ASUKA_LIB_<NAME>=path, each downloaded first if need be.
func pluginEnv(app *asukaApp, target string) ([]string, error) {
	libs, err := appLibs(app, target)
	if err != nil {
		return nil, err
	}
	var env []string
	for _, l := range libs {
		p, err := l.path()
		if err != nil {
			return nil, err
		}
		env = append(env, "ASUKA_LIB_"+strings.ToUpper(l.lib.Name)+"="+p)
	}
	return env, nil
}
