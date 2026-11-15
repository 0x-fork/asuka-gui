package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"sort"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/toml"
)

// depManifest is what an app takes of a dependency's asuka.toml: the
// purpose strings and permissions its native code needs (a package that
// reads the camera says why in its manifest), which the app's own
// asuka.toml may change.
type depManifest struct {
	IOS struct {
		Usage           map[string]string `json:"usage"`
		BackgroundModes []string          `json:"backgroundModes"`
	} `json:"ios"`
	Android struct {
		Permissions []string `json:"permissions"`
	} `json:"android"`
}

// depManifests adds to c what the app's dependencies' manifests ask
// for (deps: package ID → directory).
func (c *appConfig) depManifests(deps map[string]string, u *appbuild.Uses) error {
	ids := make([]string, 0, len(deps))
	for id := range deps {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	var errs []error
	for _, id := range ids {
		b, err := os.ReadFile(filepath.Join(deps[id], configFile))
		if err != nil {
			continue
		}
		var m depManifest
		if err := toml.Unmarshal(b, &m); err != nil {
			errs = append(errs, fmt.Errorf("%s's %s: %v", id, configFile, err))
			continue
		}
		for k, v := range m.IOS.Usage {
			if c.IOS.Usage == nil {
				c.IOS.Usage = map[string]string{}
			}
			if c.IOS.Usage[k] == "" {
				c.IOS.Usage[k] = v
			}
		}
		for _, mode := range m.IOS.BackgroundModes {
			if !slices.Contains(c.IOS.BackgroundModes, mode) {
				c.IOS.BackgroundModes = append(c.IOS.BackgroundModes, mode)
			}
		}
		for _, p := range m.Android.Permissions {
			c.addPermission(p)
		}
	}
	return errors.Join(errs...)
}

// checkCatalogs checks the app's language catalogs (asuka.toml's i18n),
// which the compiler reads: their problems are printed, their errors
// the error.
func (c *appConfig) checkCatalogs() error {
	if c.I18n == nil {
		return nil
	}
	_, diags, err := c.loadCatalogs()
	if err != nil {
		return err
	}
	for _, d := range diags {
		fmt.Fprintln(os.Stderr, "asuka:", d)
	}
	if n := diags.Errors(); n > 0 {
		return fmt.Errorf("%d %s in the catalogs in %s", n, plural(n, "error", "errors"), c.I18n.Dir)
	}
	return nil
}
