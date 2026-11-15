// asuka/store on Windows and Linux: a JSON file in the app's data
// directory, written whole on each change (small values only).
package store

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
)

type filePrefs struct {
	mu   sync.Mutex
	path string
	m    map[string]string
}

func newPrefs() (Prefs, error) {
	dir := os.Getenv("ASUKA_DATA")
	if dir == "" {
		d, err := os.UserConfigDir()
		if err != nil {
			return nil, err
		}
		dir = filepath.Join(d, filepath.Base(os.Args[0]))
	}
	p := &filePrefs{path: filepath.Join(dir, "store.json"), m: map[string]string{}}
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
	p.m[key] = value
	return p.write()
}

func (p *filePrefs) Remove(key string) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	delete(p.m, key)
	return p.write()
}

func (p *filePrefs) write() error {
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
	return os.Rename(tmp, p.path)
}
