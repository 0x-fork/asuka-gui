package pkgs

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha1"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Package is what's published: from a package's asuka.toml.
type Package struct {
	Name         string
	Version      string
	Description  string
	License      string
	Repository   string
	Dependencies map[string]string
}

// skipped are what a package's tarball never carries.
var skipped = map[string]bool{".git": true, "node_modules": true, "dist": true, "build": true, ".DS_Store": true, "asuka.lock": true}

// Pack makes a package's tarball from dir: its files under package/, as
// npm lays them out, with a package.json the registry reads (name,
// version, dependencies, "asuka": the manifest), in a fixed order and
// with fixed times, so the same sources make the same tarball.
func Pack(dir string, p Package) (tgz []byte, pkgJSON map[string]any, err error) {
	if _, err := ParseVersion(p.Version); err != nil {
		return nil, nil, err
	}
	for k, v := range p.Dependencies {
		if strings.HasPrefix(v, "file:") {
			return nil, nil, fmt.Errorf("%s = %q: a published package can't depend on a local directory", k, v)
		}
	}
	pkgJSON = map[string]any{
		"name":    p.Name,
		"version": p.Version,
		"asuka":   "asuka.toml",
	}
	if p.Description != "" {
		pkgJSON["description"] = p.Description
	}
	if p.License != "" {
		pkgJSON["license"] = p.License
	}
	if p.Repository != "" {
		pkgJSON["repository"] = p.Repository
	}
	if len(p.Dependencies) > 0 {
		pkgJSON["dependencies"] = p.Dependencies
	}
	var files []string
	err = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if skipped[d.Name()] && path != dir {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if !d.IsDir() && d.Type().IsRegular() && d.Name() != "package.json" {
			rel, _ := filepath.Rel(dir, path)
			files = append(files, filepath.ToSlash(rel))
		}
		return nil
	})
	if err != nil {
		return nil, nil, err
	}
	sort.Strings(files)
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	tw := tar.NewWriter(zw)
	mtime := time.Date(1985, 10, 26, 8, 15, 0, 0, time.UTC) // npm's fixed time
	write := func(name string, body []byte) error {
		if err := tw.WriteHeader(&tar.Header{Name: "package/" + name, Mode: 0o644, Size: int64(len(body)), ModTime: mtime, Typeflag: tar.TypeReg}); err != nil {
			return err
		}
		_, err := tw.Write(body)
		return err
	}
	pj, _ := json.MarshalIndent(pkgJSON, "", "  ")
	if err := write("package.json", append(pj, '\n')); err != nil {
		return nil, nil, err
	}
	for _, f := range files {
		b, err := os.ReadFile(filepath.Join(dir, filepath.FromSlash(f)))
		if err != nil {
			return nil, nil, err
		}
		if err := write(f, b); err != nil {
			return nil, nil, err
		}
	}
	if err := tw.Close(); err != nil {
		return nil, nil, err
	}
	if err := zw.Close(); err != nil {
		return nil, nil, err
	}
	return buf.Bytes(), pkgJSON, nil
}

// Publish uploads a packed package to its registry, as npm publish does
// (one PUT of the package's document with the tarball attached), with
// the user's token for the registry. otp is a one-time password for an
// account with two-factor authentication ("" for none).
func (c *Client) Publish(pkgJSON map[string]any, tgz []byte, tag, otp string) error {
	name, _ := pkgJSON["name"].(string)
	version, _ := pkgJSON["version"].(string)
	reg := c.RC.RegistryFor(name)
	token := c.RC.TokenFor(reg)
	if token == "" {
		return fmt.Errorf("no token for %s: log in (npm login --registry %s) or set NPM_TOKEN", reg, reg)
	}
	if tag == "" {
		tag = "latest"
	}
	base := name
	if i := strings.LastIndexByte(base, '/'); i >= 0 {
		base = base[i+1:]
	}
	file := base + "-" + version + ".tgz"
	sum := sha1.Sum(tgz)
	meta := map[string]any{}
	for k, v := range pkgJSON {
		meta[k] = v
	}
	meta["_id"] = name + "@" + version
	meta["dist"] = map[string]any{
		"integrity": Integrity(tgz),
		"shasum":    hex.EncodeToString(sum[:]),
		"tarball":   reg + name + "/-/" + file,
	}
	doc := map[string]any{
		"_id":       name,
		"name":      name,
		"dist-tags": map[string]string{tag: version},
		"versions":  map[string]any{version: meta},
		"access":    "public",
		"_attachments": map[string]any{
			file: map[string]any{"content_type": "application/octet-stream", "data": base64.StdEncoding.EncodeToString(tgz), "length": len(tgz)},
		},
	}
	if d, ok := pkgJSON["description"]; ok {
		doc["description"] = d
	}
	body, err := json.Marshal(doc)
	if err != nil {
		return err
	}
	u := reg + strings.Replace(url.PathEscape(name), "%40", "@", 1)
	req, err := http.NewRequest("PUT", u, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+token)
	req.Header.Set("User-Agent", "asuka")
	if otp != "" {
		req.Header.Set("npm-otp", otp)
	}
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	msg, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
	switch {
	case resp.StatusCode == 401 && strings.Contains(string(msg), "one-time"):
		return fmt.Errorf("publishing %s@%s needs a one-time password (asuka pkg publish -otp <code>)", name, version)
	case resp.StatusCode == 403 && strings.Contains(string(msg), "previously published"):
		return fmt.Errorf("%s@%s is already published: bump the version in asuka.toml", name, version)
	case resp.StatusCode >= 300:
		return fmt.Errorf("publishing %s@%s: %s %s", name, version, resp.Status, strings.TrimSpace(string(msg)))
	}
	return nil
}
