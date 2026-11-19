// Command mbedtlsgen vendors the subset of Mbed TLS that std/net's TLS
// is made of (core/third_party/mbedtls, compiled through
// core/src/std/mbedtls), from a pinned release:
//
//	go generate ./internal/mbedtlsgen           # downloads the release
//	go run ./internal/mbedtlsgen -cache dir     # mbedtls-<v>.tar.bz2 in dir
//
// It lays the release out flat: every public header of Mbed TLS and of
// TF-PSA-Crypto (the crypto it's built on) under include/ (mbedtls/,
// psa/, tf-psa-crypto/: the core's include path names it), and the C
// files below with every internal header they include, read from
// their #include lines, in src/, unchanged. The configuration is this
// directory's (mbedtls_config.h, crypto_config.h, threading_alt.h),
// written over the release's. Each C file gets a file of its own in
// core/src/std/mbedtls that includes it, as BearSSL's do, after
// names.h: what the files define, and what they call that the core
// defines (std/tls.c), renamed ak_mbedtls_..., so an app's C that links
// an Mbed TLS of its own doesn't clash. names.h is read from the
// objects (compiled for arm64 and x86_64 on a Mac: each has a file the
// other compiles empty).
package main

//go:generate go run .

import (
	"archive/tar"
	"bytes"
	"compress/bzip2"
	"crypto/sha256"
	"debug/elf"
	"debug/macho"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/cbuild"
)

// The release, and its tarball's SHA-256 (its release's sha256sum.txt).
const (
	version = "4.1.2"
	sum     = "392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126"
)

// files are the C files the subset compiles: what the configuration
// enables, each a file of the release's library/ or of TF-PSA-Crypto's
// core, drivers/builtin/src, platform, utilities or extras.
var files = []string{
	// TLS (library/).
	"ssl_ciphersuites.c", "ssl_client.c", "ssl_msg.c", "ssl_tls.c",
	"ssl_tls12_client.c", "ssl_tls12_server.c",
	"ssl_tls13_client.c", "ssl_tls13_generic.c", "ssl_tls13_keys.c", "ssl_tls13_server.c",
	"ssl_ticket.c", "mps_reader.c",
	// X.509 (library/).
	"x509.c", "x509_crt.c", "x509_oid.c",
	// PSA's core.
	"psa_crypto.c", "psa_crypto_client.c", "psa_crypto_driver_wrappers_no_static.c",
	"psa_crypto_slot_management.c", "psa_util.c",
	// The built-in drivers.
	"aes.c", "aesce.c", "aesni.c", "block_cipher.c", "gcm.c",
	"chacha20.c", "chacha20_neon.c", "chachapoly.c", "poly1305.c",
	"sha256.c", "sha512.c",
	"bignum.c", "bignum_core.c", "ecp.c", "ecp_curves.c", "ecdsa.c",
	"rsa.c", "rsa_alt_helpers.c",
	"psa_crypto_aead.c", "psa_crypto_cipher.c", "psa_crypto_ecp.c", "psa_crypto_hash.c",
	"psa_crypto_mac.c", "psa_crypto_rsa.c", "psa_util_internal.c",
	// The platform layer and utilities.
	"platform.c", "platform_util.c", "threading.c",
	"asn1parse.c", "asn1write.c", "base64.c", "constant_time.c", "oid.c", "pem.c",
	// Keys (extras/).
	"md.c", "pk.c", "pk_ecc.c", "pk_rsa.c", "pk_wrap.c", "pkparse.c", "pkwrite.c",
}

// Where the release keeps them.
var (
	publicDirs   = []string{"include", "tf-psa-crypto/include", "tf-psa-crypto/drivers/builtin/include"}
	internalDirs = []string{"library", "tf-psa-crypto/core", "tf-psa-crypto/drivers/builtin/src", "tf-psa-crypto/platform", "tf-psa-crypto/utilities", "tf-psa-crypto/extras", "tf-psa-crypto/dispatch"}
	// The configuration, this directory's, and where it goes.
	configs = map[string]string{
		"mbedtls_config.h": "include/mbedtls/mbedtls_config.h",
		"crypto_config.h":  "include/psa/crypto_config.h",
		"threading_alt.h":  "include/mbedtls/threading_alt.h",
	}
)

