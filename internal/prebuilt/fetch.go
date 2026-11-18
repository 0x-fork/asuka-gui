package prebuilt

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/ccache"
)

// DefaultURL is where a release's archives are: its GitHub release's
// downloads, {version} the CLI's.
const DefaultURL = "https://github.com/egoist/asuka/releases/download/v{version}"

// AssetName is the release asset of target's prebuilt set.
func AssetName(target, version string) string {
	return "asuka-prebuilt-" + target + "-" + version + ".tar.gz"
}

// Dir is where a package is kept in the cache: prebuilt/<its asset's
// name, without .tar.gz>.
func Dir(p Package) string {
	return filepath.Join(ccache.Dir("prebuilt"), strings.TrimSuffix(p.Asset, ".tar.gz"))
}

// Source is where a package is fetched from:
//
//   - ASUKA_PREBUILT_URL, a mirror: a URL (https://mirror.example/asuka,
//     {version} replaced by the release's version) or a local directory
//     (a path, or a file: URL) holding the release's archives, each
//     under its own name;
//   - else the release's on GitHub (DefaultURL).
//
// A mirror's requests carry ASUKA_PREBUILT_TOKEN, if set, as a bearer
// token (a URL's user:password@ is sent as basic authentication);
// GitHub's carry neither. A proxy is the environment's (HTTPS_PROXY,
// HTTP_PROXY, NO_PROXY). What's fetched is checked against the SHA-256
// the CLI pins, from wherever it came.
func Source(p Package) (src string, local bool) {
	base := os.Getenv("ASUKA_PREBUILT_URL")
	if base == "" {
		base = DefaultURL
	}
	base = strings.ReplaceAll(base, "{version}", p.Version)
	if u, err := url.Parse(base); err == nil && u.Scheme == "file" {
		return filepath.Join(filepath.FromSlash(fileURLPath(u)), p.Asset), true
	}
	if !strings.Contains(base, "://") {
		return filepath.Join(base, p.Asset), true
	}
	return strings.TrimSuffix(base, "/") + "/" + url.PathEscape(p.Asset), false
}

// fileURLPath is a file: URL's path: file:///C:/x is C:/x on Windows.
func fileURLPath(u *url.URL) string {
	p := u.Path
	if len(p) >= 3 && p[0] == '/' && p[2] == ':' {
		p = p[1:]
	}
	return p
}

var (
	fetchMu    sync.Mutex
	fetchLocks = map[string]*sync.Mutex{}
)

// Fetch makes sure p is in the cache and returns its directory: unpacked
// from its archive, fetched from its Source and checked against
// p.SHA256, which the CLI pins: a mirror can't give another.
func Fetch(p Package) (string, error) {
	dir := Dir(p)
	fetchMu.Lock()
	mu := fetchLocks[dir]
	if mu == nil {
		mu = &sync.Mutex{}
		fetchLocks[dir] = mu
	}
	fetchMu.Unlock()
	mu.Lock()
	defer mu.Unlock()
	if _, err := os.Stat(filepath.Join(dir, ".complete")); err == nil {
		ccache.Touch(dir)
		ccache.Hit("prebuilt")
		return dir, nil
	}
	ccache.Miss("prebuilt")
	if p.Asset == "" || p.Version == "" || p.SHA256 == "" {
		return "", fmt.Errorf("prebuilt: %q has no pinned archive and SHA-256", p.Asset)
	}
	src, _ := Source(p)
	size := ""
	if p.Size > 0 {
		size = ", " + formatSize(p.Size)
	}
	fmt.Fprintf(os.Stderr, "asuka: fetching %s (once%s) from %s\n", p.Asset, size, src)
	if err := os.MkdirAll(filepath.Dir(dir), 0o755); err != nil {
		return "", err
	}
	archive := ccache.TmpName(dir + ".tar.gz")
	defer os.Remove(archive)
	if err := download(p, archive); err != nil {
		return "", fmt.Errorf("prebuilt: %s from %s: %w", p.Asset, src, err)
	}
	tmp := ccache.TmpName(dir)
	if err := unpackFile(archive, tmp); err != nil {
		os.RemoveAll(tmp)
		return "", fmt.Errorf("prebuilt: %s: %v", p.Asset, err)
	}
	if err := os.WriteFile(filepath.Join(tmp, ".complete"), nil, 0o644); err != nil {
		os.RemoveAll(tmp)
		return "", err
	}
	os.RemoveAll(dir) // a half-written one (no .complete)
	if err := ccache.Commit(tmp, dir); err != nil {
		return "", err
	}
	return dir, nil
}

