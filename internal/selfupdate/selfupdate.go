// Package selfupdate keeps the asuka CLI up to date from its GitHub
// releases: a check, at most once a day and never in a command's way,
// for a newer release than the one running (Due, Check, Notice), and asuka
// upgrade's download of it, checked against the release's SHA256SUMS,
// in place of the running executable (Upgrade).
package selfupdate

import (
	"bufio"
	"bytes"
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
	"runtime"
	"strings"

	"github.com/egoist/asuka/internal/update"
)

// Releases is the CLI's releases: Releases/latest redirects to the
// latest one's page (.../tag/v1.2.3), which has no API rate limit, and
// Releases/download/v<version>/<asset> is an asset.
var Releases = "https://github.com/egoist/asuka/releases"

var client = &http.Client{Transport: http.DefaultTransport.(*http.Transport).Clone()}

// Latest is the version of the latest release (not a prerelease's),
// without its v.
func Latest(ctx context.Context) (string, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", Releases+"/latest", nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", "asuka-cli")
	// The redirect is the answer: not followed.
	c := *client
	c.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	resp, err := c.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	io.Copy(io.Discard, io.LimitReader(resp.Body, 64<<10))
	if resp.StatusCode < 300 || resp.StatusCode >= 400 {
		return "", fmt.Errorf("GET %s: %s, not a redirect to the latest release", req.URL, resp.Status)
	}
	loc, err := resp.Location()
	if err != nil {
		return "", err
	}
	dir, tag := path.Split(loc.Path)
	if !strings.HasSuffix(dir, "/tag/") || !strings.HasPrefix(tag, "v") || len(tag) < 2 {
		return "", fmt.Errorf("GET %s: redirected to %s, not a release", req.URL, loc)
	}
	tag, err = url.PathUnescape(tag)
	if err != nil {
		return "", err
	}
	return strings.TrimPrefix(tag, "v"), nil
}

// Newer reports whether latest is a later release than current
// (semantic versions: a prerelease before its release).
func Newer(latest, current string) bool {
	return latest != "" && update.Compare(latest, current) > 0
}

// Released reports whether version is a release's, which can be checked
// and upgraded: a CLI built from a checkout is "dev".
func Released(version string) bool {
	return version != "" && version != "dev" && !strings.HasPrefix(version, "(")
}

// Asset is the release asset of the CLI for goos and goarch.
func Asset(goos, goarch string) (string, error) {
	cpu := map[string]string{"amd64": "x64", "arm64": "arm64"}[goarch]
	switch {
	case cpu == "" || goos != "darwin" && goos != "linux" && goos != "windows":
		return "", fmt.Errorf("no release of asuka is built for %s/%s", goos, goarch)
	case goos == "darwin" && cpu == "x64":
		return "", errors.New("no release of asuka is built for Intel Macs")
	case goos == "windows":
		return "asuka-windows-" + cpu + ".exe", nil
	}
	return "asuka-" + goos + "-" + cpu, nil
}

// Upgrade replaces the executable exe with the release version's for
// this computer: downloaded beside it (packed, ArchiveAsset, or for a
// release before those, as it is, Asset), checked against the release's
// SHA256SUMS, unpacked, then check (its run, asuka version), then put in
// exe's place. On Windows, where a running executable can't be replaced
// but can be renamed, exe is moved aside to exe.old first (removed by
// RemoveOld, the next run).
func Upgrade(ctx context.Context, exe, version string, check func(path string) error) error {
	raw, err := Asset(runtime.GOOS, runtime.GOARCH)
	if err != nil {
		return err
	}
	packed, err := ArchiveAsset(runtime.GOOS, runtime.GOARCH)
	if err != nil {
		return err
	}
	return upgrade(ctx, exe, version, packed, raw, runtime.GOOS, check)
}