func main() {
	log.SetFlags(0)
	cache := flag.String("cache", "", "a directory with mbedtls-"+version+".tar.bz2 (default: download it)")
	core := flag.String("core", "../../core", "the core")
	flag.Parse()
	if err := run(*cache, *core); err != nil {
		log.Fatal("mbedtlsgen: ", err)
	}
}

func run(cache, core string) error {
	tmp, err := os.MkdirTemp("", "mbedtlsgen")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	if err := fetch(cache, tmp); err != nil {
		return err
	}
	rel := filepath.Join(tmp, "mbedtls-"+version)
	out := filepath.Join(core, "third_party", "mbedtls")
	if err := os.RemoveAll(out); err != nil {
		return err
	}
	// Every header and internal file, by where it goes.
	public := map[string]string{} // include/<path> → the release's file
	for _, d := range publicDirs {
		root := filepath.Join(rel, filepath.FromSlash(d))
		err := filepath.WalkDir(root, func(p string, e os.DirEntry, err error) error {
			if err != nil || e.IsDir() || !strings.HasSuffix(p, ".h") {
				return err
			}
			r, _ := filepath.Rel(root, p)
			key := filepath.ToSlash(r)
			if _, ok := public[key]; ok {
				return fmt.Errorf("two public headers %s", key)
			}
			public[key] = p
			return nil
		})
		if err != nil {
			return err
		}
	}
	internal := map[string]string{} // src/<name> → the release's file
	for _, d := range internalDirs {
		all, _ := filepath.Glob(filepath.Join(rel, filepath.FromSlash(d), "*.[ch]"))
		for _, p := range all {
			b := filepath.Base(p)
			if _, ok := internal[b]; ok {
				return fmt.Errorf("two internal files %s", b)
			}
			internal[b] = p
		}
	}
	// The C files, and what they include, transitively.
	copied := map[string]bool{}
	var queue []string // "src/x" or "include/x"
	for _, f := range files {
		if internal[f] == "" {
			return fmt.Errorf("no %s in the release", f)
		}
		queue = append(queue, "src/"+f)
	}
	inc := regexp.MustCompile(`(?m)^\s*#\s*include\s*["<]([^">]+)[">]`)
	for len(queue) > 0 {
		k := queue[0]
		queue = queue[1:]
		if copied[k] {
			continue
		}
		copied[k] = true
		var from string
		if strings.HasPrefix(k, "src/") {
			from = internal[strings.TrimPrefix(k, "src/")]
		} else {
			from = public[strings.TrimPrefix(k, "include/")]
		}
		b, err := os.ReadFile(from)
		if err != nil {
			return err
		}
		for name := range configs {
			if configs[name] == k {
				if b, err = os.ReadFile(name); err != nil {
					return err
				}
			}
		}
		if err := write(filepath.Join(out, filepath.FromSlash(k)), b); err != nil {
			return err
		}
		for _, m := range inc.FindAllSubmatch(b, -1) {
			name := string(m[1])
			// A header beside it, an internal one (the flat src/), or a
			// public one.
			beside := strings.TrimPrefix(filepath.ToSlash(filepath.Join(filepath.Dir(k), name)), "include/")
			switch {
			case strings.HasPrefix(k, "include/") && public[beside] != "":
				queue = append(queue, "include/"+beside)
			case internal[name] != "" && strings.HasSuffix(name, ".h"):
				queue = append(queue, "src/"+name)
			case public[name] != "":
				queue = append(queue, "include/"+name)
			}
		}
	}
	for name, dest := range configs {
		if !copied[dest] {
			b, err := os.ReadFile(name)
			if err != nil {
				return err
			}
			if err := write(filepath.Join(out, filepath.FromSlash(dest)), b); err != nil {
				return err
			}
		}
	}
	lic, err := os.ReadFile(filepath.Join(rel, "LICENSE"))
	if err != nil {
		return err
	}
	if err := write(filepath.Join(out, "LICENSE"), lic); err != nil {
		return err
	}
	if err := write(filepath.Join(out, "README.md"), []byte(readme())); err != nil {
		return err
	}
	// The core's files that compile them, names.h empty while the names
	// are read.
	wrap := filepath.Join(core, "src", "std", "mbedtls")
	old, _ := filepath.Glob(filepath.Join(wrap, "*.c"))
	for _, p := range old {
		os.Remove(p)
	}
	names := filepath.Join(wrap, "names.h")
	if err := write(names, []byte("")); err != nil {
		return err
	}
	var srcs []string
	for _, f := range files {
		p := filepath.Join(wrap, f)
		body := fmt.Sprintf("// Mbed TLS's %s, a file of its own as Mbed TLS compiles it (its files\n// share macro and static names): third_party/mbedtls/README.md. Not the\n// web's, which has no sockets.\n#if !defined(__wasm__)\n#include \"std/mbedtls/names.h\"\n#include \"../../../third_party/mbedtls/src/%s\"\n#endif\n", f, f)
		if err := write(p, []byte(body)); err != nil {
			return err
		}
		srcs = append(srcs, p)
	}
	syms, err := symbols(core, srcs, tmp)
	if err != nil {
		return err
	}
	var h strings.Builder
	h.WriteString("// names.h: Mbed TLS's exported names, the core's own (ak_mbedtls_...), so\n// an app's C or a library it links that has Mbed TLS of its own doesn't\n// clash with the subset the core compiles in (third_party/mbedtls). Every\n// file of core/src/std/mbedtls includes it first, and so does what calls\n// Mbed TLS (std/mbedtls/mbedtls.h). Written by internal/mbedtlsgen from\n// what the subset's objects define, and what they call that std/tls.c\n// defines.\n#ifndef AK_MBEDTLS_NAMES_H\n#define AK_MBEDTLS_NAMES_H\n")
	for _, s := range syms {
		fmt.Fprintf(&h, "#define %s ak_%s\n", s, s)
	}
	h.WriteString("#endif\n")
	if err := write(names, []byte(h.String())); err != nil {
		return err
	}
	log.Printf("mbedtlsgen: Mbed TLS %s: %d files, %d names", version, len(copied), len(syms))
	return nil
}

