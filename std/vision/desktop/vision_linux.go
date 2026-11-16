//go:build linux && !android

package vision

import (
	"context"
	"image"
	"os"
	"path/filepath"
	"strings"
	"unicode/utf8"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: ZBar reads codes and Tesseract reads text, where they're
// installed (libzbar0; libtesseract5 and its languages' data). No library
// on a Linux desktop finds faces: none are found.

type gnuAnalyzer struct{}

func newAnalyzer() (Analyzer, error) { return gnuAnalyzer{}, nil }

func (gnuAnalyzer) Analyze(ctx context.Context, path string, text, faces, codes bool, languages []string) (Result, error) {
	img, err := ieDecode(path, assets())
	if err != nil {
		return Result{}, err
	}
	w, ht := img.Rect.Dx(), img.Rect.Dy()
	res := Result{Width: w, Height: ht, Lines: []Line{}, Faces: []Box{}, Codes: []Code{}}
	box := func(x0, y0, x1, y1 int) Box {
		return Box{X: float64(x0) / float64(w), Y: float64(y0) / float64(ht), W: float64(x1-x0) / float64(w), H: float64(y1-y0) / float64(ht)}
	}
	gray := grayOf(img)
	if codes && LoadZBar() == nil {
		for _, s := range ZBarScan(gray, w, ht) {
			if kind := zbarKind(s.Type); kind != "" {
				res.Codes = append(res.Codes, Code{Kind: kind, Value: s.Data, Box: box(s.X0, s.Y0, s.X1, s.Y1)})
			}
		}
	}
	if text && LoadTesseract() == nil {
		langs := tessLanguages(languages, userLanguage())
		words, err := TessRead(gray, w, ht, langs)
		if err != nil && langs != "eng" {
			words, _ = TessRead(gray, w, ht, "eng")
		}
		res.Lines = visLines(words, box)
	}
	return res, nil
}

// userLanguage is the user's language (LANGUAGE, LC_ALL, LC_MESSAGES,
// LANG).
func userLanguage() string {
	for _, k := range []string{"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"} {
		v, _, _ := strings.Cut(os.Getenv(k), ":")
		if v != "" && v != "C" && v != "POSIX" {
			return v
		}
	}
	return ""
}

func inList(l []string, s string) bool {
	for _, x := range l {
		if x == s {
			return true
		}
	}
	return false
}

// grayOf is the image's luma, a byte a pixel (on white where it's
// transparent).
func grayOf(img *image.RGBA) []byte {
	w, h := img.Rect.Dx(), img.Rect.Dy()
	g := make([]byte, w*h)
	for y := range h {
		row := img.Pix[y*img.Stride:]
		for x := range w {
			p := row[x*4:]
			white := 255 - uint32(p[3])
			r, gg, b := uint32(p[0])+white, uint32(p[1])+white, uint32(p[2])+white
			g[y*w+x] = byte((299*r + 587*gg + 114*b) / 1000)
		}
	}
	return g
}

// zbarKind is asuka/sdk/scan's name of a ZBar symbology ("": none of its).
func zbarKind(t string) string {
	switch t {
	case "QR-Code":
		return "qr"
	case "EAN-13":
		return "ean13"
	case "EAN-8":
		return "ean8"
	case "UPC-A":
		return "upca"
	case "UPC-E":
		return "upce"
	case "CODE-128":
		return "code128"
	case "CODE-39":
		return "code39"
	case "CODE-93":
		return "code93"
	case "Codabar", "CODABAR":
		return "codabar"
	case "I2/5":
		return "itf"
	case "PDF417":
		return "pdf417"
	}
	return ""
}

// visLines groups Tesseract's words in their lines, each word's place
// in its line's text in runes.
func visLines(words []TessWord, box func(x0, y0, x1, y1 int) Box) []Line {
	lines := []Line{}
	at := 0 // bytes into the line's text searched
	for _, wd := range words {
		if wd.NewLine {
			text := strings.TrimRight(wd.Line, " \n")
			if strings.TrimSpace(text) == "" {
				continue
			}
			lines = append(lines, Line{Text: text, Box: box(wd.LineBox[0], wd.LineBox[1], wd.LineBox[2], wd.LineBox[3]), Words: []Word{}})
			at = 0
		}
		if len(lines) == 0 || wd.Text == "" {
			continue
		}
		l := &lines[len(lines)-1]
		i := strings.Index(l.Text[at:], wd.Text)
		if i < 0 {
			continue
		}
		start := utf8.RuneCountInString(l.Text[:at+i])
		end := start + utf8.RuneCountInString(wd.Text)
		at += i + len(wd.Text)
		l.Words = append(l.Words, Word{Text: wd.Text, Box: box(wd.Box[0], wd.Box[1], wd.Box[2], wd.Box[3]), Start: start, End: end})
	}
	return lines
}

// tessCodes are Tesseract's names of languages, by BCP 47's.
var tessCodes = map[string]string{
	"en": "eng", "de": "deu", "fr": "fra", "es": "spa", "it": "ita", "pt": "por", "nl": "nld",
	"sv": "swe", "da": "dan", "nb": "nor", "no": "nor", "fi": "fin", "pl": "pol", "cs": "ces",
	"sk": "slk", "hu": "hun", "ro": "ron", "tr": "tur", "el": "ell", "ru": "rus", "uk": "ukr",
	"bg": "bul", "ar": "ara", "he": "heb", "fa": "fas", "hi": "hin", "th": "tha", "vi": "vie",
	"id": "ind", "ms": "msa", "ja": "jpn", "ko": "kor", "zh-Hans": "chi_sim", "zh-Hant": "chi_tra",
	"zh": "chi_sim", "zh-TW": "chi_tra", "zh-HK": "chi_tra", "ca": "cat", "hr": "hrv", "sr": "srp",
}

// tessDataDirs are where Tesseract's languages' data may be.
func tessDataDirs() []string {
	dirs := []string{}
	if d := os.Getenv("TESSDATA_PREFIX"); d != "" {
		dirs = append(dirs, d, filepath.Join(d, "tessdata"))
	}
	m, _ := filepath.Glob("/usr/share/tesseract-ocr/*/tessdata")
	return append(append(dirs, m...), "/usr/share/tessdata", "/usr/local/share/tessdata")
}

// tessLanguages is Tesseract's languages for the request's (none: the
// app's), those it has data for ("eng" if none).
func tessLanguages(tags []string, locale string) string {
	if len(tags) == 0 && locale != "" {
		tags = []string{strings.ReplaceAll(strings.SplitN(locale, ".", 2)[0], "_", "-")}
	}
	var out []string
	for _, t := range tags {
		code := tessCodes[t]
		if code == "" {
			base, _, _ := strings.Cut(t, "-")
			code = tessCodes[base]
		}
		if code == "" || inList(out, code) {
			continue
		}
		for _, d := range tessDataDirs() {
			if _, err := os.Stat(filepath.Join(d, code+".traineddata")); err == nil {
				out = append(out, code)
				break
			}
		}
	}
	if len(out) == 0 {
		return "eng"
	}
	return strings.Join(out, "+")
}
