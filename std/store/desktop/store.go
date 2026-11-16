// asuka/store on Windows and Linux: a JSON file in the app's data
// directory (internal/appdir), written whole a moment after a change, so
// a run of changes is one write (small values only). Flush writes now.
package store

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/appdir"
)

// delay is how long a change waits for the next before it's written.
const delay = 200 * time.Millisecond

type filePrefs struct {
	mu    sync.Mutex
	path  string
	m     map[string]string
	timer *time.Timer
	dirty bool
}

func newPrefs() (Prefs, error) {
	p := &filePrefs{path: filepath.Join(appdir.Data(), "store.json"), m: map[string]string{}}
	if b, err := os.ReadFile(p.path); err == nil {
		json.Unmarshal(b, &p.m)
	}
	return p, nil
}

func (p *filePrefs) Get(key string) (*string, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	if v, ok := p.m[key]; ok {
		return &v, nil
	}
	return nil, nil
}

func (p *filePrefs) Set(key, value string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if old, ok := p.m[key]; ok && old == value {
		return nil
	}
	p.m[key] = value
	p.changed()
	return nil
}

func (p *filePrefs) Remove(key string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if _, ok := p.m[key]; !ok {
		return nil
	}
	delete(p.m, key)
	p.changed()
	return nil
}

func (p *filePrefs) Flush() error {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.timer != nil {
		p.timer.Stop()
		p.timer = nil
	}
	return p.write()
}

func (p *filePrefs) Dir() (string, error) { return filepath.Dir(p.path), nil }

// changed schedules the write (p.mu held).
func (p *filePrefs) changed() {
	p.dirty = true
	if p.timer != nil {
		return
	}
	p.timer = time.AfterFunc(delay, func() {
		p.mu.Lock()
		defer p.mu.Unlock()
		p.timer = nil
		p.write()
	})
}

// write writes the file when something changed (p.mu held).
func (p *filePrefs) write() error {
	if !p.dirty {
		return nil
	}
	b, err := json.Marshal(p.m)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p.path), 0o755); err != nil {
		return err
	}
	tmp := p.path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, p.path); err != nil {
		return err
	}
	p.dirty = false
	return nil
}