// ErrChecksum is a download that isn't the archive the CLI pins.
var ErrChecksum = errors.New("the archive isn't the one this asuka was released with (its SHA-256 differs)")

// download writes p's archive to file, checked against p.SHA256; a
// download that fails part way, or that a server refuses for a while
// (5xx, 429), is tried again, three times in all.
func download(p Package, file string) error {
	src, local := Source(p)
	var err error
	for attempt := 0; attempt < 3; attempt++ {
		if attempt > 0 {
			time.Sleep(time.Duration(attempt) * retryWait)
		}
		var retry bool
		if local {
			err = copyChecked(file, p.SHA256, func() (io.ReadCloser, error) { return os.Open(src) })
		} else {
			retry, err = getChecked(src, file, p.SHA256)
		}
		if err == nil || !retry {
			return err
		}
	}
	return err
}

// httpClient fetches archives: the environment's proxy (net/http's
// default transport's: ProxyFromEnvironment), redirects followed (a
// release's downloads redirect to its CDN).
var httpClient = &http.Client{Transport: http.DefaultTransport.(*http.Transport).Clone()}

// retryWait is how long a download waits before it's tried again (times
// the attempts so far).
var retryWait = 2 * time.Second

// idle is how long a download may go without a byte before it's given up.
var idle = 60 * time.Second

// getChecked downloads u to file, checked; retry is whether trying again
// may help.
func getChecked(u, file, sha string) (retry bool, err error) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", u, nil)
	if err != nil {
		return false, err
	}
	req.Header.Set("User-Agent", "asuka-cli")
	if t := os.Getenv("ASUKA_PREBUILT_TOKEN"); t != "" && os.Getenv("ASUKA_PREBUILT_URL") != "" {
		// Only a mirror's: net/http drops it on a redirect to another host.
		req.Header.Set("Authorization", "Bearer "+t)
	}
	timer := time.AfterFunc(idle, cancel)
	defer timer.Stop()
	resp, err := httpClient.Do(req)
	if err != nil {
		return true, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		retry := resp.StatusCode >= 500 || resp.StatusCode == http.StatusTooManyRequests
		return retry, fmt.Errorf("GET %s: %s", u, resp.Status)
	}
	err = copyChecked(file, sha, func() (io.ReadCloser, error) {
		return io.NopCloser(&idleReader{r: resp.Body, timer: timer}), nil
	})
	return err != nil && !errors.Is(err, ErrChecksum), err
}

// idleReader pushes its timer back with every read that gets something.
type idleReader struct {
	r     io.Reader
	timer *time.Timer
}

func (r *idleReader) Read(b []byte) (int, error) {
	n, err := r.r.Read(b)
	if n > 0 {
		r.timer.Reset(idle)
	}
	return n, err
}

// copyChecked writes what open reads to file, which is removed unless
// its SHA-256 is sha.
func copyChecked(file, sha string, open func() (io.ReadCloser, error)) error {
	r, err := open()
	if err != nil {
		return err
	}
	defer r.Close()
	f, err := os.Create(file)
	if err != nil {
		return err
	}
	h := sha256.New()
	_, err = io.Copy(io.MultiWriter(f, h), r)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil && hex.EncodeToString(h.Sum(nil)) != sha {
		err = ErrChecksum
	}
	if err != nil {
		os.Remove(file)
	}
	return err
}

func unpackFile(archive, dir string) error {
	f, err := os.Open(archive)
	if err != nil {
		return err
	}
	defer f.Close()
	return unpack(f, dir)
}

