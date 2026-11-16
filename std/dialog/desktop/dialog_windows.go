//go:build windows

package dialog

import (
	"mime"
	"os"
	"path/filepath"
	"slices"
	"strings"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the common item dialogs (IFileOpenDialog, IFileSaveDialog),
// modal to the window the user works in. They run a modal loop, posted
// work going on meanwhile.

func supported() error { return nil }

// fileDialog shows the open dialog (kind "open", "folder") or the save
// dialog ("save"). Main thread.
func fileDialog(kind string, r Request, done func(chosen)) {
	o := FileDialogOptions{Save: kind == "save", Folders: kind == "folder", Multiple: r.Multiple, Hidden: r.ShowHidden,
		Title: r.Title, Button: r.Button}
	o.Folder, o.Name = startIn(r.Start, kind == "folder")
	if r.Name != "" {
		o.Name = r.Name
	}
	switch {
	case kind == "folder":
		o.Name = ""
	case kind == "open":
		// A file that's there is selected (its name in the name field);
		// Name is save's.
		if st, err := os.Stat(r.Start); err != nil || st.IsDir() {
			o.Name = ""
		} else {
			o.Name = filepath.Base(r.Start)
		}
	}
	if kind != "folder" {
		o.Filters, o.DefaultExt = dialogFilters(r.Filters)
	}
	paths, err := FileDialog(native.Window(), o)
	done(chosen{paths, err})
}

// typeFamilies are the extensions of a family of MIME types, as
// Windows's own apps know them.
var typeFamilies = map[string][]string{
	"image/*": {"png", "jpg", "jpeg", "gif", "bmp", "webp", "heic", "heif", "tif", "tiff", "ico", "svg", "avif"},
	"video/*": {"mp4", "m4v", "mov", "webm", "avi", "wmv", "mkv"},
	"audio/*": {"mp3", "m4a", "aac", "wav", "flac", "ogg", "opus", "wma"},
	"text/*":  {"txt", "csv", "md", "html", "htm", "xml", "json", "log"},
	"font/*":  {"ttf", "otf", "woff", "woff2"},
}

// dialogFilters are the dialog's file types (each filter's patterns: its
// extensions, its MIME types' as the registry gives them) and the
// extension a name typed without one gets (the first filter's first).
func dialogFilters(fs []Filter) ([]FileFilter, string) {
	var out []FileFilter
	def := ""
	for _, f := range fs {
		var pats []string
		add := func(ext string) {
			p := "*." + strings.TrimPrefix(ext, ".")
			if ext == "*" {
				p = "*.*"
			}
			if !slices.Contains(pats, p) {
				pats = append(pats, p)
			}
		}
		for _, e := range f.Extensions {
			add(e)
		}
		for _, t := range f.Types {
			switch {
			case t == "*/*" || t == "*" || t == "application/*":
				add("*")
			case typeFamilies[t] != nil:
				for _, e := range typeFamilies[t] {
					add(e)
				}
			default:
				exts, _ := mime.ExtensionsByType(t)
				for _, e := range exts {
					add(e)
				}
			}
		}
		if len(pats) == 0 {
			continue
		}
		if def == "" && len(out) == 0 && pats[0] != "*.*" {
			def = strings.TrimPrefix(pats[0], "*.")
		}
		out = append(out, FileFilter{Name: f.Name, Patterns: strings.Join(pats, ";")})
	}
	return out, def
}