func write(p string, b []byte) error {
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	return os.WriteFile(p, b, 0o644)
}

// symbols compiles srcs as the core does and reads the external names
// their objects define, and those they call with Mbed TLS's prefixes
// (the core defines them: the RNG, the locks).
func symbols(core string, srcs []string, tmp string) ([]string, error) {
	cc, err := cbuild.Find()
	if err != nil {
		return nil, err
	}
	base := append(append(append([]string{}, cbuild.Flags...), cbuild.Includes(core)...), "-O1")
	targets := [][]string{nil}
	if runtime.GOOS == "darwin" {
		targets = [][]string{{"-target", "arm64-apple-macos11"}, {"-target", "x86_64-apple-macos11"}}
	}
	set := map[string]bool{}
	for i, t := range targets {
		objs, err := cc.CompileWith(srcs, filepath.Join(tmp, fmt.Sprint("obj", i)), append(append([]string{}, base...), t...))
		if err != nil {
			return nil, err
		}
		for _, o := range objs {
			if err := readSymbols(o, set); err != nil {
				return nil, fmt.Errorf("%s: %v", o, err)
			}
		}
	}
	var out []string
	for s := range set {
		out = append(out, s)
	}
	sort.Strings(out)
	return out, nil
}

func ours(name string) bool {
	return strings.HasPrefix(name, "mbedtls_") || strings.HasPrefix(name, "psa_") || strings.HasPrefix(name, "tf_psa_")
}

