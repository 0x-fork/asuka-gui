package selfupdate

import (
	"archive/tar"
	"archive/zip"
	"compress/flate"
	"compress/gzip"
	"errors"
	"fmt"
	"io"
	"os"
	"time"
)

// The CLI's executable is released packed, a third of its size: a
// .tar.gz on macOS and Linux (tar and gzip are on every one; the mode
// stays executable), a .zip on Windows (PowerShell's Expand-Archive),
// each holding the executable alone, at its root. The executable itself
// is released too (Asset), which asuka upgrade before 0.2.10 fetches.

// ArchiveAsset is the release asset of the CLI for goos and goarch,
// packed: asuka-linux-x64.tar.gz, asuka-windows-x64.zip.
func ArchiveAsset(goos, goarch string) (string, error) {
	a, err := Asset(goos, goarch)
	if err != nil {
		return "", err
	}
	if goos == "windows" {
		return a[:len(a)-len(".exe")] + ".zip", nil
	}
	return a + ".tar.gz", nil
}

// exeName is the executable's name in the archive.
func exeName(goos string) string {
	if goos == "windows" {
		return "asuka.exe"
	}
	return "asuka"
}

// packTime is every packed file's time: the same executable packs to the
// same bytes, so a release run again uploads nothing new.
var packTime = time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC)

// maxExe bounds what an archive may unpack to: a damaged or hostile
// archive can't fill the disk.
const maxExe = 1 << 30

// Pack writes the archive of the CLI's executable exe (its contents) for
// goos to w.
func Pack(w io.Writer, goos string, exe []byte) error {
	if goos == "windows" {
		z := zip.NewWriter(w)
		z.RegisterCompressor(zip.Deflate, func(out io.Writer) (io.WriteCloser, error) {
			return flate.NewWriter(out, flate.BestCompression)
		})
		h := &zip.FileHeader{Name: exeName(goos), Method: zip.Deflate, Modified: packTime}
		h.SetMode(0o755)
		f, err := z.CreateHeader(h)
		if err != nil {
			return err
		}
		if _, err := f.Write(exe); err != nil {
			return err
		}
		return z.Close()
	}
	gz, err := gzip.NewWriterLevel(w, gzip.BestCompression)
	if err != nil {
		return err
	}
	tw := tar.NewWriter(gz)
	if err := tw.WriteHeader(&tar.Header{Name: exeName(goos), Mode: 0o755, Size: int64(len(exe)), ModTime: packTime, Typeflag: tar.TypeReg, Format: tar.FormatUSTAR}); err != nil {
		return err
	}
	if _, err := tw.Write(exe); err != nil {
		return err
	}
	if err := tw.Close(); err != nil {
		return err
	}
	return gz.Close()
}

// unpack writes the executable in the archive at path (Pack's, for goos)
// to w: the one file it holds.
func unpack(path, goos string, w io.Writer) error {
	name := exeName(goos)
	if goos == "windows" {
		z, err := zip.OpenReader(path)
		if err != nil {
			return err
		}
		defer z.Close()
		for _, f := range z.File {
			if f.Name != name {
				continue
			}
			r, err := f.Open()
			if err != nil {
				return err
			}
			defer r.Close()
			return copyExe(w, r)
		}
		return fmt.Errorf("no %s in it", name)
	}
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		return err
	}
	tr := tar.NewReader(gz)
	for {
		h, err := tr.Next()
		if errors.Is(err, io.EOF) {
			return fmt.Errorf("no %s in it", name)
		}
		if err != nil {
			return err
		}
		if h.Name == name && h.Typeflag == tar.TypeReg {
			return copyExe(w, tr)
		}
	}
}

func copyExe(w io.Writer, r io.Reader) error {
	n, err := io.Copy(w, io.LimitReader(r, maxExe+1))
	if err == nil && n > maxExe {
		err = errors.New("its executable is past any asuka's size")
	}
	return err
}
