// Command mimallocgen vendors mimalloc, the allocator the core's
// ak_alloc, ak_realloc and ak_free take memory from
// (core/src/base/alloc.c), into core/third_party/mimalloc, from a pinned
// release:
//
//	go generate ./internal/mimallocgen          # downloads the release
//	go run ./internal/mimallocgen -cache dir    # v<version>.tar.gz in dir
//
// It copies the release's src/ and include/ (their C and headers,
// unchanged) and its LICENSE, and writes core/src/base/mimalloc_names.h:
// every external name alloc.c's object defines once mimalloc is compiled
// into it, renamed to the core's own (ak_mi_..., ak__mi_...), so an app,
// or a library it links, with a mimalloc of its own doesn't clash with
// the core's. The names are read from alloc.c compiled for each target
// whose toolchain is here (Apple's, the NDK's, Zig's for Linux, with
// glibc and musl, and Windows): a platform's own files define names of
// their own.
package main

//go:generate go run .

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"debug/elf"
	"debug/macho"
	"debug/pe"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/cbuild"
)

// The release, and its tarball's SHA-256 (GitHub's archive of the tag).
const (
	version = "3.6.1"
	sum     = "2672e6a99d4579fb4bb32d9de0d63aa424ce600d31fbfaf06eb74d940d2e07a6"
)

func main() {
	log.SetFlags(0)
	cache := flag.String("cache", "", "a directory with v"+version+".tar.gz (default: download it)")
	core := flag.String("core", "../../core", "the core")
	flag.Parse()
	if err := run(*cache, *core); err != nil {
		log.Fatal("mimallocgen: ", err)
	}
}

func run(cache, core string) error {
	tmp, err := os.MkdirTemp("", "mimallocgen")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	if err := fetch(cache, tmp); err != nil {
		return err
	}
	rel := filepath.Join(tmp, "mimalloc-"+version)
	dst := filepath.Join(core, "third_party", "mimalloc")
	if err := os.RemoveAll(dst); err != nil {
		return err
	}
	n := 0
	for _, d := range []string{"src", "include"} {
		err := filepath.WalkDir(filepath.Join(rel, d), func(p string, e os.DirEntry, err error) error {
			if err != nil || e.IsDir() || !(strings.HasSuffix(p, ".c") || strings.HasSuffix(p, ".h")) {
				return err
			}
			r, _ := filepath.Rel(rel, p)
			n++
			return copyFile(p, filepath.Join(dst, r))
		})
		if err != nil {
			return err
		}
	}
	if err := copyFile(filepath.Join(rel, "LICENSE"), filepath.Join(dst, "LICENSE")); err != nil {
		return err
	}
	if err := write(filepath.Join(dst, "README.md"), []byte(readme())); err != nil {
		return err
	}

	// The names, read with the header empty.
	names := filepath.Join(core, "src", "base", "mimalloc_names.h")
	if err := write(names, nil); err != nil {
		return err
	}
	set := map[string]bool{}
	targets := 0
	for _, t := range toolchains() {
		flags := append(append(append(append([]string{}, t.flags...), cbuild.Flags...), cbuild.Includes(core)...), "-O2", "-DNDEBUG")
		for _, debug := range []bool{false, true} {
			f := flags
			if debug {
				f = append(append(append([]string{}, t.flags...), cbuild.Flags...), cbuild.Includes(core)...)
				f = append(f, "-O1", "-DAK_DEBUG=1")
			}
			out := filepath.Join(tmp, "obj", fmt.Sprint(t.name, debug))
			objs, err := t.cc.CompileWith([]string{filepath.Join(core, "src", "base", "alloc.c")}, out, f)
			if err != nil {
				return fmt.Errorf("%s: %v", t.name, err)
			}
			got, err := defined(objs[0])
			if err != nil {
				return fmt.Errorf("%s: %v", t.name, err)
			}
			for _, s := range got {
				// (COFF's .refptr. names are the compiler's own.)
				if !strings.HasPrefix(s, "ak_") && ident.MatchString(s) {
					set[s] = true
				}
			}
		}
		targets++
	}
	var syms []string
	for s := range set {
		syms = append(syms, s)
	}
	sort.Strings(syms)
	var h strings.Builder
	h.WriteString("// mimalloc_names.h: mimalloc's external names, the core's own (ak_mi_...,\n// ak__mi_...), so an app or a library it links with a mimalloc of its own\n// doesn't clash with the core's (core/src/base/alloc.c, which includes it\n// before mimalloc). Written by internal/mimallocgen from what alloc.c's\n// objects define, for every target.\n#ifndef AK_MIMALLOC_NAMES_H\n#define AK_MIMALLOC_NAMES_H\n")
	for _, s := range syms {
		fmt.Fprintf(&h, "#define %s ak_%s\n", s, s)
	}
	h.WriteString("#endif\n")
	if err := write(names, []byte(h.String())); err != nil {
		return err
	}
	log.Printf("mimallocgen: mimalloc %s: %d files, %d names from %d targets", version, n, len(syms), targets)
	return nil
}