func readSymbols(obj string, set map[string]bool) error {
	b, err := os.ReadFile(obj)
	if err != nil {
		return err
	}
	if f, err := macho.NewFile(bytes.NewReader(b)); err == nil {
		if f.Symtab == nil {
			return nil
		}
		for _, s := range f.Symtab.Syms {
			const nExt, nType, nUndf = 0x01, 0x0e, 0x0
			if s.Type&nExt == 0 || s.Type&0xe0 != 0 {
				continue // local, or a debugging entry
			}
			name := strings.TrimPrefix(s.Name, "_")
			if s.Type&nType != nUndf || ours(name) {
				set[name] = true
			}
		}
		return nil
	}
	f, err := elf.NewFile(bytes.NewReader(b))
	if err != nil {
		return err
	}
	syms, err := f.Symbols()
	if err != nil {
		return nil
	}
	for _, s := range syms {
		if elf.ST_BIND(s.Info) == elf.STB_LOCAL || s.Name == "" {
			continue
		}
		if s.Section != elf.SHN_UNDEF || ours(s.Name) {
			if typ := elf.ST_TYPE(s.Info); typ == elf.STT_FUNC || typ == elf.STT_OBJECT || typ == elf.STT_NOTYPE || typ == elf.STT_COMMON {
				set[s.Name] = true
			}
		}
	}
	return nil
}

func readme() string {
	var b strings.Builder
	fmt.Fprintf(&b, `# Mbed TLS

A subset of Mbed TLS %s (https://github.com/Mbed-TLS/mbedtls,
`+"`mbedtls-%s.tar.bz2`"+`, SHA-256 %s),
the Mbed TLS project's, Apache-2.0 licensed (LICENSE; or GPL-2.0-or-later
at the user's choice): TLS 1.2 and 1.3 and X.509 for std/net's TLS
(core/src/std/tls.c), and TF-PSA-Crypto, the crypto it's built on. Mbed
TLS 4.1 is a long-term support branch, fixed until at least March 2029.

Written by internal/mbedtlsgen (`+"`go generate ./internal/mbedtlsgen`"+`), which
says how: the release's public headers under include/, the C files
below and the internal headers they include flat in src/, all
unchanged but the configuration (include/mbedtls/mbedtls_config.h,
include/psa/crypto_config.h, include/mbedtls/threading_alt.h: Asuka's,
from internal/mbedtlsgen). Each C file has a file in
core/src/std/mbedtls that includes it, after names.h, which renames
what it exports to the core's names (ak_mbedtls_...). The link leaves
out what nothing calls, and nothing does unless an app calls std/net's
TLS (core/link/link.c's AK_USES_TLS).

`, version, version, sum)
	b.WriteString("The C files:\n\n")
	for _, f := range files {
		fmt.Fprintf(&b, "- `%s`\n", f)
	}
	b.WriteString("\nTo update: change the version and SHA-256 in internal/mbedtlsgen and run\nit; to add a file, add it to its list.\n")
	return b.String()
}

// fetch extracts the release's tarball, from cache or downloaded, into
// dir, after checking its SHA-256.
func fetch(cache, dir string) error {
	file := "mbedtls-" + version + ".tar.bz2"
	var b []byte
	var err error
	if cache != "" {
		b, err = os.ReadFile(filepath.Join(cache, file))
	}
	if cache == "" || os.IsNotExist(err) {
		b, err = download("https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-" + version + "/" + file)
	}
	if err != nil {
		return err
	}
	h := sha256.Sum256(b)
	if got := hex.EncodeToString(h[:]); got != sum {
		return fmt.Errorf("%s has SHA-256 %s, want %s", file, got, sum)
	}
	tr := tar.NewReader(bzip2.NewReader(bytes.NewReader(b)))
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
		p := filepath.Join(dir, filepath.FromSlash(h.Name))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			return err
		}
		body, err := io.ReadAll(tr)
		if err != nil {
			return err
		}
		if err := os.WriteFile(p, body, 0o644); err != nil {
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
