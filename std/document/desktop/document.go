// asuka/document on the desktop: the files, read and written on the
// call's goroutine; a write is a temporary file renamed over the file.
package document

import (
	"context"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
)

type files struct{}

func newFiles() (Files, error) { return files{}, nil }

func (files) Read(ctx context.Context, path string) ([]byte, error) { return os.ReadFile(path) }

func (files) Write(ctx context.Context, path string, data []byte) error {
	mode := fs.FileMode(0o644)
	if st, err := os.Stat(path); err == nil {
		mode = st.Mode().Perm()
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), "."+filepath.Base(path)+".*")
	if err != nil {
		return err
	}
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		os.Remove(tmp.Name())
		return err
	}
	if err := tmp.Close(); err != nil {
		os.Remove(tmp.Name())
		return err
	}
	os.Chmod(tmp.Name(), mode)
	if err := os.Rename(tmp.Name(), path); err != nil {
		os.Remove(tmp.Name())
		return err
	}
	return nil
}

func (files) Modified(ctx context.Context, path string) (int, error) {
	st, err := os.Stat(path)
	if errors.Is(err, fs.ErrNotExist) {
		return 0, nil
	}
	if err != nil {
		return 0, err
	}
	return int(st.ModTime().UnixMilli()), nil
}
