package pkgs

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/gzip"
	"crypto/sha1"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
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
)

// DefaultRegistry is npm's.
const DefaultRegistry = "https://registry.npmjs.org/"

// NPMRC is what the user's and the project's .npmrc say: the registry
// (per scope too) and the tokens, as npm reads them.
type NPMRC struct {
	Registry string            // the default registry
	Scopes   map[string]string // "@acme" → its registry
	Tokens   map[string]string // "//registry.example.com/" → its token
}

// LoadNPMRC reads ~/.npmrc then dir's .npmrc (later wins), and
// NPM_TOKEN for the default registry. ${VAR} in values is the
// environment's.
func LoadNPMRC(dir string) NPMRC {
	rc := NPMRC{Registry: DefaultRegistry, Scopes: map[string]string{}, Tokens: map[string]string{}}
	if home, err := os.UserHomeDir(); err == nil {
		rc.read(filepath.Join(home, ".npmrc"))
	}
	if dir != "" {
		rc.read(filepath.Join(dir, ".npmrc"))
	}
	if r := os.Getenv("ASUKA_REGISTRY"); r != "" {
		rc.Registry = withSlash(r)
	}
	if t := os.Getenv("NPM_TOKEN"); t != "" {
		if _, ok := rc.Tokens[nerf(rc.Registry)]; !ok {
			rc.Tokens[nerf(rc.Registry)] = t
		}
	}
	return rc
}

