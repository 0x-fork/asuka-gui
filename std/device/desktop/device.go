// asuka/device on Windows and Linux (macOS runs the package's Swift):
// the system's open dialog for the pickers (their files copied to the
// app's caches, as a phone's pickers give them), the system's browser,
// the clipboard. The platform is touched on the main thread (native.Post,
// native.OnMain), where its answers come and changed is called; the
// goroutine of a call waits for them.
package device

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"mime"
	"net/url"
	"os"
	"path/filepath"

	"github.com/egoist/asuka/internal/localfile"
	"github.com/egoist/asuka/native"
)

type platform struct{}

func newPlatform() (Platform, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return platform{}, nil
}

// wait runs start on the main thread and waits for what it gives done,
// or ctx.
func wait[T any](ctx context.Context, start func(done func(T))) (T, error) {
	ch := make(chan T, 1)
	native.Post(func() { start(func(v T) { ch <- v }) })
	select {
	case v := <-ch:
		return v, nil
	case <-ctx.Done():
		var zero T
		return zero, ctx.Err()
	}
}

// webURL is an error unless u is an http(s) URL.
func webURL(u string) error {
	p, err := url.Parse(u)
	if err != nil || (p.Scheme != "https" && p.Scheme != "http") {
		return fmt.Errorf("device: not a web URL: %s", u)
	}
	return nil
}

// cacheDir is a folder of the app's caches.
func cacheDir(name string) string { return filepath.Join(localfile.AppCacheDir(), name) }

// copyPicked copies the files into dir, each in a folder of its own (so
// its name stays its own).
func copyPicked(dir string, paths []string) ([]File, error) {
	out := []File{}
	for _, src := range paths {
		into := filepath.Join(dir, randomName())
		if err := os.MkdirAll(into, 0o755); err != nil {
			return nil, err
		}
		to := filepath.Join(into, filepath.Base(src))
		size, err := copyFile(src, to)
		if err != nil {
			return nil, err
		}
		out = append(out, File{Path: to, Name: filepath.Base(src), Type: mime.TypeByExtension(filepath.Ext(src)), Size: int(size)})
	}
	return out, nil
}

func copyFile(src, dst string) (int64, error) {
	in, err := os.Open(src)
	if err != nil {
		return 0, err
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		return 0, err
	}
	n, err := io.Copy(out, in)
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	return n, err
}

func randomName() string {
	var b [12]byte
	rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

// writeCache writes data into a new file of the app's caches' folder
// name, with extension ext: its path.
func writeCache(name, ext string, data []byte) (string, error) {
	dir := cacheDir(name)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	path := filepath.Join(dir, randomName()+"."+ext)
	return path, os.WriteFile(path, data, 0o644)
}

// photos are the paths of picked files.
func photos(files []File) []string {
	paths := []string{}
	for _, f := range files {
		paths = append(paths, f.Path)
	}
	return paths
}
