//go:build linux && !android

package device

import (
	"bufio"
	"context"
	"errors"
	"os"
	"runtime"
	"strings"

	"github.com/egoist/asuka/internal/boot"
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: GtkFileDialog for the pickers (the portal's in a sandbox), modal
// to the window the user works in; GtkUriLauncher for the browser;
// GdkClipboard; the session kept from idling (gtk_application_inhibit).

func supported() error { return nil }

func (platform) Info(context.Context) (Info, error) {
	return Info{Model: "PC", System: "Linux", SystemVersion: osVersion(), AppVersion: boot.Version, Build: boot.Build}, nil
}

// osVersion is the distribution and its version (os-release's NAME and
// VERSION_ID: the host's in a Flatpak).
func osVersion() string {
	for _, path := range []string{"/run/host/os-release", "/etc/os-release", "/usr/lib/os-release"} {
		if r := osRelease(path); r["NAME"] != "" {
			return strings.TrimSpace(r["NAME"] + " " + r["VERSION_ID"])
		}
	}
	return runtime.GOOS
}

func osRelease(path string) map[string]string {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	out := map[string]string{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		k, v, ok := strings.Cut(strings.TrimSpace(sc.Text()), "=")
		if !ok || strings.HasPrefix(k, "#") {
			continue
		}
		if len(v) >= 2 && (v[0] == '"' || v[0] == '\'') && v[len(v)-1] == v[0] {
			v = v[1 : len(v)-1]
		}
		out[k] = v
	}
	return out
}

func (platform) Browse(_ context.Context, u string) error {
	if err := webURL(u); err != nil {
		return err
	}
	native.Post(func() {
		l := UriLauncherNew(u)
		UriLauncherLaunch(l, native.Window(), 0, 0, 0)
		ObjectUnref(l)
	})
	return nil
}

type picked struct {
	paths []string
	err   error
}

// pick shows the open dialog and copies what's chosen.
func pick(ctx context.Context, title string, mimes []string, multiple bool, limit int) ([]File, error) {
	p, err := wait(ctx, func(done func(picked)) { openDialog(title, mimes, multiple, done) })
	if err != nil {
		return nil, err
	}
	if p.err != nil || len(p.paths) == 0 {
		return []File{}, p.err
	}
	if limit > 0 && len(p.paths) > limit {
		p.paths = p.paths[:limit]
	}
	return copyPicked(cacheDir("asuka-files"), p.paths)
}

// openDialog shows GTK's open dialog for files of the MIME types (none:
// any file). Cancelled: no paths, no error. Main thread.
func openDialog(title string, mimes []string, multiple bool, done func(picked)) {
	d := Sys.FileDialogNew()
	if title != "" {
		Sys.FileDialogSetTitle(d, title)
	}
	Sys.FileDialogSetModal(d, true)
	if f := fileFilter(mimes); f != 0 {
		store := Sys.ListStoreNew(Sys.FileFilterGetType())
		Sys.ListStoreAppend(store, f)
		Sys.FileDialogSetFilters(d, store)
		Sys.FileDialogSetDefaultFilter(d, f)
		ObjectUnref(store)
		ObjectUnref(f)
	}
	parent := native.Window()
	finish := func(paths []string, gerr Ptr) {
		ObjectUnref(d)
		switch {
		case gerr != 0 && Cancelled(gerr):
			ErrorFree(gerr)
			done(picked{})
		case gerr != 0:
			done(picked{err: GError(gerr)})
		default:
			done(picked{paths: paths})
		}
	}
	if multiple {
		Async(func(a AsyncCall) { Sys.FileDialogOpenMultiple(d, parent, 0, a.Callback, a.Data) }, func(_, res Ptr) {
			var gerr Ptr
			list := Sys.FileDialogOpenMultipleFin(d, res, &gerr)
			var paths []string
			if list != 0 {
				n := Sys.ListModelGetNItems(list)
				for i := uint32(0); i < n; i++ {
					f := Sys.ListModelGetItem(list, i)
					if p := GoStringFree(Sys.FileGetPath(f)); p != "" {
						paths = append(paths, p)
					}
					ObjectUnref(f)
				}
				ObjectUnref(list)
			}
			finish(paths, gerr)
		})
		return
	}
	Async(func(a AsyncCall) { Sys.FileDialogOpen(d, parent, 0, a.Callback, a.Data) }, func(_, res Ptr) {
		var gerr Ptr
		f := Sys.FileDialogOpenFinish(d, res, &gerr)
		var paths []string
		if f != 0 {
			if p := GoStringFree(Sys.FileGetPath(f)); p != "" {
				paths = append(paths, p)
			}
			ObjectUnref(f)
		}
		finish(paths, gerr)
	})
}

// fileFilter is a filter of the MIME types (0: any file).
func fileFilter(mimes []string) Ptr {
	var ms []string
	for _, m := range mimes {
		m = strings.TrimSpace(m)
		if m == "" || m == "*/*" {
			return 0
		}
		ms = append(ms, m)
	}
	if len(ms) == 0 {
		return 0
	}
	f := Sys.FileFilterNew()
	names := map[string]string{"image/*": "Images", "video/*": "Videos", "audio/*": "Audio", "text/*": "Text"}
	name := strings.Join(ms, ", ")
	if len(ms) == 1 && names[ms[0]] != "" {
		name = names[ms[0]]
	}
	Sys.FileFilterSetName(f, name)
	for _, m := range ms {
		Sys.FileFilterAddMIMEType(f, m)
	}
	return f
}

func (platform) PickPhotos(ctx context.Context, max int) ([]string, error) {
	files, err := pick(ctx, "Choose Images", []string{"image/*"}, max != 1, max)
	return photos(files), err
}

func (platform) PickFiles(ctx context.Context, types []string, multiple bool) ([]File, error) {
	return pick(ctx, "", types, multiple, 0)
}

func clipboard() Ptr { return DisplayGetClipboard(DisplayGetDefault()) }

func (platform) ClipboardText(ctx context.Context) (*string, error) {
	return wait(ctx, func(done func(*string)) {
		cb := clipboard()
		Async(func(a AsyncCall) { ClipboardReadTextAsync(cb, 0, a.Callback, a.Data) }, func(_, res Ptr) {
			var gerr Ptr
			p := ClipboardReadTextFinish(cb, res, &gerr)
			if gerr != 0 {
				ErrorFree(gerr)
			}
			if p == 0 {
				done(nil)
				return
			}
			s := GoStringFree(p)
			done(&s)
		})
	})
}

func kinds() ClipboardKinds {
	f := Sys.ClipboardGetFormats(clipboard())
	text := Sys.ContentFormatsContainMIME(f, "text/plain;charset=utf-8") || Sys.ContentFormatsContainMIME(f, "text/plain") ||
		Sys.ContentFormatsContainGType(f, TypeString)
	img := Sys.ContentFormatsContainGType(f, Sys.TextureGetType()) || Sys.ContentFormatsContainMIME(f, "image/png")
	url := Sys.ContentFormatsContainMIME(f, "text/uri-list") || Sys.ContentFormatsContainGType(f, Sys.FileListGetType()) ||
		Sys.ContentFormatsContainGType(f, Sys.FileGetTypeOfObject())
	return ClipboardKinds{Text: text, Image: img, Url: url}
}

func (platform) ClipboardHas(context.Context) (k ClipboardKinds, err error) {
	native.OnMain(func() { k = kinds() })
	return k, nil
}

func (platform) ClipboardImage(ctx context.Context) (*string, error) {
	png, err := wait(ctx, func(done func([]byte)) {
		cb := clipboard()
		if !kinds().Image {
			done(nil)
			return
		}
		Async(func(a AsyncCall) { Sys.ClipboardReadTextureAsync(cb, 0, a.Callback, a.Data) }, func(_, res Ptr) {
			var gerr Ptr
			tex := Sys.ClipboardReadTextureFinish(cb, res, &gerr)
			if tex == 0 {
				if gerr != 0 {
					ErrorFree(gerr)
				}
				done(nil)
				return
			}
			bs := Sys.TextureSaveToPNGBytes(tex)
			ObjectUnref(tex)
			var n uintptr
			b := GoBytes(BytesGetData(bs, &n), int(n))
			BytesUnref(bs)
			done(b)
		})
	})
	if err != nil || png == nil {
		return nil, err
	}
	path, err := writeCache("asuka-clipboard", "png", png)
	if err != nil {
		return nil, err
	}
	return &path, nil
}

func (platform) CopyImage(_ context.Context, path string) error {
	if _, err := os.Stat(path); err != nil {
		return errors.New("device: no file at " + path)
	}
	var err error
	native.OnMain(func() {
		var gerr Ptr
		tex := TextureNewFromFilename(path, &gerr)
		if tex == 0 {
			if gerr != 0 {
				ErrorFree(gerr)
			}
			err = errors.New("device: not an image: " + path)
			return
		}
		Sys.ClipboardSetTexture(clipboard(), tex)
		ObjectUnref(tex)
	})
	return err
}

func (platform) WatchClipboard(ctx context.Context, changed func(ClipboardKinds)) error {
	var cb Ptr
	var handler uint64
	native.OnMain(func() {
		cb = clipboard()
		handler = Connect(cb, "changed", func() { changed(kinds()) })
	})
	<-ctx.Done()
	native.Post(func() { Disconnect(cb, handler) })
	return ctx.Err()
}

// inhibit is the session's idle inhibition while the screen's kept on.
var inhibit uint32

func (platform) KeepAwake(on bool) error {
	if on == (inhibit != 0) {
		return nil
	}
	app := Call(Sym("g_application_get_default"))
	if app == 0 {
		return nil
	}
	if on {
		inhibit = ApplicationInhibit(app, native.Window(), 8, "Keeping the screen on") // GTK_APPLICATION_INHIBIT_IDLE
		return nil
	}
	ApplicationUninhibit(app, inhibit)
	inhibit = 0
	return nil
}