func (rc *NPMRC) read(p string) {
	f, err := os.Open(p)
	if err != nil {
		return
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		if line == "" || line[0] == '#' || line[0] == ';' {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		k, v = strings.TrimSpace(k), os.ExpandEnv(strings.Trim(strings.TrimSpace(v), `"`))
		switch {
		case k == "registry":
			rc.Registry = withSlash(v)
		case strings.HasPrefix(k, "@") && strings.HasSuffix(k, ":registry"):
			rc.Scopes[strings.TrimSuffix(k, ":registry")] = withSlash(v)
		case strings.HasPrefix(k, "//") && strings.HasSuffix(k, ":_authToken"):
			rc.Tokens[strings.TrimSuffix(k, ":_authToken")] = v
		}
	}
}

func withSlash(s string) string {
	if !strings.HasSuffix(s, "/") {
		s += "/"
	}
	return s
}

// nerf is a registry URL without its scheme, as .npmrc keys tokens.
func nerf(u string) string {
	p, err := url.Parse(u)
	if err != nil {
		return u
	}
	return "//" + p.Host + withSlash(p.Path)
}

// RegistryFor is the registry a package (by its name) is fetched from.
func (rc NPMRC) RegistryFor(name string) string {
	if strings.HasPrefix(name, "@") {
		if scope, _, ok := strings.Cut(name, "/"); ok {
			if r, ok := rc.Scopes[scope]; ok {
				return r
			}
		}
	}
	return rc.Registry
}

// TokenFor is the token for a registry URL's requests.
func (rc NPMRC) TokenFor(u string) string {
	n := nerf(u)
	for k, v := range rc.Tokens {
		if strings.HasPrefix(n, withSlash(k)) || n == withSlash(k) {
			return v
		}
	}
	return ""
}

// Packument is a package's metadata from the registry, what we read of
// it.
type Packument struct {
	Name     string                 `json:"name"`
	DistTags map[string]string      `json:"dist-tags"`
	Versions map[string]PackageMeta `json:"versions"`
}

// PackageMeta is a version's metadata.
type PackageMeta struct {
	Name    string `json:"name"`
	Version string `json:"version"`
	// Asuka is the path of the package's asuka.toml in its tarball; set
	// for Asuka packages (asuka pkg publish).
	Asuka        string            `json:"asuka"`
	Dependencies map[string]string `json:"dependencies"`
	Deprecated   string            `json:"deprecated"`
	Dist         struct {
		Tarball   string `json:"tarball"`
		Integrity string `json:"integrity"` // sha512-<base64>
		Shasum    string `json:"shasum"`    // sha1 hex, older packages
	} `json:"dist"`
}

// Client fetches from registries.
type Client struct {
	RC   NPMRC
	HTTP *http.Client
	mu   sync.Mutex
	docs map[string]*Packument
}

// NewClient is a client with rc's registries and tokens.
func NewClient(rc NPMRC) *Client {
	return &Client{RC: rc, HTTP: &http.Client{Timeout: 60 * time.Second}, docs: map[string]*Packument{}}
}

func (c *Client) get(u string, accept string) ([]byte, error) {
	req, err := http.NewRequest("GET", u, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "asuka")
	if accept != "" {
		req.Header.Set("Accept", accept)
	}
	if t := c.RC.TokenFor(u); t != "" {
		req.Header.Set("Authorization", "Bearer "+t)
	}
	var last error
	for attempt := 0; attempt < 3; attempt++ {
		resp, err := c.HTTP.Do(req)
		if err != nil {
			last = err
			time.Sleep(time.Duration(attempt+1) * 300 * time.Millisecond)
			continue
		}
		b, err := io.ReadAll(resp.Body)
		resp.Body.Close()
		switch {
		case err != nil:
			last = err
		case resp.StatusCode == 404:
			return nil, errNotFound
		case resp.StatusCode == 401 || resp.StatusCode == 403:
			return nil, fmt.Errorf("%s: %s (log in with npm login, or set a token in .npmrc)", u, resp.Status)
		case resp.StatusCode >= 500 || resp.StatusCode == 429:
			last = fmt.Errorf("%s: %s", u, resp.Status)
		case resp.StatusCode != 200:
			return nil, fmt.Errorf("%s: %s", u, resp.Status)
		default:
			return b, nil
		}
		time.Sleep(time.Duration(attempt+1) * 500 * time.Millisecond)
	}
	return nil, last
}

var errNotFound = errors.New("not found")

// Packument fetches a package's metadata (once per client).
func (c *Client) Packument(name string) (*Packument, error) {
	c.mu.Lock()
	if d, ok := c.docs[name]; ok {
		c.mu.Unlock()
		return d, nil
	}
	c.mu.Unlock()
	u := c.RC.RegistryFor(name) + strings.Replace(url.PathEscape(name), "%40", "@", 1)
	b, err := c.get(u, "application/json")
	if errors.Is(err, errNotFound) {
		return nil, fmt.Errorf("no package %s on %s", name, c.RC.RegistryFor(name))
	}
	if err != nil {
		return nil, err
	}
	var d Packument
	if err := json.Unmarshal(b, &d); err != nil {
		return nil, fmt.Errorf("%s: %v", u, err)
	}
	c.mu.Lock()
	c.docs[name] = &d
	c.mu.Unlock()
	return &d, nil
}

// Versions are the package's published versions.
func (d *Packument) VersionList() []Version {
	var vs []Version
	for s := range d.Versions {
		if v, err := ParseVersion(s); err == nil {
			vs = append(vs, v)
		}
	}
	return vs
}

// Tarball downloads a version's tarball and checks it against its
// integrity.
func (c *Client) Tarball(m PackageMeta) ([]byte, error) {
	b, err := c.get(m.Dist.Tarball, "")
	if err != nil {
		return nil, err
	}
	if err := checkIntegrity(b, m.Dist.Integrity, m.Dist.Shasum); err != nil {
		return nil, fmt.Errorf("%s@%s: %v", m.Name, m.Version, err)
	}
	return b, nil
}

// Integrity is b's SRI hash, as npm records it.
func Integrity(b []byte) string {
	h := sha512.Sum512(b)
	return "sha512-" + base64.StdEncoding.EncodeToString(h[:])
}

func checkIntegrity(b []byte, integrity, shasum string) error {
	for _, f := range strings.Fields(integrity) {
		if strings.HasPrefix(f, "sha512-") {
			if f != Integrity(b) {
				return errors.New("the download doesn't match its integrity hash")
			}
			return nil
		}
	}
	if shasum != "" {
		h := sha1.Sum(b)
		if hex.EncodeToString(h[:]) != shasum {
			return errors.New("the download doesn't match its checksum")
		}
		return nil
	}
	return errors.New("the registry gave no hash to check the download against")
}

// Extract writes a package tarball's files (under its top directory,
// "package/" on npm) into dir. It refuses paths that leave dir, and
// links.
func Extract(tgz []byte, dir string) error {
	zr, err := gzip.NewReader(bytes.NewReader(tgz))
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
		if h.Typeflag != tar.TypeReg && h.Typeflag != tar.TypeRegA && h.Typeflag != tar.TypeDir {
			continue // links, devices: not in packages
		}
		name := path.Clean(h.Name)
		if i := strings.IndexByte(name, '/'); i >= 0 {
			name = name[i+1:] // the tarball's top directory
		} else if h.Typeflag == tar.TypeDir {
			continue
		}
		if name == "" || name == "." || strings.HasPrefix(name, "../") || path.IsAbs(name) {
			continue
		}
		p := filepath.Join(dir, filepath.FromSlash(name))
		if h.Typeflag == tar.TypeDir {
			if err := os.MkdirAll(p, 0o755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return err
		}
		f, err := os.OpenFile(p, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
		if err != nil {
			return err
		}
		_, err = io.Copy(f, io.LimitReader(tr, 1<<30))
		f.Close()
		if err != nil {
			return err
		}
	}
}