// unpack writes an archive's files, under its one top directory, into
// dir.
func unpack(r io.Reader, dir string) error {
	zr, err := gzip.NewReader(r)
	if err != nil {
		return err
	}
	tr := tar.NewReader(zr)
	for {
		h, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		name := path.Clean(strings.TrimPrefix(filepath.ToSlash(h.Name), "./"))
		if name == ".." || strings.HasPrefix(name, "../") || path.IsAbs(name) {
			return fmt.Errorf("the archive has a file outside it (%s)", h.Name)
		}
		// Everything is under the archive's top directory
		// (asuka-prebuilt-<target>-<version>/): it goes.
		if i := strings.IndexByte(name, '/'); i >= 0 {
			name = name[i+1:]
		} else {
			continue
		}
		if name == "" || name == "." || strings.HasPrefix(name, "../") || name == ".." || path.IsAbs(name) || strings.Contains(name, ":") {
			return fmt.Errorf("the archive has a file outside it (%s)", h.Name)
		}
		p := filepath.Join(dir, filepath.FromSlash(name))
		switch h.Typeflag {
		case tar.TypeDir:
			if err := os.MkdirAll(p, 0o755); err != nil {
				return err
			}
		case tar.TypeReg:
			if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
				return err
			}
			mode := os.FileMode(0o644)
			if h.Mode&0o111 != 0 {
				mode = 0o755
			}
			f, err := os.OpenFile(p, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode)
			if err != nil {
				return err
			}
			_, cerr := io.Copy(f, io.LimitReader(tr, h.Size))
			if err := f.Close(); cerr == nil {
				cerr = err
			}
			if cerr != nil {
				return cerr
			}
		default:
			// Links and the like: a set of libraries has none.
		}
	}
}

func sha256File(p string) (string, error) {
	f, err := os.Open(p)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func formatSize(n int64) string {
	switch {
	case n >= 1<<30:
		return fmt.Sprintf("%.1f GB", float64(n)/(1<<30))
	case n >= 1<<20:
		return fmt.Sprintf("%.0f MB", float64(n)/(1<<20))
	default:
		return fmt.Sprintf("%d KB", (n+1023)/1024)
	}
}

// Pack writes the files under src as a release's archive of them: a
// gzip compressed tar archive, everything under the top directory top,
// the same bytes for the same files (sorted, no times, owners or
// names of the packer's), so a release run again makes what it made.
func Pack(w io.Writer, src, top string) error {
	var files []string
	err := filepath.WalkDir(src, func(p string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if p == src {
			return nil
		}
		if d.Type()&os.ModeSymlink != 0 {
			return fmt.Errorf("%s: a symbolic link (an archive of libraries has none)", p)
		}
		files = append(files, p)
		return nil
	})
	if err != nil {
		return err
	}
	// WalkDir's order is lexical per directory: the same every time.
	zw, err := gzip.NewWriterLevel(w, gzip.BestCompression)
	if err != nil {
		return err
	}
	tw := tar.NewWriter(zw)
	for _, p := range files {
		rel, err := filepath.Rel(src, p)
		if err != nil {
			return err
		}
		st, err := os.Stat(p)
		if err != nil {
			return err
		}
		h := &tar.Header{Name: top + "/" + filepath.ToSlash(rel), Format: tar.FormatPAX, ModTime: time.Unix(0, 0)}
		if st.IsDir() {
			h.Typeflag, h.Name, h.Mode = tar.TypeDir, h.Name+"/", 0o755
			if err := tw.WriteHeader(h); err != nil {
				return err
			}
			continue
		}
		h.Typeflag, h.Size, h.Mode = tar.TypeReg, st.Size(), 0o644
		if st.Mode()&0o111 != 0 {
			h.Mode = 0o755
		}
		if err := tw.WriteHeader(h); err != nil {
			return err
		}
		f, err := os.Open(p)
		if err != nil {
			return err
		}
		_, err = io.Copy(tw, f)
		f.Close()
		if err != nil {
			return err
		}
	}
	if err := tw.Close(); err != nil {
		return err
	}
	return zw.Close()
}
