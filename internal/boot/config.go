package boot

import (
	"encoding/json"
	"os"
	"path/filepath"
)

// ConfigFile is a desktop app's settings from its asuka.toml, which the
// Asuka CLI writes beside the executable (cmd/asuka-host, the same for
// every app), or in a Mac app's Resources: what the host shows of the app
// (its name, its accent) and how it runs (its ID, its URL scheme).
const ConfigFile = "asuka-app.json"

// Config is ConfigFile's content: the variables of this package the CLI
// set with -ldflags -X when it built each app's Go code.
type Config struct {
	Name              string `json:"name,omitempty"`
	ID                string `json:"id,omitempty"`
	Version           string `json:"version,omitempty"`
	Build             string `json:"build,omitempty"`
	Accent            string `json:"accent,omitempty"`
	Scheme            string `json:"scheme,omitempty"`
	FileTypes         string `json:"fileTypes,omitempty"`
	Lang              string `json:"lang,omitempty"`
	Direction         string `json:"direction,omitempty"`
	Background        bool   `json:"background,omitempty"`
	KeepRunning       bool   `json:"keepRunning,omitempty"`
	MultipleInstances bool   `json:"multipleInstances,omitempty"`
	// The updates' feed, the public key their signatures are checked
	// with, and the build's target (asuka/sdk/update).
	UpdateFeed   string `json:"updateFeed,omitempty"`
	UpdateKey    string `json:"updateKey,omitempty"`
	UpdateTarget string `json:"updateTarget,omitempty"`
}

// UpdateFeed, UpdateKey and UpdateTarget are asuka.toml's updates for a
// desktop build (ConfigFile's): asuka/sdk/update's feed, the key that
// signs its updates and the build's target.
var UpdateFeed, UpdateKey, UpdateTarget string

// Apply sets this package's variables from c, leaving those it doesn't
// set as they are.
func (c *Config) Apply() {
	set := func(dst *string, v string) {
		if v != "" {
			*dst = v
		}
	}
	set(&Name, c.Name)
	set(&ID, c.ID)
	set(&Version, c.Version)
	set(&Build, c.Build)
	set(&Accent, c.Accent)
	set(&Scheme, c.Scheme)
	set(&FileTypes, c.FileTypes)
	set(&Lang, c.Lang)
	set(&Direction, c.Direction)
	set(&UpdateFeed, c.UpdateFeed)
	set(&UpdateKey, c.UpdateKey)
	set(&UpdateTarget, c.UpdateTarget)
	if c.Background {
		Background = "1"
	}
	if c.KeepRunning {
		KeepRunning = true
	}
	if c.MultipleInstances {
		MultipleInstances = "1"
	}
}

// LoadConfig reads ConfigFile, from ASUKA_APP_CONFIG when it's set, else
// beside the executable or in a Mac app's Resources, and applies it. An
// app without one keeps the defaults.
func LoadConfig() error {
	p := os.Getenv("ASUKA_APP_CONFIG")
	if p == "" {
		exe, err := os.Executable()
		if err != nil {
			return nil
		}
		dir := filepath.Dir(exe)
		for _, c := range []string{filepath.Join(dir, ConfigFile), filepath.Join(dir, "..", "Resources", ConfigFile), filepath.Join(dir, "..", "share", filepath.Base(exe), ConfigFile)} {
			if _, err := os.Stat(c); err == nil {
				p = c
				break
			}
		}
		if p == "" {
			return nil
		}
	}
	b, err := os.ReadFile(p)
	if err != nil {
		return err
	}
	var c Config
	if err := json.Unmarshal(b, &c); err != nil {
		return err
	}
	c.Apply()
	return nil
}