var ident = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_]*$`)

type toolchain struct {
	name  string
	cc    cbuild.Compiler
	flags []string
}

// toolchains are the targets' compilers that are here: each platform's
// own, and Zig's for Linux (glibc and musl) and Windows.
func toolchains() []toolchain {
	var out []toolchain
	for _, t := range []cbuild.Target{
		{OS: "darwin", Arch: "arm64"}, {OS: "darwin", Arch: "amd64"}, {OS: "ios", Arch: "arm64"},
		{OS: "android", Arch: "arm64"}, {OS: "android", Arch: "amd64"}, {OS: "android", Arch: "arm"},
		{OS: "linux", Arch: "arm64"}, {OS: "linux", Arch: "amd64"},
		{OS: "windows", Arch: "amd64"}, {OS: "windows", Arch: "arm64"},
	} {
		tc, err := cbuild.ForTarget(t)
		if err != nil {
			log.Printf("mimallocgen: no toolchain for %s (%v): skipped", t, err)
			continue
		}
		out = append(out, toolchain{t.String(), tc.CC, tc.Flags})
	}
	if z, err := cbuild.Zig(); err == nil {
		for _, a := range []string{"aarch64", "x86_64"} {
			out = append(out, toolchain{a + "-linux-musl", z, []string{"-target", a + "-linux-musl"}})
		}
	}
	return out
}

func copyFile(from, to string) error {
	b, err := os.ReadFile(from)
	if err != nil {
		return err
	}
	return write(to, b)
}

func write(p string, b []byte) error {
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	return os.WriteFile(p, b, 0o644)
}

func readme() string {
	return fmt.Sprintf(`# mimalloc

mimalloc %s (tag `+"`v%s`"+`, https://github.com/microsoft/mimalloc, GitHub's
archive of the tag, SHA-256 %s),
Microsoft's, MIT licensed (LICENSE): its src/ and include/, unchanged.
core/src/base/alloc.c compiles it in (src/static.c: the whole allocator,
one file) as what ak_alloc, ak_realloc, ak_free and the small blocks
take memory from, with the core's flags, its exported names renamed to
the core's (core/src/base/mimalloc_names.h: ak_mi_...), never overriding
the process's malloc. Not under AddressSanitizer or MemorySanitizer (the
core's tests) or on the web, which keep the C library's.

Written by internal/mimallocgen (`+"`go generate ./internal/mimallocgen`"+`). To
update: change the version and SHA-256 there and run it, which also
writes mimalloc_names.h again.
`, version, version, sum)
}

// fetch extracts the release's tarball, from cache or downloaded, into
// dir, after checking its SHA-256.
func fetch(cache, dir string) error {
	file := "v" + version + ".tar.gz"
	var b []byte
	var err error
	if cache != "" {
		b, err = os.ReadFile(filepath.Join(cache, file))
	}
	if cache == "" || os.IsNotExist(err) {
		b, err = download("https://github.com/microsoft/mimalloc/archive/refs/tags/" + file)
	}
	if err != nil {
		return err
	}
	h := sha256.Sum256(b)
	if got := hex.EncodeToString(h[:]); got != sum {
		return fmt.Errorf("%s has SHA-256 %s, want %s", file, got, sum)
	}
	zr, err := gzip.NewReader(bytes.NewReader(b))
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
		if h.Typeflag != tar.TypeReg || strings.Contains(h.Name, "..") || strings.HasPrefix(h.Name, "/") {
			continue
		}
		body, err := io.ReadAll(tr)
		if err != nil {
			return err
		}
		if err := write(filepath.Join(dir, filepath.FromSlash(h.Name)), body); err != nil {
			return err
		}
	}
}

func download(url string) ([]byte, error) {
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s: %s", url, resp.Status)
	}
	return io.ReadAll(resp.Body)
}

// defined are the external names an object defines (Mach-O, ELF or
// COFF), without Mach-O's leading underscore.
func defined(obj string) ([]string, error) {
	b, err := os.ReadFile(obj)
	if err != nil {
		return nil, err
	}
	var out []string
	if f, err := macho.NewFile(bytes.NewReader(b)); err == nil {
		if f.Symtab == nil {
			return nil, nil
		}
		for _, s := range f.Symtab.Syms {
			const nExt, nType, nUndf = 0x01, 0x0e, 0x0
			if s.Type&nExt == 0 || s.Type&0xe0 != 0 || s.Type&nType == nUndf {
				continue // local, a debugging entry, or undefined
			}
			out = append(out, strings.TrimPrefix(s.Name, "_"))
		}
		return out, nil
	}
	if f, err := elf.NewFile(bytes.NewReader(b)); err == nil {
		syms, err := f.Symbols()
		if err != nil {
			return nil, nil
		}
		for _, s := range syms {
			if elf.ST_BIND(s.Info) == elf.STB_LOCAL || s.Name == "" || s.Section == elf.SHN_UNDEF {
				continue
			}
			if typ := elf.ST_TYPE(s.Info); typ == elf.STT_FUNC || typ == elf.STT_OBJECT || typ == elf.STT_NOTYPE || typ == elf.STT_COMMON || typ == elf.STT_TLS {
				out = append(out, s.Name)
			}
		}
		return out, nil
	}
	f, err := pe.NewFile(bytes.NewReader(b))
	if err != nil {
		return nil, fmt.Errorf("not Mach-O, ELF or COFF")
	}
	const external = 2 // IMAGE_SYM_CLASS_EXTERNAL
	for _, s := range f.Symbols {
		if s.StorageClass != external || s.SectionNumber <= 0 {
			continue
		}
		out = append(out, s.Name)
	}
	return out, nil
}