func upgrade(ctx context.Context, exe, version, packed, raw, goos string, check func(string) error) error {
	base := Releases + "/download/v" + url.PathEscape(version) + "/"
	sums, err := get(ctx, base+"SHA256SUMS", 1<<20)
	if err != nil {
		return err
	}
	asset := packed
	want, err := sumOf(sums, packed)
	if err != nil {
		// A release from before the packed ones: the executable as it is.
		asset = raw
		if want, err = sumOf(sums, raw); err != nil {
			return fmt.Errorf("v%s's SHA256SUMS: no %s or %s in it", version, packed, raw)
		}
	}
	dir := filepath.Dir(exe)
	tmpFile := func() (*os.File, error) {
		f, err := os.CreateTemp(dir, ".asuka-upgrade-*")
		if err != nil {
			return nil, fmt.Errorf("can't write beside %s (%v): reinstall asuka where you can, or with the permissions it was installed with", exe, err)
		}
		return f, nil
	}
	f, err := tmpFile()
	if err != nil {
		return err
	}
	tmp := f.Name()
	defer os.Remove(tmp)
	got, err := download(ctx, base+asset, f)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return err
	}
	if got != want {
		return fmt.Errorf("the download of %s doesn't match v%s's SHA256SUMS: not installed", asset, version)
	}
	if asset == packed {
		x, err := tmpFile()
		if err != nil {
			return err
		}
		exeTmp := x.Name()
		defer os.Remove(exeTmp)
		err = unpack(tmp, goos, x)
		if cerr := x.Close(); err == nil {
			err = cerr
		}
		if err != nil {
			return fmt.Errorf("unpacking %s: %v: not installed", asset, err)
		}
		tmp = exeTmp
	}
	if err := os.Chmod(tmp, 0o755); err != nil {
		return err
	}
	if check != nil {
		if err := check(tmp); err != nil {
			return fmt.Errorf("the downloaded %s doesn't run here (%v): not installed", asset, err)
		}
	}
	return replace(tmp, exe, goos)
}

// replace puts the file tmp in exe's place.
func replace(tmp, exe, goos string) error {
	if goos != "windows" {
		return os.Rename(tmp, exe) // atomic: a run at the same time has one or the other
	}
	old := exe + ".old"
	os.Remove(old) // a previous upgrade's, if no longer running
	if err := os.Rename(exe, old); err != nil {
		return fmt.Errorf("moving %s aside: %v", exe, err)
	}
	if err := os.Rename(tmp, exe); err != nil {
		os.Rename(old, exe)
		return err
	}
	os.Remove(old) // fails while it runs: RemoveOld, the next run
	return nil
}

// RemoveOld removes what an upgrade on Windows moved aside, once it no
// longer runs.
func RemoveOld(exe string) {
	if runtime.GOOS == "windows" && exe != "" {
		os.Remove(exe + ".old")
	}
}

// sumOf is name's SHA-256 in a SHA256SUMS file (sha256sum's lines:
// <hex>  <name>, or <hex> *<name>).
func sumOf(sums []byte, name string) (string, error) {
	sc := bufio.NewScanner(bytes.NewReader(sums))
	for sc.Scan() {
		f := strings.Fields(sc.Text())
		if len(f) == 2 && strings.TrimPrefix(f[1], "*") == name && len(f[0]) == 64 {
			return strings.ToLower(f[0]), nil
		}
	}
	return "", fmt.Errorf("no %s in it", name)
}

func get(ctx context.Context, u string, max int64) ([]byte, error) {
	var b bytes.Buffer
	resp, err := open(ctx, u)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	_, err = io.Copy(&b, io.LimitReader(resp.Body, max))
	return b.Bytes(), err
}

// download writes u to w, returning its SHA-256.
func download(ctx context.Context, u string, w io.Writer) (string, error) {
	resp, err := open(ctx, u)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	h := sha256.New()
	if _, err := io.Copy(io.MultiWriter(w, h), resp.Body); err != nil {
		return "", fmt.Errorf("GET %s: %v", u, err)
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func open(ctx context.Context, u string) (*http.Response, error) {
	req, err := http.NewRequestWithContext(ctx, "GET", u, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "asuka-cli")
	resp, err := client.Do(req) // a download redirects to the CDN
	if err != nil {
		return nil, err
	}
	if resp.StatusCode != http.StatusOK {
		resp.Body.Close()
		return nil, fmt.Errorf("GET %s: %s", u, resp.Status)
	}
	return resp, nil
}
