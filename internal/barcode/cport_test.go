package barcode

import (
	"bufio"
	"bytes"
	"fmt"
	"image"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// The C port (platform/desktop/src/barcode.c, asuka_barcode.h), which the
// desktop's C reads codes with, must read what this package reads: every
// image the tests decode is decoded again by it (built with ASan and
// UBSan), and the two must agree, code for code.

type decodeCase struct {
	file  string
	want  kindSet
	codes []Code
}

var cport struct {
	dir   string
	cases []decodeCase
}

func TestMain(m *testing.M) {
	dir, err := os.MkdirTemp("", "barcode-cport")
	if err == nil {
		cport.dir = dir
		decoded = func(gray []uint8, w, h int, want kindSet, origin image.Point, codes []Code) {
			f := filepath.Join(dir, fmt.Sprintf("%05d.pgm", len(cport.cases)))
			var b bytes.Buffer
			fmt.Fprintf(&b, "P5\n%d %d\n255\n", w, h)
			b.Write(gray[:w*h])
			if os.WriteFile(f, b.Bytes(), 0o644) == nil {
				c := decodeCase{f, want, append([]Code(nil), codes...)}
				for i := range c.codes {
					c.codes[i].Bounds = c.codes[i].Bounds.Sub(origin) // the C has the pixels alone
				}
				cport.cases = append(cport.cases, c)
			}
		}
	}
	code := m.Run()
	decoded = nil
	if code == 0 && cport.dir != "" && len(cport.cases) > 0 {
		if err := checkCPort(); err != nil {
			fmt.Fprintln(os.Stderr, "the C port:", err)
			code = 1
		}
	}
	if cport.dir != "" {
		os.RemoveAll(cport.dir)
	}
	os.Exit(code)
}

const cportMain = `#include "asuka_barcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Each argument is "kinds:file.pgm": the codes found, a line each (kind,
// the text in hex, the box), after a line "= file".
int main(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		unsigned kinds = (unsigned)strtoul(argv[i], NULL, 10);
		const char *path = strchr(argv[i], ':') + 1;
		FILE *f = fopen(path, "rb");
		if (!f) return 2;
		int w, h, max;
		if (fscanf(f, "P5 %d %d %d", &w, &h, &max) != 3) return 3;
		fgetc(f);
		unsigned char *pix = malloc((size_t)w * h + 1);
		if (fread(pix, 1, (size_t)w * h, f) != (size_t)w * h) return 4;
		fclose(f);
		asuka_barcode *codes;
		size_t n = asuka_barcode_decode(pix, w, h, w, kinds, &codes);
		printf("= %s\n", path);
		for (size_t k = 0; k < n; k++) {
			printf("%s ", asuka_barcode_name(codes[k].kind));
			for (const unsigned char *c = (const unsigned char *)codes[k].text; *c; c++) printf("%02x", *c);
			printf(" %d %d %d %d\n", codes[k].x0, codes[k].y0, codes[k].x1, codes[k].y1);
		}
		asuka_barcode_free(codes, n);
		free(pix);
	}
	return 0;
}
`

// cKinds is the C port's bits for a kindSet (the same order).
func cKinds(k kindSet) uint {
	var c uint
	for i, b := range []kindSet{kQR, kDataMatrix, kEAN13, kEAN8, kUPCA, kUPCE, kCode128, kCode39} {
		if k&b != 0 {
			c |= 1 << i
		}
	}
	return c
}

func checkCPort() error {
	cc, err := cbuild.Find()
	if err != nil {
		fmt.Fprintln(os.Stderr, "the C port isn't checked: no C compiler:", err)
		return nil
	}
	root, err := filepath.Abs(filepath.Join("..", ".."))
	if err != nil {
		return err
	}
	desk := filepath.Join(root, "platform", "desktop")
	main := filepath.Join(cport.dir, "main.c")
	if err := os.WriteFile(main, []byte(cportMain), 0o644); err != nil {
		return err
	}
	san := []string{"-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"}
	if runtime.GOOS == "windows" {
		san = nil
	}
	flags := append(append(append([]string{}, cbuild.Flags...), "-O1", "-I"+filepath.Join(desk, "include")), san...)
	objs, err := cc.CompileWith([]string{main, filepath.Join(desk, "src", "barcode.c"), filepath.Join(desk, "src", "barcode_jis.c")}, filepath.Join(cport.dir, "obj"), flags)
	if err != nil {
		return err
	}
	exe := filepath.Join(cport.dir, "cport")
	link := append([]string{}, san...)
	if runtime.GOOS == "linux" {
		link = append(link, "-lm")
	}
	if err := cc.Link(objs, exe, link); err != nil {
		return err
	}
	got := map[string][]string{}
	for start := 0; start < len(cport.cases); start += 64 {
		var args []string
		for _, c := range cport.cases[start:min(start+64, len(cport.cases))] {
			args = append(args, strconv.FormatUint(uint64(cKinds(c.want)), 10)+":"+c.file)
		}
		cmd := exec.Command(exe, args...)
		if runtime.GOOS == "linux" {
			cmd.Env = append(os.Environ(), "ASAN_OPTIONS=detect_leaks=1")
		}
		var stderr bytes.Buffer
		cmd.Stderr = &stderr
		out, err := cmd.Output()
		if err != nil {
			return fmt.Errorf("%v\n%s", err, stderr.String())
		}
		var cur string
		sc := bufio.NewScanner(bytes.NewReader(out))
		sc.Buffer(nil, 1<<20)
		for sc.Scan() {
			if f, ok := strings.CutPrefix(sc.Text(), "= "); ok {
				cur = f
				got[cur] = []string{}
			} else {
				got[cur] = append(got[cur], sc.Text())
			}
		}
	}
	var bad []string
	for _, c := range cport.cases {
		g := got[c.file]
		if len(g) != len(c.codes) {
			bad = append(bad, fmt.Sprintf("%s: Go read %v, C %v", filepath.Base(c.file), c.codes, g))
			continue
		}
		for i, code := range c.codes {
			var kind, hex string
			var x0, y0, x1, y1 int
			fmt.Sscanf(g[i], "%s %s %d %d %d %d", &kind, &hex, &x0, &y0, &x1, &y1)
			if !strings.Contains(g[i], " ") || kind != string(code.Kind) || hex != fmt.Sprintf("%x", code.Text) {
				bad = append(bad, fmt.Sprintf("%s: Go read %s %q, C %s", filepath.Base(c.file), code.Kind, code.Text, g[i]))
				continue
			}
			// The boxes: a pixel's give for arithmetic that rounds alike
			// but not the same (fused multiply-adds).
			b := code.Bounds
			if abs(b.Min.X-x0) > 1 || abs(b.Min.Y-y0) > 1 || abs(b.Max.X-x1) > 1 || abs(b.Max.Y-y1) > 1 {
				bad = append(bad, fmt.Sprintf("%s: Go's box %v, C's (%d,%d)-(%d,%d)", filepath.Base(c.file), b, x0, y0, x1, y1))
			}
		}
	}
	if len(bad) > 0 {
		if len(bad) > 20 {
			bad = append(bad[:20], fmt.Sprintf("and %d more", len(bad)-20))
		}
		return fmt.Errorf("it disagrees on %d of %d images:\n%s", len(bad), len(cport.cases), strings.Join(bad, "\n"))
	}
	fmt.Fprintf(os.Stderr, "the C port agrees on %d images\n", len(cport.cases))
	return nil
}
