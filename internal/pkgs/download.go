package pkgs

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/ccache"
)

// CDownload is a [c.download.<name>] table: an archive the C builds
// from (a library's sources, a vendor's static libraries) that isn't
// kept in the project, fetched by the first build that needs it, checked
// against its pinned hash and unpacked into the build cache. Paths in
// [c] that start with $<name> are in it (docs/c.md, Downloaded sources).
type CDownload struct {
	URL    string `json:"url"`    // https://…, a .zip, .tar.gz or .tgz
	SHA256 string `json:"sha256"` // the archive's, hex
	// Strip is how many leading directories its paths lose (1: an
	// archive whose files are all in one top directory).
	Strip int `json:"strip"`
}

var (
	downloadName = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_-]*$`)
	sha256Hex    = regexp.MustCompile(`^[0-9a-f]{64}$`)
)

// check reports what's wrong in d, [c.download.<name>].
func (d CDownload) check(name string) error {
	t := "[c.download." + name + "]"
	switch {
	case !downloadName.MatchString(name):
		return fmt.Errorf("%s: %q isn't a name (letters, digits, _ and -)", t, name)
	case !strings.HasPrefix(d.URL, "https://"):
		return fmt.Errorf("%s url: %q isn't an https URL", t, d.URL)
	case archiveKind(d.URL) == "":
		return fmt.Errorf("%s url: %q isn't a .zip, .tar.gz or .tgz", t, d.URL)
	case !sha256Hex.MatchString(d.SHA256):
		return fmt.Errorf("%s sha256: %q isn't a SHA-256 (64 lowercase hex digits; shasum -a 256 prints it)", t, d.SHA256)
	case d.Strip < 0:
		return fmt.Errorf("%s strip: %d is negative", t, d.Strip)
	}
	return nil
}

func archiveKind(u string) string {
	u = strings.ToLower(u)
	if i := strings.IndexAny(u, "?#"); i >= 0 {
		u = u[:i]
	}
	switch {
	case strings.HasSuffix(u, ".zip"):
		return "zip"
	case strings.HasSuffix(u, ".tar.gz"), strings.HasSuffix(u, ".tgz"):
		return "tgz"
	}
	return ""
}

// Dir is where d's files are once fetched: the cache's
// downloads/<sha256> (and its strip), shared by every project.
func (d CDownload) Dir() string {
	name := d.SHA256
	if d.Strip > 0 {
		name += "-" + strconv.Itoa(d.Strip)
	}
	return filepath.Join(ccache.Dir("downloads"), name)
}

// Fetched reports whether d's files are in the cache.
func (d CDownload) Fetched() bool {
	_, err := os.Stat(filepath.Join(d.Dir(), ".complete"))
	return err == nil
}

var (
	fetchMu    sync.Mutex
	fetchLocks = map[string]*sync.Mutex{}
)

// Fetch makes sure d's files are in the cache: downloaded, checked
// against d.SHA256 and unpacked, the first time.
func (d CDownload) Fetch() error {
	dir := d.Dir()
	fetchMu.Lock()
	mu := fetchLocks[dir]
	if mu == nil {
		mu = &sync.Mutex{}
		fetchLocks[dir] = mu
	}
	fetchMu.Unlock()
	mu.Lock()
	defer mu.Unlock()
	if d.Fetched() {
		ccache.Touch(dir)
		return nil
	}
	fmt.Fprintf(os.Stderr, "asuka: downloading %s (once)\n", d.URL)
	data, err := download(d.URL)
	if err != nil {
		return err
	}
	sum := sha256.Sum256(data)
	if got := hex.EncodeToString(sum[:]); got != d.SHA256 {
		return fmt.Errorf("%s: its SHA-256 is %s, not the %s asuka.toml pins", d.URL, got, d.SHA256)
	}
	tmp := ccache.TmpName(dir)
	if err := unpackArchive(archiveKind(d.URL), data, d.Strip, tmp); err != nil {
		os.RemoveAll(tmp)
		return fmt.Errorf("%s: %v", d.URL, err)
	}
	if err := os.WriteFile(filepath.Join(tmp, ".complete"), nil, 0o644); err != nil {
		os.RemoveAll(tmp)
		return err
	}
	os.RemoveAll(dir) // a half-written one (no .complete)
	return ccache.Commit(tmp, dir)
}

func download(u string) ([]byte, error) {
	req, err := http.NewRequest("GET", u, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "asuka-cli")
	resp, err := (&http.Client{Timeout: 10 * time.Minute}).Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("GET %s: %s", u, resp.Status)
	}
	return io.ReadAll(io.LimitReader(resp.Body, 2<<30))
}

// unpackArchive writes an archive's files into dir, each path's first
// strip directories dropped (a file with no more is left out).
func unpackArchive(kind string, data []byte, strip int, dir string) error {
	write := func(name string, mode os.FileMode, r io.Reader) error {
		name = path.Clean(strings.TrimPrefix(filepath.ToSlash(name), "./"))
		if name == ".." || strings.HasPrefix(name, "../") || path.IsAbs(name) || strings.Contains(name, ":") {
			return fmt.Errorf("the archive has a file outside it (%s)", name)
		}
		parts := strings.Split(name, "/")
		if len(parts) <= strip {
			return nil
		}
		p := filepath.Join(dir, filepath.FromSlash(strings.Join(parts[strip:], "/")))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return err
		}
		perm := os.FileMode(0o644)
		if mode&0o111 != 0 {
			perm = 0o755
		}
		f, err := os.OpenFile(p, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, perm)
		if err != nil {
			return err
		}
		_, cerr := io.Copy(f, r)
		if err := f.Close(); cerr == nil {
			cerr = err
		}
		return cerr
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	switch kind {
	case "zip":
		zr, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
		if err != nil {
			return err
		}
		for _, f := range zr.File {
			if f.FileInfo().IsDir() {
				continue
			}
			rc, err := f.Open()
			if err != nil {
				return err
			}
			err = write(f.Name, f.Mode(), rc)
			rc.Close()
			if err != nil {
				return err
			}
		}
		return nil
	case "tgz":
		gz, err := gzip.NewReader(bytes.NewReader(data))
		if err != nil {
			return err
		}
		tr := tar.NewReader(gz)
		for {
			h, err := tr.Next()
			if err == io.EOF {
				return nil
			}
			if err != nil {
				return err
			}
			if h.Typeflag != tar.TypeReg {
				continue
			}
			if err := write(h.Name, os.FileMode(h.Mode), io.LimitReader(tr, h.Size)); err != nil {
				return err
			}
		}
	}
	return fmt.Errorf("not a .zip, .tar.gz or .tgz")
}

// Path is p, a path in [c] (relative to dir, the project; or $<name>/…,
// in a download), as a path on disk, and the download it's in (nil when
// none). It doesn't fetch the download.
func (c *CConfig) Path(dir, p string) (string, *CDownload, error) {
	if !strings.HasPrefix(p, "$") {
		return filepath.Join(dir, filepath.FromSlash(p)), nil, nil
	}
	name, rest, _ := strings.Cut(p[1:], "/")
	d, ok := c.Download[name]
	if !ok {
		return "", nil, fmt.Errorf("%q: there's no [c.download.%s]", p, name)
	}
	return filepath.Join(d.Dir(), filepath.FromSlash(rest)), &d, nil
}

// FetchFor fetches the downloads s's paths name ($<name>/…).
func (c *CConfig) FetchFor(s CSettings) error {
	for _, name := range c.downloadsOf(append(append(append([]string{}, s.Sources...), s.Include...), s.Libraries...)) {
		if err := c.Download[name].Fetch(); err != nil {
			return err
		}
	}
	return nil
}

// downloadsOf are the downloads ps name, each once.
func (c *CConfig) downloadsOf(ps []string) []string {
	var out []string
	seen := map[string]bool{}
	for _, p := range ps {
		if !strings.HasPrefix(p, "$") {
			continue
		}
		name, _, _ := strings.Cut(p[1:], "/")
		if _, ok := c.Download[name]; ok && !seen[name] {
			seen[name] = true
			out = append(out, name)
		}
	}
	return out
}
